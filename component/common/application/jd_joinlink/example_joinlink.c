/*******************************example_joinlink **************************/

#include "autoconf.h"
#include "platform_stdlib.h"
#include "wifi_conf.h"
#include "wifi_structures.h"
#include "osdep_service.h"
#include "lwip_netconf.h"
#include "task.h"
#include "joinlink.h"
#include "cJSON.h"

#include <lwip/sockets.h>
#include <lwip/raw.h>
#include <lwip/icmp.h>
#include <lwip/inet_chksum.h>
#include <platform/platform_stdlib.h>

#define MASK_SIZE_JOINLINK  3
#define SOURCE_PORT         101

//gloable 
static unsigned char cur_channel = 1;                                    
static unsigned char lock_channel = 1;
static _timer timer_handler_phase2;
static _timer timer_handler_phase1;
static u8 joinlink_finished = 0;
static u8 security_type = 0xff;
static u8 jl_rx_flag = 0;
static rtw_scan_result_t *all_channel_scan_result = NULL;
static rtw_scan_result_t *p_result = NULL;
static int all_channel_ret = 0;
static int phase1_finished = 0;
static int phase2_started = 0;
static u32 start_time = 0;
static u32 current_time = 0;
static int idx = 1;
static int phase1_scanned_channel[14];
static char ap_bssid[6];
static char aes_key[] = "123456789";

static void* pre_scan_sema;
static int ack_socket;
static struct sockaddr_in to_addr;
static struct sockaddr_in from_addr;
static char header_cmd[] = "cmd";
static cJSON *ack_content = NULL;
extern struct netif xnetif[];

void example_joinlink(void);

static rtw_result_t joinlink_scan_result_handler(rtw_scan_handler_result_t* malloced_scan_result )
{

	static int ApNum = 0;
  //TODO: add timer of 2s, wf, 1021
	if (malloced_scan_result->scan_complete != RTW_TRUE) {
		rtw_scan_result_t* record = &malloced_scan_result->ap_details;
		record->SSID.val[record->SSID.len] = 0; /* Ensure the SSID is null terminated */
		++ApNum;
		
		if(malloced_scan_result->user_data)
			memcpy((void *)((char *)malloced_scan_result->user_data+(ApNum-1)*sizeof(rtw_scan_result_t)), (char *)record, sizeof(rtw_scan_result_t));
	}
	// scan finished, wf, 1022 
	else
	{
		rtw_up_sema(&pre_scan_sema);
		ApNum = 0;
	}
	return RTW_SUCCESS;
}

void* joinlink_all_scan()
{      
	int ret = 0;
	rtw_scan_result_t *joinlink_scan_buf = NULL; 
	
	if(joinlink_scan_buf != NULL)
		free(joinlink_scan_buf);
		
	joinlink_scan_buf = (rtw_scan_result_t *)malloc(65*sizeof(rtw_scan_result_t));
	if(joinlink_scan_buf == NULL){
		return 0;
	}
  memset(joinlink_scan_buf, 0, 65*sizeof(rtw_scan_result_t));
  
	if((ret = wifi_scan_networks(joinlink_scan_result_handler, joinlink_scan_buf)) != RTW_SUCCESS){
		printf("[ATWS]ERROR: wifi scan failed\n\r");
		free(joinlink_scan_buf);
		return 0;
	}	
	return joinlink_scan_buf;
}

void joinlink_deinit_content()
{
	rtw_del_timer(&timer_handler_phase2);
	rtw_del_timer(&timer_handler_phase1);
	if(all_channel_scan_result)
	{
		free(all_channel_scan_result);
		all_channel_scan_result = NULL;
	}
	rtw_free_sema(&pre_scan_sema);
	joinlink_deinit();
	
	return;
}
static char *jl_itoa(int value)
{
	char *val_str;
	int tmp = value, len = 1;

	while((tmp /= 10) > 0)
		len ++;

	val_str = (char *) malloc(len + 1);
	sprintf(val_str, "%d", value);

	return val_str;
}

static void get_ip_str(int *ip_int, char *ip_ch)
{ 
	char *ip_single = NULL;
	u8 pos = 0, len = 0;
	
	for(int i = 0; i < 4; i++)
	{
		ip_single = jl_itoa(ip_int[i]);
		len = strlen(ip_single);
		memcpy(ip_ch + pos, ip_single,len);
		free(ip_single);
		ip_single = NULL;
		pos += len;
		if(i == 3) 
		{
			*(ip_ch + pos) = 0;
			break;	
		}
		*(ip_ch + pos) = '.';
		pos++;
	} 
}

static int joinlink_set_ack_content(u8 check_sum)
{
	cJSON_Hooks memoryHook;
	memoryHook.malloc_fn = malloc;
	memoryHook.free_fn = free;
	cJSON_InitHooks(&memoryHook);
	
	if(ack_content != NULL)
	{
		cJSON_Delete(ack_content);
		ack_content = NULL;
	}
	if((ack_content = cJSON_CreateObject()) != NULL)
	{
		char mac_str[18];
		u8 pos = 0;
		memset(mac_str, 0, sizeof(mac_str));
		for(int i = 0; i < 6; i++)
		{
			sprintf(mac_str + pos, "%02x", xnetif[0].hwaddr[i]);
			pos += 2;
			if(i != 5)
				mac_str[pos++] = ':';
		}
		
		cJSON_AddItemToObject(ack_content, "deviceid", cJSON_CreateString(mac_str));
		cJSON_AddItemToObject(ack_content, "code", cJSON_CreateNumber(check_sum));
	}
	else
	{
		printf("create jSON object failure\n");
		return -1;
	}
		
  return 0;
}

#if 1

static void recv_cmd(void *para)
{
	int rev_len = 0;
	char pkt_buf[16];
	while(1)
	{
		vTaskDelay(500);
		if((rev_len = recvfrom(ack_socket, pkt_buf, sizeof(pkt_buf), 0, NULL, NULL)) >= 0)
		{
			if(memcmp(pkt_buf, header_cmd, sizeof(header_cmd)) == 0)
			{
				printf("received reboot command, restart join_link process\n");
				// do we need to reboot?
				example_joinlink();
				close(ack_socket);
				break;
			}
		}		
	}
	vTaskDelete(NULL);
	return;
}

static int send_ack(int *dest_ip, int dest_port, u8 sum)
{
#if CONFIG_LWIP_LAYER
	int ack_transmit_round;
	char ip[16];
	char *jsonString = NULL;
	int sended_data = 0;

	if(joinlink_set_ack_content(sum) == -1)
		return -1;
		
	jsonString = cJSON_Print(ack_content);
	if(jsonString == NULL)
	{
		printf("json string convert failure\n");
		cJSON_Delete(ack_content);
		return -1;
	}
	
	get_ip_str(dest_ip, ip);
	
	ack_socket = socket(PF_INET, SOCK_DGRAM, IP_PROTO_UDP);
	if (ack_socket == -1) {
		printf("create socket failure\n");
		return 	-1;
	}
	
	FD_ZERO(&to_addr);
	to_addr.sin_family = AF_INET;
	to_addr.sin_port = htons(dest_port);
	to_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	
	FD_ZERO(&from_addr);
	from_addr.sin_family = AF_INET;
	from_addr.sin_port = htons(SOURCE_PORT);
	to_addr.sin_addr.s_addr = inet_addr(ip);
	
	if(bind(ack_socket, (struct sockaddr *)&from_addr, sizeof(from_addr)) < 0)
	{
		printf("bind to source port error\n");
		return -1;
	}

	for (ack_transmit_round = 0; ack_transmit_round < 5; ack_transmit_round++) {
		sended_data = sendto(ack_socket, (unsigned char *)jsonString, strlen(jsonString), 0, (struct sockaddr *) &to_addr, sizeof(struct sockaddr));
		//printf("\r\nAlready send %d bytes data\n", sended_data);
		vTaskDelay(100);	/* delay 100 ms */		
	}
	
	if(xTaskCreate(recv_cmd, (char const *)"recv_cmd", 1512, NULL, tskIDLE_PRIORITY + 2, NULL) != pdPASS)
		printf("%s xTaskCreate failed\n", __FUNCTION__);
		
	close(ack_socket);
	if(jsonString) 
	{
		free(jsonString);
		jsonString = NULL;
	}
	cJSON_Delete(ack_content);
#endif

	return 0;	
}
#endif
static void remove_filter()
{
	wifi_disable_packet_filter(1);
	wifi_disable_packet_filter(2);
	wifi_remove_packet_filter(1);
	wifi_remove_packet_filter(2);
}

int joinlink_finish(unsigned char security_type)
{
	int ret = 0;
	int retry = 3;
	unsigned char pscan_config = 1;
	joinlink_result_t result;
	rtw_security_t security_mode;
	
	wifi_set_promisc(RTW_PROMISC_DISABLE,NULL,0);
	remove_filter();
	
	pscan_config = PSCAN_ENABLE | PSCAN_SIMPLE_CONFIG;
	ret = joinlink_get_result(&result);
	if (ret == 0) {
		printf("get result OK\n");
		//printf("\r\n joinlink get result ok,ssid = %s, pwd = %s,ssid length = %d,pwd length = %d",
		//	       result.ssid, result.pwd, result.ssid_length,result.pwd_length);
	}
	else{
		printf("joinlink result not get!\n");
		joinlink_deinit_content();
		return -1;
	}
	//ap security type
	switch(security_type){
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
		printf("unknow security mode,connect fail!\n");
	}

#if 1
	while(1){
		if(wifi_set_pscan_chan(&lock_channel, &pscan_config, 1) < 0){
			printf("ERROR: wifi set partial scan channel fail\n");
			break;
		}
		printf("wifi_connect\n");
		//printf("ap_bssid: 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n", ap_bssid[0],ap_bssid[1],ap_bssid[2],ap_bssid[3],ap_bssid[4],ap_bssid[5]);


		ret = wifi_connect((unsigned char *)result.ssid, security_mode, 
				   (unsigned char *)result.pwd, result.ssid_length, 
				   result.pwd_length, 
				   0,NULL);			   
				   
		if(ret == RTW_SUCCESS){
			printf("Connect ok!\n");
#if CONFIG_LWIP_LAYER
			/* If not rise priority, LwIP DHCP may timeout */
			vTaskPrioritySet(NULL, tskIDLE_PRIORITY + 3);	
			/* Start DHCP Client */
			ret = LwIP_DHCP(0, DHCP_START);
			vTaskPrioritySet(NULL, tskIDLE_PRIORITY + 1);	
#endif
			break;
		}
	  if (retry == 0) {
			break;
		}
		
		retry--;
	}	
	if(send_ack(result.source_ip, result.source_port, result.sum) != 0)
		printf("send ack failure\n");
#endif	
	
	joinlink_deinit_content();
	return 0;
	
}
// handler for phase2
void timer_handler_phase2_func(void *FunctionContext)
{
	// do not switch channel while handle frames, wf, 1021
	if(jl_rx_flag){
		rtw_set_timer(&timer_handler_phase2, CHANNEL_SWITCH_TIME - 25);
	} else {
		if(cur_channel >= 13)
			{cur_channel = 1;}
		else
			cur_channel ++;
		wifi_set_channel(cur_channel);
		rtw_set_timer(&timer_handler_phase2, CHANNEL_SWITCH_TIME);
	}
	
	//printf("phase2:wifi switch channel to %d\n",cur_channel);
	return;
}

// timer handler for the 1st phase, wf, 1022
void timer_handler_phase1_func(void *FunctionContext)
{
	// do not switch channel while handle frames, wf, 1021
	if(jl_rx_flag){
		rtw_set_timer(&timer_handler_phase1, SSID_SWITCH_TIME - 25);
	}
	// switch ssid, wf, 1022 
	else 
	{
		if(idx >= 14)
    {
    	phase1_finished = 1;
    	printf("wifi: phase1 scan finished\n");
    	printf("wifi: start phase2 scan\n");
// move from pkt handler to here in case no pkt to trigue phase2
#if 1
		if(phase1_finished)
		{
			phase1_finished = 0;
			phase2_started = 1;
			rtw_cancel_timer(&timer_handler_phase1);
			//start phase2 for ch1~ch13
			cur_channel = 1;	
			wifi_set_channel(cur_channel);
			
		  rtw_init_timer(&timer_handler_phase2, NULL, &timer_handler_phase2_func, NULL, "phase_2");
		  rtw_set_timer(&timer_handler_phase2, CHANNEL_SWITCH_TIME);					
		}
#endif
    	return;
    }
    
    while(idx < 14)
    {
      if(phase1_scanned_channel[idx])
      {
      	wifi_set_channel(idx);
   			rtw_set_timer(&timer_handler_phase1, SSID_SWITCH_TIME);
      	//printf("phase1: wifi switch channel to %d\n",idx);
      	idx++;
      	break;      	
      }
      else
      {
      	if(idx == 13)
      		rtw_set_timer(&timer_handler_phase1, SSID_SWITCH_TIME);
      	idx++;
      }

    }

	}
	return;
}

static void rtl_frame_recv_handle(unsigned char *buf, int len, unsigned char *da, unsigned char *sa, void *user_data) {
	
	int ret = 0;
	int fixed_channel;
	char scanned_ssid[50] = {0};
	unsigned char *current_bssid = NULL;
	int scanned_ssid_len = 0;
	
	//set this flag prevent joinlink_recv interruptted by timer,since timer has higher priority
	jl_rx_flag = 1;
	if (joinlink_finished) {
		jl_rx_flag = 0;
		return;
	}

	ret = joinlink_recv(da, sa, len, user_data);
	if(ret == JOINLINK_STATUS_CHANNEL_LOCKED)
	{
		if(phase2_started)
		{
			phase2_started = 0;
      rtw_cancel_timer(&timer_handler_phase2);			
		}
		else
			rtw_cancel_timer(&timer_handler_phase1);
			  
		lock_channel = cur_channel;
		security_type = ((ieee80211_frame_info_t *)user_data)->encrypt;
		printf("JoinLink locked to channel[%d]\n",lock_channel);
		
		current_bssid = buf + 4 + ETH_ALEN;
		memcpy(ap_bssid, current_bssid, 6);
		
		fixed_channel = promisc_get_fixed_channel(current_bssid, scanned_ssid, &scanned_ssid_len);
		if (fixed_channel != 0) {
			printf("JoinLink force fixed to channel[%d]\r\n",fixed_channel);
			printf("JoinLink ssid scanned[%s]\r\n",scanned_ssid);
			wifi_set_channel(fixed_channel);
		}
		
	}
	else if(ret == JOINLINK_STATUS_COMPLETE){
		//wifi_set_promisc(RTW_PROMISC_DISABLE,NULL,0);
		joinlink_finished = 1;
		printf("quit promisc mode!\r\n");
	} 
	//release flag
	jl_rx_flag = 0;
	
	return;
}


// callback for promisc packets, like rtk_start_parse_packet in SC, wf, 1021
void wifi_promisc_rx(unsigned char* buf, unsigned int len, void* user_data)
{
	unsigned char * da = buf;
	unsigned char * sa = buf + ETH_ALEN;
	
	if (joinlink_finished)
		return;
	
	rtl_frame_recv_handle(buf, len, da, sa, user_data);
	return;
	
}

// the entry point for joinlink
void joinlink_process(void *param)
{
	 
	while(1)
	{
		current_time = xTaskGetTickCount();
		
		if(joinlink_finished)
		{
			printf("joinlink finished\n");
			break;
		}
		
		if((current_time - start_time) > JOINLINK_TIME * configTICK_RATE_HZ)
		{
			printf("joinlink timeout\n");
			break;
		}
		
		vTaskDelay(500);
	}

	joinlink_finish(security_type);
		
	vTaskDelete(NULL);
	return;
}

int joinlink_init_content()
{
	
	int ret = 0;
	ret = joinlink_init();
	if(ret < 0){
		printf("JoinLink init failed!\n");
		return ret;
	}
	memset(phase1_scanned_channel, 0, sizeof(phase1_scanned_channel));
  security_type = 0xff;
  cur_channel = 1;
  lock_channel = 1;
  joinlink_finished = 0;
  jl_rx_flag = 0;
  p_result = NULL;
  all_channel_ret = 0;
  phase1_finished = 0;
  phase2_started = 0;
  idx = 1;
  rtw_init_sema(&pre_scan_sema, 0);
  memset(ap_bssid, 0, sizeof(ap_bssid));
  set_aes_key(aes_key, sizeof(aes_key) - 1);
	
	return 0;
}

// ret:1 indicate suc, else fail
int scan_all_channel()
{
	all_channel_scan_result = (rtw_scan_result_t *)joinlink_all_scan();

	if(all_channel_scan_result == NULL)
		return 0;
	else
		return 1;	
			
}

void get_phase1_channel()
{
	p_result = all_channel_scan_result;
	while(p_result->channel)
	{
		if((p_result->channel >= 1) && (p_result->channel <= 13))
			phase1_scanned_channel[p_result->channel] = 1;
		p_result++;
	}
  return;
}

// now only accept mcast and bcast pkt
static void filter_add_enable()
{
	u8 mask[MASK_SIZE_JOINLINK]={0xFF,0xFF,0xFF};
	u8 pattern[MASK_SIZE_JOINLINK]={0x01,0x00,0x5e};
	u8 pattern_bcast[MASK_SIZE_JOINLINK]={0xff,0xff,0xff};
	
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


void joinlink_start(void *param)
{
	joinlink_finished = 0;
	start_time = xTaskGetTickCount();
	
	if(xTaskCreate(joinlink_process, (char const *)"JoinLink", 1512, NULL, tskIDLE_PRIORITY + 2, NULL) != pdPASS)
		printf("%s xTaskCreate failed\n", __FUNCTION__);
	
	if (joinlink_init_content() < 0)
		printf("joinlink init fail!\n");
	while(1)
	{
		if(wifi_is_ready_to_transceive(RTW_STA_INTERFACE) == RTW_SUCCESS)
			break;
		else
			vTaskDelay(3000);
	}
  all_channel_ret = scan_all_channel(); 
  
	if (rtw_down_sema(&pre_scan_sema) == _FAIL)
	  printf("%s, Take Semaphore Fail\n", __FUNCTION__);
    
  //printf("\npre scan finished\n");

	//set wifi to station mode,enable promisc mode and timer to change channel
	wifi_enter_promisc_mode();
	filter_add_enable();
	
	/* enable all 802.11 packets*/
	wifi_set_promisc(RTW_PROMISC_ENABLE, wifi_promisc_rx, 1);	
		
	//init timer handler,and set timer hanler funcion
	if(all_channel_ret)
	{
		printf("\nstart the phase1 scan\n");
		get_phase1_channel();
		rtw_init_timer(&timer_handler_phase1, NULL, &timer_handler_phase1_func, NULL, "phase1_timer");
		rtw_set_timer(&timer_handler_phase1, SSID_SWITCH_TIME);			
	}
	else
	{
		printf("phase1 scan fail, start phase2 scan\n");
	  rtw_init_timer(&timer_handler_phase2, NULL, &timer_handler_phase2_func, NULL, "phase2_timer");
 	  wifi_set_channel(cur_channel);
	  rtw_set_timer(&timer_handler_phase2, CHANNEL_SWITCH_TIME);	
	}

	vTaskDelete(NULL);
	return;
}

void example_joinlink(void)
{
	if(xTaskCreate(joinlink_start, (char const *)"JoinLink_entry", 1512, NULL, tskIDLE_PRIORITY + 2, NULL) != pdPASS)
		printf("%s xTaskCreate failed\n", __FUNCTION__);	
}