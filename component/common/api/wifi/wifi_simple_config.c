#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "udp.h"
#include <sockets.h>
#include <lwip_netconf.h>
#include <osdep_service.h>
#include "platform_stdlib.h"
#include "wifi_simple_config_parser.h"
#include "wifi_simple_config.h"

#if CONFIG_EXAMPLE_UART_ATCMD
#include "at_cmd/atcmd_wifi.h"
#endif

#define STACKSIZE         512
#define LEAVE_ACK_EARLY   0

#if (CONFIG_LWIP_LAYER == 0)
extern u32 _ntohl(u32 n);
#endif

#if CONFIG_WLAN
#if (CONFIG_INCLUDE_SIMPLE_CONFIG)
#include "wifi/wifi_conf.h"
int is_promisc_callback_unlock = 0;
static int is_fixed_channel;
int fixed_channel_num;
unsigned char g_ssid[32];
int g_ssid_len;

extern int promisc_get_fixed_channel( void *, u8 *, int* );
struct rtk_test_sc;

#ifdef PACK_STRUCT_USE_INCLUDES
#include "arch/bpstruct.h"
#endif

// support scan list function from APP, comment by default
//#define SC_SCAN_SUPPORT

PACK_STRUCT_BEGIN
struct ack_msg {
	PACK_STRUCT_FIELD(u8_t flag);
	PACK_STRUCT_FIELD(u16_t length);
	PACK_STRUCT_FIELD(u8_t smac[6]);
	PACK_STRUCT_FIELD(u8_t status);
	PACK_STRUCT_FIELD(u16_t device_type);
	PACK_STRUCT_FIELD(u32_t device_ip);
	PACK_STRUCT_FIELD(u8_t device_name[64]);
};PACK_STRUCT_STRUCT;
PACK_STRUCT_END
#ifdef PACK_STRUCT_USE_INCLUDES
#include "arch/epstruct.h"
#endif



#define 	MULCAST_PORT		(8864)

#define 	SCAN_BUFFER_LENGTH	(1024)

#ifndef WLAN0_NAME
  #define WLAN0_NAME		"wlan0"
#endif
#define JOIN_SIMPLE_CONFIG          (uint32_t)(1 << 8)
extern uint32_t rtw_join_status;
char simple_config_terminate = 0;

int simple_config_result;
static struct ack_msg  *ack_content;
struct rtk_test_sc *backup_sc_ctx;
extern struct netif xnetif[NET_IF_NUM];

// listen scan command and ACK 
#ifdef SC_SCAN_SUPPORT

static int pin_enable = 0;
static int scan_start = 0;

#ifdef RTW_PACK_STRUCT_USE_INCLUDES
#include "pack_begin.h"
#endif
RTW_PACK_STRUCT_BEGIN
struct ack_msg_scan {
	u8_t flag;
	u16_t length;
	u8_t smac[6];
	u8_t status;
	u16_t device_type;
	u32_t device_ip;
	u8_t device_name[64];
	u8_t pin_enabled;
}
RTW_PACK_STRUCT_STRUCT;
RTW_PACK_STRUCT_END
#ifdef RTW_PACK_STRUCT_USE_INCLUDES
#include "pack_end.h"
#endif

static void set_device_name(char *device_name)
{
	int pos = 0;
	memcpy(device_name, "ameba_", 6);
	for(int i = 0; i < 3; i++)
	{
		sprintf(device_name + 6 + pos, "%02x", xnetif[0].hwaddr[i + 3]);
		pos += 2;
		if(i != 2)
			device_name[6 + pos++] = ':';
	}
	return;
}
void SC_scan_thread(void *para)
{
	int sockfd_scan;
	struct sockaddr_in device_addr;
	unsigned char packet[256];
	struct sockaddr from;
	struct sockaddr_in *from_sin = (struct sockaddr_in*) &from;
	socklen_t fromLen = sizeof(from);
	struct ack_msg_scan ack_msg;
	
	#ifdef RTW_PACK_STRUCT_USE_INCLUDES
	#include "pack_begin.h"
	#endif
	RTW_PACK_STRUCT_BEGIN
	struct scan_msg{
		unsigned char 	flag;
		unsigned short 	length;
		unsigned char		sec_level;
		unsigned char 	nonce[64];
		unsigned char 	digest[16];
		unsigned char 	smac[6];
		unsigned short 	device_type;	
	};
	RTW_PACK_STRUCT_STRUCT;
	RTW_PACK_STRUCT_END
	#ifdef RTW_PACK_STRUCT_USE_INCLUDES
	#include "pack_end.h"
	#endif
	
	struct scan_msg *pMsg;
	
	if ((sockfd_scan = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
		printf("SC scan socket error\n");
		return;
	}
	memset(&device_addr, 0, sizeof(struct sockaddr_in)); 
	device_addr.sin_family = AF_INET;
	device_addr.sin_port = htons(18864);
	device_addr.sin_addr.s_addr = INADDR_ANY;
	
	if (bind(sockfd_scan, (struct sockaddr *)&device_addr, sizeof(struct sockaddr)) == -1) 
	{
		printf("SC scan bind error\n");
		close(sockfd_scan);
		return;
	}
	memset(packet, 0, sizeof(packet));
	
	// for now, no checking for the validity of received data, wf, 0225
	while(1)
	{
		if((recvfrom(sockfd_scan, &packet, sizeof(packet), MSG_DONTWAIT, &from, &fromLen)) >= 0) {
			uint16_t from_port = ntohs(from_sin->sin_port);
			//printf("SC_scan: recv %d bytes from %d.%d.%d.%d:%d\n",packetLen, ip[0], ip[1], ip[2], ip[3], from_port);
			
			from_sin->sin_port = htons(8864); 		
			// send ACK for scan
			pMsg = (struct scan_msg *)packet;
			if(pMsg->flag == 0x00) // scan flag
			{
				ack_msg.flag = 0x21; 
				ack_msg.length = sizeof(struct ack_msg_scan);
				ack_msg.status = 1;
				memcpy(ack_msg.smac, xnetif[0].hwaddr, 6);

				ack_msg.device_type = 0;
				ack_msg.device_ip = xnetif[0].ip_addr.addr;
				memset(ack_msg.device_name, 0, 64);
				set_device_name((char*)ack_msg.device_name);
				// set the device_name to: ameba_xxxxxx(last 3 bytes of MAC)				
				ack_msg.pin_enabled = pin_enable;
				for(int i = 0; i < 3;i++)
				{
					int ret = sendto(sockfd_scan,(unsigned char *)&ack_msg,sizeof(struct ack_msg_scan),0,(struct sockaddr *)&from, fromLen);
					if(ret < 0)
						printf("send ACK for scan fail\n");
					//else
						//printf("send %d bytes of ACK to scan\n", ret);
				}				
			}
			else
	  		continue;
		}
		vTaskDelay(500);
	}
}

void SC_listen_ACK_scan()
{
	if(xTaskCreate(SC_scan_thread, ((const char*)"SC_scan_thread"), 512, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS)
		printf("\n\r%s xTaskCreate(SC_scan_thread) failed", __FUNCTION__);
}

#endif

void SC_set_ack_content()
{
	memset(ack_content, 0, sizeof(struct ack_msg));
	ack_content->flag = 0x20;
	ack_content->length = htons(sizeof(struct ack_msg)-3);
	memcpy(ack_content->smac, xnetif[0].hwaddr, 6);
	ack_content->status = 0;
	ack_content->device_type = 0;
	ack_content->device_ip = xnetif[0].ip_addr.addr;
	memset(ack_content->device_name, 0, 64);
}

int SC_send_simple_config_ack(u8 round)
{
#if CONFIG_LWIP_LAYER
	int ack_transmit_round, ack_num_each_sec;
	int ack_socket;
	//int sended_data = 0;
	struct sockaddr_in to_addr;
#if LEAVE_ACK_EARLY
	u8 check_phone_ack = 0;
#endif
	SC_set_ack_content();
	
	ack_socket = socket(PF_INET, SOCK_DGRAM, IP_PROTO_UDP);
	if (ack_socket == -1) {
		return 	-1;
	}
#if LEAVE_ACK_EARLY
	else {
		struct sockaddr_in bindAddr;
		bindAddr.sin_family = AF_INET;
		bindAddr.sin_port = htons(8864);
		bindAddr.sin_addr.s_addr = INADDR_ANY;
		if(bind(ack_socket, (struct sockaddr *) &bindAddr, sizeof(bindAddr)) == 0)
			check_phone_ack = 1;
	}
#endif
	printf("\nSending simple config ack\n");
	FD_ZERO(&to_addr);
	to_addr.sin_family = AF_INET;
	to_addr.sin_port = htons(8864);
	to_addr.sin_addr.s_addr = (backup_sc_ctx->ip_addr);
	for (ack_transmit_round = 0;ack_transmit_round < round; ack_transmit_round++) {
		for (ack_num_each_sec = 0;ack_num_each_sec < 20; ack_num_each_sec++) {
			//sended_data = 
			sendto(ack_socket, (unsigned char *)ack_content, sizeof(struct ack_msg), 0, (struct sockaddr *) &to_addr, sizeof(struct sockaddr));
			//printf("\r\nAlready send %d bytes data\n", sended_data);
			vTaskDelay(50);	/* delay 50 ms */

#if LEAVE_ACK_EARLY
			if(check_phone_ack) {
				unsigned char packet[100];
				int packetLen;
				struct sockaddr from;
				struct sockaddr_in *from_sin = (struct sockaddr_in*) &from;
				socklen_t fromLen = sizeof(from);

				if((packetLen = recvfrom(ack_socket, &packet, sizeof(packet), MSG_DONTWAIT, &from, &fromLen)) >= 0) {
					uint8_t *ip = (uint8_t *) &from_sin->sin_addr.s_addr;
					uint16_t from_port = ntohs(from_sin->sin_port);
					printf("recv %d bytes from %d.%d.%d.%d:%d at round=%d, num=%d\n",
							packetLen, ip[0], ip[1], ip[2], ip[3], from_port,
							ack_transmit_round, ack_num_each_sec);
					goto leave_ack;
				}
			}
#endif
		}
	}

leave_ack:
	close(ack_socket);
#endif

#if CONFIG_INIC_CMD_RSP
	extern unsigned int inic_sc_ip_addr;
	inic_sc_ip_addr = backup_sc_ctx->ip_addr;
	inic_c2h_wifi_info("ATWQ", RTW_SUCCESS);
#endif

	return 0;
}



static int  SC_check_and_show_connection_info(void)
{
	rtw_wifi_setting_t setting;	
	int ret = -1;

#if CONFIG_LWIP_LAYER
	/* If not rise priority, LwIP DHCP may timeout */
	vTaskPrioritySet(NULL, tskIDLE_PRIORITY + 3);	
	/* Start DHCP Client */
	ret = LwIP_DHCP(0, DHCP_START);
	vTaskPrioritySet(NULL, tskIDLE_PRIORITY + 1);	
#endif	
	
#if CONFIG_EXAMPLE_UART_ATCMD == 0
	wifi_get_setting(WLAN0_NAME, &setting);
	wifi_show_setting(WLAN0_NAME, &setting);
#endif

#if CONFIG_LWIP_LAYER
	if (ret != DHCP_ADDRESS_ASSIGNED)
		return SC_DHCP_FAIL;
	else
#endif
		return SC_SUCCESS;
}

static void check_and_set_security_in_connection(rtw_security_t security_mode, rtw_network_info_t *wifi) 
{
  	
	if (security_mode == RTW_SECURITY_WPA2_AES_PSK) {					
		printf("\r\nwifi->security_type = RTW_SECURITY_WPA2_AES_PSK\n");
		wifi->security_type = RTW_SECURITY_WPA2_AES_PSK;
	} else if (security_mode == RTW_SECURITY_WEP_PSK) {
		printf("\r\nwifi->security_type = RTW_SECURITY_WEP_PSK\n");
		wifi->security_type = RTW_SECURITY_WEP_PSK;
		wifi->key_id = 0;
	} else if (security_mode == RTW_SECURITY_WPA_AES_PSK) {
		printf("\r\nwifi->security_type = RTW_SECURITY_WPA_AES_PSK\n");
		wifi->security_type = RTW_SECURITY_WPA_AES_PSK;
	} else {
		printf("\r\nwifi->security_type = RTW_SECURITY_OPEN\n");
		wifi->security_type = RTW_SECURITY_OPEN;
	}
}

int get_connection_info_from_profile(rtw_security_t security_mode, rtw_network_info_t *wifi)
{

	printf("\r\n======= Connection Information =======\n");
	check_and_set_security_in_connection(security_mode, wifi);

	wifi->password = backup_sc_ctx->password;
	wifi->password_len = (int)strlen((char const *)backup_sc_ctx->password);

	/* 1.both scanned g_ssid and ssid from profile are null, return fail */
	if ((0 == g_ssid_len) && (0 == strlen(backup_sc_ctx->ssid))) {
		printf("no ssid info found, connect will fail\n");
		return -1;
	}

	/* g_ssid and ssid from profile are same, enter connect and retry */
	if (0 == strcmp(backup_sc_ctx->ssid, g_ssid)) {
		wifi->ssid.len = strlen(backup_sc_ctx->ssid);
		rtw_memcpy(wifi->ssid.val, backup_sc_ctx->ssid, wifi->ssid.len);
		printf("using ssid from profile and scan result\n");
		goto ssid_set_done;
	}

	/* if there is profile, but g_ssid and profile are different, using profile to connect and retry */
	if (strlen(backup_sc_ctx->ssid) > 0) {
		wifi->ssid.len = strlen(backup_sc_ctx->ssid);
		rtw_memcpy(wifi->ssid.val, backup_sc_ctx->ssid, wifi->ssid.len);
		printf("using ssid only from profile\n");
		goto ssid_set_done;
	
}
	
	/* if there is no profile but have scanned ssid, using g_ssid to connect and retry
		(maybe ssid is right and password is wrong) */
	if (g_ssid_len > 0) {
		wifi->ssid.len = g_ssid_len;
		rtw_memcpy(wifi->ssid.val, g_ssid, wifi->ssid.len);
		printf("using ssid only from scan result\n");
		goto ssid_set_done;
	}


ssid_set_done:

	
	if(wifi->security_type == RTW_SECURITY_WEP_PSK)
	{
		if(wifi->password_len == 10)
		{
			u32 p[5] = {0};
			u8 pwd[6], i = 0; 
			sscanf((const char*)backup_sc_ctx->password, "%02x%02x%02x%02x%02x", &p[0], &p[1], &p[2], &p[3], &p[4]);
			for(i=0; i< 5; i++)
				pwd[i] = (u8)p[i];
			pwd[5] = '\0';
			memset(backup_sc_ctx->password, 0, 65);
			strcpy((char*)backup_sc_ctx->password, (char*)pwd);
			wifi->password_len = 5;
		}else if(wifi->password_len == 26){
			u32 p[13] = {0};
			u8 pwd[14], i = 0;
			sscanf((const char*)backup_sc_ctx->password, "%02x%02x%02x%02x%02x%02x%02x"\
				"%02x%02x%02x%02x%02x%02x", &p[0], &p[1], &p[2], &p[3], &p[4],\
				&p[5], &p[6], &p[7], &p[8], &p[9], &p[10], &p[11], &p[12]);
			for(i=0; i< 13; i++)
				pwd[i] = (u8)p[i];
			pwd[13] = '\0';
			memset(backup_sc_ctx->password, 0, 64);
			strcpy((char*)backup_sc_ctx->password, (char*)pwd);
			wifi->password_len = 13;
		}
	}
	printf("\r\nwifi.password = %s\n", wifi->password);
	printf("\r\nwifi.password_len = %d\n", wifi->password_len);
	printf("\r\nwifi.ssid = %s\n", wifi->ssid.val);				
	printf("\r\nwifi.ssid_len = %d\n", wifi->ssid.len);
	printf("\r\nwifi.channel = %d\n", fixed_channel_num);
	printf("\r\n===== start to connect target AP =====\n");
	return 0;
}




#pragma pack(1)
struct scan_with_ssid_result {
	u8 len; /* len of a memory area store ap info */
	u8 mac[ETH_ALEN];
	int rssi;
	u8 sec_mode;
	u8 password_id;
	u8 channel;
	//char ssid[65];
};


struct sc_ap_info {

	char *ssid;
	int ssid_len;

};



rtw_security_t	SC_translate_iw_security_mode(u8 security_type) {

	rtw_security_t security_mode = RTW_SECURITY_UNKNOWN;


	switch (security_type) {
	case IW_ENCODE_ALG_NONE:
		security_mode = RTW_SECURITY_OPEN;
	break;
	case IW_ENCODE_ALG_WEP:
		security_mode = RTW_SECURITY_WEP_PSK;
	break;
	case IW_ENCODE_ALG_CCMP:
		security_mode = RTW_SECURITY_WPA2_AES_PSK;
	break;
	default:
		printf("error: security type not supported\n");
	break;
	};

	return security_mode;
}

/*

	scan buf format:

	len	mac	rssi	sec	wps	channel	ssid
	1B	6B	4B	1B	1B		1B		(len - 14)B

*/
enum sc_result SC_parse_scan_result_and_connect(scan_buf_arg* scan_buf, rtw_network_info_t *wifi)
{

	struct scan_with_ssid_result scan_result;

	char *buf = scan_buf->buf;
	int buf_len = scan_buf->buf_len;
	char ssid[65];
	int ssid_len = 0 ;
	int parsed_len = 0;
	u8 scan_channel = 0;
	int i = 0;
	int ret = 0;
	u8 pscan_config = PSCAN_ENABLE | PSCAN_SIMPLE_CONFIG;

	memset((void*)&scan_result, 0, sizeof(struct scan_with_ssid_result));

	/* if wifi_is_connected_to_ap and we run here, ther will be hardfault(caused by auto reconnect) */
	printf("Scan result got, start to connect AP with scanned bssid\n");

	while (1) {

		memcpy(&scan_result, buf, sizeof(struct scan_with_ssid_result));
		/* len maybe 3*/
		if  (scan_result.len < sizeof(struct scan_with_ssid_result)) {
			printf("length = %d, too small!\n",scan_result.len);
			goto sc_connect_wifi_fail;
		}

		/* set ssid */
		memset(ssid, 0, 65);

		ssid_len = scan_result.len - sizeof(struct scan_with_ssid_result);
		
		memcpy(ssid, buf + sizeof(struct scan_with_ssid_result), ssid_len);
		
		/* run here means there is a match */
		if (ssid_len == wifi->ssid.len) {
			if (memcmp(ssid, wifi->ssid.val, ssid_len) == 0) {

				printf("Connecting to  MAC=%02x:%02x:%02x:%02x:%02x:%02x, ssid = %s, SEC=%d\n",
				scan_result.mac[0], scan_result.mac[1], scan_result.mac[2],
				scan_result.mac[3], scan_result.mac[4], scan_result.mac[5],
				ssid, scan_result.sec_mode);

				scan_channel = scan_result.channel;


				/* try 3 times to connect */
				for (i = 0; i < 3; i++) {
					if(wifi_set_pscan_chan(&scan_channel, &pscan_config, 1) < 0){
						printf("\n\rERROR: wifi set partial scan channel fail");
						ret = SC_TARGET_CHANNEL_SCAN_FAIL;
						goto sc_connect_wifi_fail;
					}
					ret = wifi_connect_bssid(scan_result.mac,  (char*)wifi->ssid.val,  SC_translate_iw_security_mode(scan_result.sec_mode), 
						(char*)wifi->password,  ETH_ALEN,  wifi->ssid.len,  wifi->password_len,  0,  NULL);
					if (ret == RTW_SUCCESS)
						goto sc_connect_wifi_success;
				}

			}
		}


		buf = buf + scan_result.len;
		parsed_len += scan_result.len;
		if (parsed_len >= buf_len) {
			printf("parsed=%d, total = %d\n", parsed_len, buf_len);
			break;
		}
		
	}
	

sc_connect_wifi_success:
	printf("%s success\n", __FUNCTION__);
	return ret;
	
sc_connect_wifi_fail:
	printf("%s fail\n", __FUNCTION__);
	return ret;

	
}


/*

	When BSSID_CHECK_SUPPORT is not set, there will be problems:

	1.AP1 and AP2 (different SSID) both forward simple config packets, 
		profile is from AP2, but Ameba connect with AP1 
	2.AP1 and AP2 (same SSID, but different crypto or password), both forward simple config packets,
		profile is from AP2, but Ameba connect with AP1
	3. ...

	fix: using SSID to query matched BSSID(s) in scan result, traverse and connect.


	Consideration:
	1.Only take ssid and password
	2.Assume they have different channel.
	3.Assume they have different encrypt methods
	
*/
int  SC_connect_to_candidate_AP (rtw_network_info_t *wifi){

	int ret;

	scan_buf_arg scan_buf;
	int scan_cnt = 0;
	char *ssid = (char*)wifi->ssid.val;
	int ssid_len = wifi->ssid.len;
	

	printf("\nConnect with SSID=%s  password=%s\n", wifi->ssid.val, wifi->password);

	/* scan buf init */
	scan_buf.buf_len = 1000;
	scan_buf.buf = (char*)pvPortMalloc(scan_buf.buf_len);
	if(!scan_buf.buf){
		printf("\n\rERROR: Can't malloc memory");
		return RTW_NOMEM;
	}

	/* set ssid_len, ssid to scan buf */
	memset(scan_buf.buf, 0, scan_buf.buf_len);
	if(ssid && ssid_len > 0 && ssid_len <= 32){
		memcpy(scan_buf.buf, &ssid_len, sizeof(int));
		memcpy(scan_buf.buf+sizeof(int), ssid, ssid_len);
	}

	/* call wifi scan to scan */
	if(scan_cnt = (wifi_scan(RTW_SCAN_TYPE_ACTIVE, RTW_BSS_TYPE_ANY, &scan_buf)) < 0){
		printf("\n\rERROR: wifi scan failed");
		ret = RTW_ERROR;
	}else{
		ret  = SC_parse_scan_result_and_connect(&scan_buf, wifi);
	}

	if(scan_buf.buf)
	    vPortFree(scan_buf.buf);
	
	return ret;
}




rtw_security_t SC_translate_security(u8 security_type)
{

	rtw_security_t security_mode = RTW_SECURITY_UNKNOWN;

	switch (security_type) {
  	case RTW_ENCRYPTION_OPEN:
    		security_mode = RTW_SECURITY_OPEN;
    	break;
  	case RTW_ENCRYPTION_WEP40:
  	case RTW_ENCRYPTION_WEP104:
    		security_mode = RTW_SECURITY_WEP_PSK;
    	break;
  	case RTW_ENCRYPTION_WPA_TKIP:
  	case RTW_ENCRYPTION_WPA_AES:
  	case RTW_ENCRYPTION_WPA2_TKIP:
  	case RTW_ENCRYPTION_WPA2_AES:
  	case RTW_ENCRYPTION_WPA2_MIXED:
    		security_mode = RTW_SECURITY_WPA2_AES_PSK;
    	break;
  	case RTW_ENCRYPTION_UNKNOWN:
  	case RTW_ENCRYPTION_UNDEF:
  	default:
		printf( "unknow security mode,connect fail\n");
	}

	return security_mode;

}


enum sc_result SC_connect_to_AP(void)
{
	enum sc_result ret = SC_ERROR;
	u8 scan_channel;
	u8 pscan_config;
	int max_retry = 5, retry = 0;
	rtw_security_t security_mode;
	rtw_network_info_t wifi = {0};
	if(!(fixed_channel_num == 0)){
		scan_channel = fixed_channel_num;
	}
	pscan_config = PSCAN_ENABLE | PSCAN_SIMPLE_CONFIG;

	security_mode = SC_translate_security(g_security_mode);
	g_security_mode = 0xff;//clear it

	if (-1 == get_connection_info_from_profile(security_mode, &wifi)) {
		ret = SC_CONTROLLER_INFO_PARSE_FAIL;
		goto wifi_connect_fail;
	}

#if CONFIG_AUTO_RECONNECT
	/* disable auto reconnect */
	wifi_set_autoreconnect(0);
#endif

#if 1
	/* optimization: get g_bssid to connect with only pscan */
	while (1) {
		if(wifi_set_pscan_chan(&scan_channel, &pscan_config, 1) < 0){
			printf("\n\rERROR: wifi set partial scan channel fail");
			ret = SC_TARGET_CHANNEL_SCAN_FAIL;
			goto wifi_connect_fail;
		}
        rtw_join_status = 0;//clear simple config status
		ret = wifi_connect_bssid(g_bssid,  (char*)wifi.ssid.val,  wifi.security_type,  (char*)wifi.password,
									  ETH_ALEN,  wifi.ssid.len,  wifi.password_len,  wifi.key_id,  NULL);

		if (ret == RTW_SUCCESS)
			goto wifi_connect_success;

		if (retry == max_retry) {
			printf("connect fail with bssid, try ssid instead\n");
			break;
		}
		retry ++;
	}
#endif




#if 1
	/* when optimization fail: if connect with bssid fail because of we have connect to the wrong AP */
	ret = SC_connect_to_candidate_AP(&wifi);
	if (RTW_SUCCESS == ret) {
		goto wifi_connect_success;
	} else {
		ret = SC_JOIN_BSS_FAIL;
		goto wifi_connect_fail;
	}
#endif


wifi_connect_success:
	ret = SC_check_and_show_connection_info();
	goto wifi_connect_end;

	
wifi_connect_fail:
	printf("SC_connect_to_AP failed\n");
	goto wifi_connect_end;

wifi_connect_end:
#if CONFIG_AUTO_RECONNECT
	wifi_config_autoreconnect(1, 10, 5);
#endif
	return ret;


}
/* Make callback one by one to wlan rx when promiscuous mode */

void simple_config_callback(unsigned char *buf, unsigned int len, void* userdata)
{
	unsigned char * da = buf;
	unsigned char * sa = buf + ETH_ALEN;
	taskENTER_CRITICAL();
	if (is_promisc_callback_unlock == 1) {
	 	simple_config_result = rtk_start_parse_packet(da, sa, len, userdata, (void *)backup_sc_ctx);
		//printf("\r\nresult in callback function = %d\n",simple_config_result);
	} 
	taskEXIT_CRITICAL();

}

static unsigned int simple_config_cmd_start_time;
static unsigned int simple_config_cmd_current_time;
extern int simple_config_status;
extern void rtk_restart_simple_config(void);


extern void rtk_sc_deinit(void);

void init_simple_config_lib_config(struct simple_config_lib_config* config)
{
	config->free = rtw_mfree;
	config->malloc = rtw_malloc;
	config->memcmp = memcmp;
	config->memcpy = memcpy;
	config->memset = memset;
	config->printf = printf;
	config->strcpy = strcpy;
	config->strlen = strlen;
	config->zmalloc = rtw_zmalloc;
#if CONFIG_LWIP_LAYER
	config->_ntohl = lwip_ntohl;
#else
	config->_ntohl = _ntohl;
#endif
	config->is_promisc_callback_unlock = &is_promisc_callback_unlock;
}


int init_test_data(char *custom_pin_code)
{
#if (CONFIG_INCLUDE_SIMPLE_CONFIG)
	is_promisc_callback_unlock = 1;
	is_fixed_channel = 0;
	fixed_channel_num = 0;
	simple_config_result = 0;
	rtw_memset(g_ssid, 0, 32);
	g_ssid_len = 0;
	simple_config_cmd_start_time = xTaskGetTickCount();
	
	if (ack_content != NULL) {
		vPortFree(ack_content);
		ack_content = NULL;
	}
	ack_content = pvPortMalloc(sizeof(struct ack_msg));
	if (!ack_content) {
		printf("\n\rrtk_sc_init fail by allocate ack\n");
	}	
	memset(ack_content, 0, sizeof(struct ack_msg));

#ifdef SC_SCAN_SUPPORT
	if(custom_pin_code)
		pin_enable = 1;
	else
		pin_enable = 0;
#endif	

	backup_sc_ctx = pvPortMalloc(sizeof(struct rtk_test_sc));
	if (!backup_sc_ctx) {
		printf("\n\r[Mem]malloc SC context fail\n");
	} else {
		memset(backup_sc_ctx, 0, sizeof(struct rtk_test_sc));	
		struct simple_config_lib_config lib_config;
		init_simple_config_lib_config(&lib_config);
		//custom_pin_code can be null
		if (rtk_sc_init(custom_pin_code, &lib_config) < 0) {
			printf("\n\rRtk_sc_init fail\n");
		} else {
			return 0;
		}
	}	

#else
	printf("\n\rPlatform no include simple config now\n");
#endif	
	return -1;
}

void deinit_test_data(){
#if (CONFIG_INCLUDE_SIMPLE_CONFIG)
	rtk_sc_deinit();
	if (backup_sc_ctx != NULL) {
		vPortFree(backup_sc_ctx);
		backup_sc_ctx = NULL;
	}	
	if (ack_content != NULL) {
		vPortFree(ack_content);
		ack_content = NULL;
	}
	rtw_join_status = 0;//clear simple config status
#endif
}

void stop_simple_config()
{
	simple_config_terminate = 1;
}

enum sc_result simple_config_test(rtw_network_info_t *wifi)
{
	int channel = 1;
	enum sc_result ret = SC_SUCCESS;
	unsigned int start_time;
	int is_need_connect_to_AP = 0;
	int fix_channel = 0;
	int delta_time = 0;
	wifi_set_promisc(RTW_PROMISC_ENABLE, simple_config_callback, 1);
	start_time = xTaskGetTickCount();
	printf("\n\r");
	wifi_set_channel(channel);
	while (simple_config_terminate != 1) {
	  	vTaskDelay(50);	//delay 0.5s to release CPU usage
	  	simple_config_cmd_current_time = xTaskGetTickCount();
#if CONFIG_GAGENT
	  	if (simple_config_cmd_current_time - simple_config_cmd_start_time < ((50 + delta_time)*configTICK_RATE_HZ)) {
#else
	  	if (simple_config_cmd_current_time - simple_config_cmd_start_time < ((120 + delta_time)*configTICK_RATE_HZ)) {
#endif
			unsigned int current_time = xTaskGetTickCount();
			if (((current_time - start_time)*1000 /configTICK_RATE_HZ < 100)
								|| (is_fixed_channel == 1)) {
				if(is_fixed_channel == 0 && get_channel_flag == 1){
				    fix_channel = promisc_get_fixed_channel(g_bssid,g_ssid,&g_ssid_len);
				    if(fix_channel != 0)
				    {
    					printf("\r\nin simple_config_test fix channel = %d ssid: %s\n",fix_channel, g_ssid);
    					is_fixed_channel = 1;
    					fixed_channel_num = fix_channel;
    					wifi_set_channel(fix_channel);				    
				    }
				    else
				        printf("get channel fail\n");
				}
				
				if (simple_config_result == 1) {  
					is_need_connect_to_AP = 1;
					is_fixed_channel = 0;	      
					break;
				} 
				if (simple_config_result == -1) {  
					printf("\r\nsimple_config_test restart for result = -1");
					delta_time = 60;
					wifi_set_channel(1);	
					is_need_connect_to_AP = 0;
					is_fixed_channel = 0;
	               		fixed_channel_num = 0;
					memset(g_ssid, 0, 32);
					g_ssid_len = 0;
					simple_config_result = 0;
					g_security_mode = 0xff;
					rtk_restart_simple_config();					
				} 
				if (simple_config_result == -2) {
					printf("\n\rThe APP or client must have pin!\n");
					break;
				}
			} else {
					channel++;
					if ((1 <= channel) && (channel <= 13)) {
						if (wifi_set_channel(channel) == 0) {	
							start_time = xTaskGetTickCount();
							printf("\n\rSwitch to channel(%d)\n", channel);
						}	
					} else {
						channel = 1;
						if (wifi_set_channel(channel) == 0) {	
							start_time = xTaskGetTickCount();
							printf("\n\rSwitch to channel(%d)\n", channel);
						}	
					}	
					
			}
		} else {
			ret = SC_NO_CONTROLLER_FOUND;
			break;
		} 
	}
		wifi_set_promisc(RTW_PROMISC_DISABLE, NULL, 0);
	if (is_need_connect_to_AP == 1) {
		if(NULL == wifi){
			int tmp_res = SC_connect_to_AP();
			if (SC_SUCCESS == tmp_res) {
				if(-1 == SC_send_simple_config_ack(10))
					ret = SC_UDP_SOCKET_CREATE_FAIL;
				#ifdef SC_SCAN_SUPPORT
				  // check whether the thread of listen scan command is already created
				  if(scan_start == 0)
				  {
				  	scan_start = 1;
				  	SC_listen_ACK_scan();
				  }
				#endif	
			} else {
				ret = tmp_res;
			}
		}else{
			if (-1 == get_connection_info_from_profile(wifi->security_type,wifi)) {
				ret = SC_CONTROLLER_INFO_PARSE_FAIL;
			}else
				ret = SC_SUCCESS;
		}
	}else{
		ret = SC_NO_CONTROLLER_FOUND;
	}
	return ret;
}

//Filter packet da[] = {0x01, 0x00, 0x5e}
//add another filter for bcast, {0xff, 0xff, 0xff, 0xff}
#define MASK_SIZE 3
void filter_add_enable(){
	u8 mask[MASK_SIZE]={0xFF,0xFF,0xFF};
	u8 pattern[MASK_SIZE]={0x01,0x00,0x5e};
	u8 pattern_bcast[MASK_SIZE]={0xff,0xff,0xff};
	
	rtw_packet_filter_pattern_t packet_filter;
	rtw_packet_filter_pattern_t packet_filter_bcast;
	rtw_packet_filter_rule_e rule;

	packet_filter.offset = 0;
	packet_filter.mask_size = 3;
	packet_filter.mask = mask;
	packet_filter.pattern = pattern;
	
	packet_filter_bcast.offset = 0;
	packet_filter_bcast.mask_size = 3;
	packet_filter_bcast.mask = mask;
	packet_filter_bcast.pattern = pattern_bcast;
	
	rule = RTW_POSITIVE_MATCHING;

	wifi_init_packet_filter();
	wifi_add_packet_filter(1, &packet_filter,rule);
	wifi_add_packet_filter(2, &packet_filter_bcast,rule);
		
	wifi_enable_packet_filter(1);
	wifi_enable_packet_filter(2);
}

void remove_filter(){
	wifi_disable_packet_filter(1);
	wifi_disable_packet_filter(2);
	wifi_remove_packet_filter(1);
	wifi_remove_packet_filter(2);
}

void print_simple_config_result(enum sc_result sc_code)
{
	printf("\r\n");
	switch (sc_code) {
	case SC_NO_CONTROLLER_FOUND:
		printf("Simple Config timeout!! Can't get Ap profile. Please try again\n"); 
	break;
	case SC_CONTROLLER_INFO_PARSE_FAIL:
		printf("Simple Config fail, cannot parse target ap info from controller\n");
	break;
	case SC_TARGET_CHANNEL_SCAN_FAIL:
		printf("Simple Config cannot scan the target channel\n");
	break;
	case SC_JOIN_BSS_FAIL:
		printf("Simple Config Join bss failed\n");
	break;
	case SC_DHCP_FAIL:
		printf("Simple Config fail, cannot get dhcp ip address\n");
	break;
	case SC_UDP_SOCKET_CREATE_FAIL:
		printf("Simple Config Ack socket create fail!!!\n");
	break;
	case SC_TERMINATE:
		printf("Simple Config terminate\n");
	break;
	case SC_SUCCESS:
		printf("Simple Config success\n");
	break;

	case SC_ERROR:
	default:
		printf("unknown error when simple config!\n");

	}
}

#endif //CONFIG_INCLUDE_SIMPLE_CONFIG

void cmd_simple_config(int argc, char **argv){
#if CONFIG_INCLUDE_SIMPLE_CONFIG
	char *custom_pin_code = NULL;
	enum sc_result ret = SC_ERROR;

	if(argc > 2){
		printf("\n\rInput Error!");
	}

	if(argc == 2)
		custom_pin_code = (argv[1]);
	
	simple_config_terminate = 0;
	rtw_join_status |= JOIN_SIMPLE_CONFIG;

	wifi_enter_promisc_mode();
	if(init_test_data(custom_pin_code) == 0){
		filter_add_enable();
		ret = simple_config_test(NULL);
		deinit_test_data();
		print_simple_config_result(ret);
		remove_filter();
	}
#if CONFIG_INIC_CMD_RSP
	if(ret != SC_SUCCESS)
		inic_c2h_wifi_info("ATWQ", RTW_ERROR);
#endif

#if CONFIG_EXAMPLE_UART_ATCMD
	if(ret == SC_SUCCESS){
		at_printf("\n\r[ATWQ] OK");
	}else{
		at_printf("\n\r[ATWQ] ERROR:%d",ret);
	}
#endif

#endif	
}
#endif	//#if CONFIG_WLAN
