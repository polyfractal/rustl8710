#include <platform/platform_stdlib.h>
#include <platform_opts.h>

#include <stdio.h>
#include "log_service.h"
#include "atcmd_wifi.h"
#include "atcmd_lwip.h"
#include "osdep_service.h"
#include "osdep_api.h"

//#define MAX_BUFFER 	256
#define MAX_BUFFER 	(LOG_SERVICE_BUFLEN)
#define ATCP_STACK_SIZE		512//2048

extern char log_buf[LOG_SERVICE_BUFLEN];
extern struct netif xnetif[NET_IF_NUM]; 

static unsigned char tx_buffer[MAX_BUFFER];
static unsigned char rx_buffer[MAX_BUFFER];

#if ATCMD_VER == ATVER_2 
node node_pool[NUM_NS];

node* mainlist;

static int atcmd_lwip_auto_recv = FALSE;
volatile int atcmd_lwip_tt_mode = FALSE; //transparent transmission mode
xTaskHandle atcmd_lwip_tt_task = NULL;
xSemaphoreHandle atcmd_lwip_tt_sema = NULL;
volatile int atcmd_lwip_tt_datasize = 0;
volatile int atcmd_lwip_tt_lasttickcnt = 0;

#ifdef ERRNO
_WEAK int errno = 0; //LWIP errno
#endif

static void atcmd_lwip_receive_task(void *param);
int atcmd_lwip_start_autorecv_task(void);
int atcmd_lwip_is_autorecv_mode(void);
void atcmd_lwip_set_autorecv_mode(int enable);
int atcmd_lwip_start_tt_task(void);
int atcmd_lwip_is_tt_mode(void);
void atcmd_lwip_set_tt_mode(int enable);
int atcmd_lwip_write_info_to_flash(struct atcmd_lwip_conn_info *cur_conn, int enable);
#else //#if ATCMD_VER == ATVER_2 

xTaskHandle server_task = NULL;
xTaskHandle client_task = NULL;

static int mode = 0;
static int local_port;
static int remote_port;
static char remote_addr[16];
static int packet_size;

static int sockfd, newsockfd;
static socklen_t client;
static struct sockaddr_in serv_addr, cli_addr;
static int opt = 1;

static int type; //TCP SERVER:1, TCP CLIENT: 2, UDP SERVER:3, UDP CLIENT: 4

static void init_transport_struct(void)
{
	mode = 0;
	local_port = 0;
	remote_port = 0;
	sockfd = -1;
	newsockfd = -1;
	packet_size = 0;
        _memset(remote_addr, 0, sizeof(remote_addr));
	_memset(&client, 0, sizeof(client));
	_memset(&serv_addr, 0, sizeof(serv_addr));
	
}

void socket_close(void)
{
	close(sockfd);
	if(server_task != NULL)
	{
		vTaskDelete(server_task);
		server_task = NULL;
	}
	if(client_task != NULL)
	{
		vTaskDelete(client_task);
		client_task = NULL;
	}
	type = 0;
	init_transport_struct();
}
#endif //#if ATCMD_VER == ATVER_2 

static void server_start(void *param)
{
	int s_mode;
	int s_sockfd, s_newsockfd;
	socklen_t s_client;
	struct sockaddr_in s_serv_addr, s_cli_addr;
	int s_local_port;
	int error_no = 0;
	int s_opt = 1;
#if ATCMD_VER == ATVER_2 
	node* ServerNodeUsed = (node*)param;
	if(ServerNodeUsed){
		s_mode = ServerNodeUsed->protocol;
		s_local_port = ServerNodeUsed->port;
	}
//	else
//#endif
#else
	{
		s_mode = mode;
		s_local_port = local_port;
		s_opt = opt;
	}
#endif

	if(s_mode == NODE_MODE_UDP)
		s_sockfd = socket(AF_INET,SOCK_DGRAM,0);
	else
		s_sockfd = socket(AF_INET, SOCK_STREAM, 0);
	
	if (s_sockfd == INVALID_SOCKET_ID) {
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,"ERROR opening socket");
		error_no = 5;
		goto err_exit;
	}

	if((setsockopt(s_sockfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&s_opt, sizeof(s_opt))) < 0){
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,"ERROR on setting socket option");
		error_no = 6;
		goto err_exit;
	}
	
	memset((char *)&s_serv_addr, 0, sizeof(s_serv_addr));
	s_serv_addr.sin_family = AF_INET;
	s_serv_addr.sin_addr.s_addr = INADDR_ANY;
	s_serv_addr.sin_port = htons(s_local_port);

	if (bind(s_sockfd, (struct sockaddr *)&s_serv_addr,sizeof(s_serv_addr)) < 0) {
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,"ERROR on binding");
		error_no = 7;
		goto err_exit;
	}

#if ATCMD_VER == ATVER_2 
	if(ServerNodeUsed != NULL) {
		uint8_t *ip = (uint8_t *)LwIP_GetIP(&xnetif[0]);
		ServerNodeUsed->sockfd = s_sockfd;
		ServerNodeUsed->addr = ntohl(*((u32_t *)ip));
	}
//	else
//#endif
#else
	{
		sockfd = s_sockfd;
		memcpy(&serv_addr, &s_serv_addr, sizeof(s_serv_addr));
	}
#endif

	if (s_mode == NODE_MODE_TCP){//TCP MODE
		if(listen(s_sockfd , 5) < 0){
			AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,"ERROR on listening");
			error_no = 8;
			goto err_exit;
		}	
#if ATCMD_VER == ATVER_2
		if(param != NULL) {
			if(hang_node(ServerNodeUsed) < 0)
			{
				error_no = 9;
				goto err_exit;
			}else{
				#if CONFIG_LOG_SERVICE_LOCK
				log_service_lock();
				#endif
				at_printf("\r\n[ATPS] OK"
				"\r\n[ATPS] con_id=%d",
				ServerNodeUsed->con_id);
				at_printf(STR_END_OF_ATCMD_RET);
				#if CONFIG_LOG_SERVICE_LOCK
				log_service_unlock();
				#endif
			}
		}
#endif
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,"The TCP SERVER START OK!");
		s_client = sizeof(s_cli_addr);
		while(1){
			if((s_newsockfd = accept(s_sockfd,(struct sockaddr *) &s_cli_addr,&s_client)) < 0){
#if ATCMD_VER == ATVER_2
				if(param != NULL) {
					AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
						"[ATPS] ERROR:ERROR on accept");
				}
//				else
//#endif
#else
				{
					AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
					"ERROR on accept");
				}
#endif
				error_no = 10;
				goto err_exit;
			}
			else{
#if ATCMD_VER == ATVER_2
				if(param != NULL) {
					node* seednode = create_node(s_mode, NODE_ROLE_SEED);
					if(seednode == NULL){
						AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
							"[ATPS]create node failed!");
						error_no = 11;
						goto err_exit;
					}
					seednode->sockfd = s_newsockfd;
					seednode->port = ntohs(s_cli_addr.sin_port);
					seednode->addr = ntohl(s_cli_addr.sin_addr.s_addr);
					if(hang_seednode(ServerNodeUsed,seednode) < 0){
						delete_node(seednode);
						seednode = NULL;
					}
					else{
						#if CONFIG_LOG_SERVICE_LOCK
						log_service_lock();
						#endif
						at_printf("\r\n[ATPS] A client connected to server[%d]\r\n"
							"con_id:%d,"
							"seed,"
							"tcp,"
							"address:%s,"
							"port:%d,"
							"socket:%d", 
							ServerNodeUsed->con_id,
							seednode->con_id,
							inet_ntoa(s_cli_addr.sin_addr.s_addr),
							ntohs(s_cli_addr.sin_port),
							seednode->sockfd
							);
						at_printf(STR_END_OF_ATCMD_RET);
						#if CONFIG_LOG_SERVICE_LOCK
						log_service_unlock();
						#endif
					}
				}
//				else
//#endif
#else
				{
					newsockfd = s_newsockfd;
					memcpy(&cli_addr, &s_cli_addr, sizeof(cli_addr));
					client = s_client;
					AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS,
						"A client connected to this server :\r\n[PORT]: %d\r\n[IP]:%s", 
						ntohs(cli_addr.sin_port), inet_ntoa(cli_addr.sin_addr.s_addr));
				}
#endif
			}
		}
	}
	else{
#if ATCMD_VER == ATVER_2
		if(ServerNodeUsed != NULL) {
			if(hang_node(ServerNodeUsed) < 0){
				error_no = 12;
				goto err_exit;
			}
			#if CONFIG_LOG_SERVICE_LOCK
			log_service_lock();
			#endif
			at_printf("\r\n[ATPS] OK"
				"\r\n[ATPS] con_id=%d",
				ServerNodeUsed->con_id);
			at_printf(STR_END_OF_ATCMD_RET);
			#if CONFIG_LOG_SERVICE_LOCK
			log_service_unlock();
			#endif
			//task will exit itself
			ServerNodeUsed->handletask = NULL;
		}
#endif
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS,"The UDP SERVER START OK!");
	}
	goto exit;
err_exit:
#if ATCMD_VER == ATVER_2 
	if(ServerNodeUsed){
		//task will exit itself if getting here
		ServerNodeUsed->handletask = NULL;
		delete_node(ServerNodeUsed);
	}
	//else
//#endif
#else
	{
		socket_close();
	}
#endif
	#if CONFIG_LOG_SERVICE_LOCK
	log_service_lock();
	#endif
	at_printf("\r\n[ATPS] ERROR:%d", error_no);
	at_printf(STR_END_OF_ATCMD_RET);
	#if CONFIG_LOG_SERVICE_LOCK
	log_service_unlock();
	#endif
exit:
	return;
}

static void client_start(void *param)
{
	int c_mode;
	int c_remote_port;
	char c_remote_addr[16];
	int c_sockfd;
	struct sockaddr_in c_serv_addr;
	int error_no = 0;
#if ATCMD_VER == ATVER_2
	struct in_addr c_addr;
	node * ClientNodeUsed = (node *)param;
	if(ClientNodeUsed){
		c_mode = ClientNodeUsed->protocol;
		c_remote_port = ClientNodeUsed->port;
		c_addr.s_addr = htonl(ClientNodeUsed->addr);
		if(inet_ntoa_r(c_addr, c_remote_addr, sizeof(c_remote_addr))==NULL){
			error_no = 6;
			goto err_exit;
		}
	}
	//else
//#endif
#else
	{
		c_mode = mode;
		c_remote_port = remote_port;
		memcpy(c_remote_addr, remote_addr, 16*sizeof(char));	
	}
#endif
	if(c_mode == NODE_MODE_UDP)
		c_sockfd = socket(AF_INET,SOCK_DGRAM,0);
	else
		c_sockfd = socket(AF_INET, SOCK_STREAM, 0);
	
	if (c_sockfd == INVALID_SOCKET_ID) {
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,"Failed to create sock_fd!");
		error_no = 7;
		goto err_exit;
	}
	AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS,"OK to create sock_fd!");
	memset(&c_serv_addr, 0, sizeof(c_serv_addr));
	c_serv_addr.sin_family = AF_INET;
	c_serv_addr.sin_addr.s_addr = inet_addr(c_remote_addr);
	c_serv_addr.sin_port = htons(c_remote_port);

#if ATCMD_VER == ATVER_2
	if(ClientNodeUsed){
		ClientNodeUsed->sockfd = c_sockfd;
	}
	//else
//#endif
#else
	{
		sockfd = c_sockfd;
		memcpy(&serv_addr, &c_serv_addr, sizeof(c_serv_addr));
	}
#endif
	if (c_mode == NODE_MODE_TCP){//TCP MODE
		if(connect(c_sockfd, (struct sockaddr *)&c_serv_addr,  sizeof(c_serv_addr)) == 0){
			AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS,"Connect to Server successful!");
#if ATCMD_VER == ATVER_2
			if(ClientNodeUsed != NULL) {
				if(hang_node(ClientNodeUsed) < 0){
					error_no = 8;
					goto err_exit;
				}
				#if CONFIG_LOG_SERVICE_LOCK
				log_service_lock();
				#endif
				at_printf("\r\n[ATPC] OK\r\n[ATPC] con_id=%d",ClientNodeUsed->con_id);
				at_printf(STR_END_OF_ATCMD_RET);
				#if CONFIG_LOG_SERVICE_LOCK
				log_service_unlock();
				#endif
			}
#endif
			}else{
#if ATCMD_VER == ATVER_2
				if(ClientNodeUsed != NULL) {
					AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,"[ATPC] ERROR:Connect to Server failed!");
				}
				//else
//#endif
#else
				{
					AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,"Connect to Server failed!");
				}
#endif
				error_no = 9;
				goto err_exit;
		}
	}
	else{
#if ATCMD_VER == ATVER_2
		if(ClientNodeUsed != NULL) {
			if(ClientNodeUsed->local_port){
				struct sockaddr_in addr;  
				memset(&addr, 0, sizeof(addr));  
				addr.sin_family=AF_INET;  
				addr.sin_port=htons(ClientNodeUsed->local_port);  
				addr.sin_addr.s_addr=htonl(INADDR_ANY) ;  
				if (bind(ClientNodeUsed->sockfd, (struct sockaddr *)&addr, sizeof(addr))<0) {
					AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,"bind sock error!"); 
					error_no = 12;
					goto err_exit;
				}
			}
			if(hang_node(ClientNodeUsed) < 0){
				error_no = 10;
				goto err_exit;
			}
			#if CONFIG_LOG_SERVICE_LOCK
			log_service_lock();
			#endif
			at_printf("\r\n[ATPC] OK\r\n[ATPC] con_id=%d",ClientNodeUsed->con_id);
			at_printf(STR_END_OF_ATCMD_RET);
			#if CONFIG_LOG_SERVICE_LOCK
			log_service_unlock();
			#endif
		}
#endif
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS,"UDP client starts successful!");
	}
	goto exit;
err_exit:
#if ATCMD_VER == ATVER_2 
	if(ClientNodeUsed)
	{
		delete_node(ClientNodeUsed);
	}
	//else
//#endif
#else
	{
		socket_close();
	}
#endif
	#if CONFIG_LOG_SERVICE_LOCK
	log_service_lock();
	#endif
	at_printf("\r\n[ATPC] ERROR:%d", error_no);
	at_printf(STR_END_OF_ATCMD_RET);
	#if CONFIG_LOG_SERVICE_LOCK
	log_service_unlock();
	#endif
exit:
	return;
}

static void client_start_task(void *param)
{
	vTaskDelay(1000);
#if ATCMD_VER == ATVER_2 
	if(param){
		client_start(param);
		vTaskDelete(NULL);
		return;
	}
//#endif
#else
	if(remote_addr == NULL){
		printf("\r\n[ERROR] Please using ATP3 to input an valid remote IP address!\r\n");
		vTaskDelete(client_task);
		client_task = NULL;
	}
	else if(remote_port == 0){
		printf("\r\n[ERROR] Please using ATP4 to input an valid remote PORT!\r\n");
		vTaskDelete(client_task);
		client_task = NULL;
	}
	else{
		printf("\n\r\tStart Client\r\n\t[IP]: %s\r\n\t[PORT]:%d\n\r\n\r", remote_addr, remote_port);
		client_start(param);
	}
#endif
	vTaskDelete(NULL);
}

static void server_start_task(void *param)
{
	vTaskDelay(1000);
#if ATCMD_VER == ATVER_2      
	if(param != NULL){
		server_start(param);
		vTaskDelete(NULL);
		return;
	}
//#endif
#else
	if(local_port == 0){
		printf("\r\n[ERROR] Please using ATP2 to input an valid local PORT!\r\n");
		vTaskDelete(server_task);
		server_task = NULL;
	}
	else{
		uint8_t *ip = (uint8_t *)LwIP_GetIP(&xnetif[0]);
		printf("\n\rStart Server\r\n\t[IP]: %d.%d.%d.%d\r\n\t[PORT]:%d\n\r\n\r", ip[0], ip[1], ip[2], ip[3], local_port);
		server_start(param);
	}
#endif
	vTaskDelete(NULL);
}

//AT Command function
#if ATCMD_VER == ATVER_1
void fATP1(void *arg){
	if(!arg){
		printf("[ATP1]Usage: ATP1=MODE\n\r");
		goto exit;
	}	
	mode = atoi((char*)arg);
	printf("[ATP1]: _AT_TRANSPORT_MODE_ [%d]\n\r", mode);
exit:
	return;
}

void fATP2(void *arg){
	if(!arg){
		printf("[ATP2]Usage: ATP2=LOCAL_PORT\n\r");
		goto exit;
	}
	local_port = atoi((char*)arg);
	printf("[ATP2]: _AT_TRANSPORT_LOCAL_PORT_ [%d]\n\r", local_port);

exit:
	return;
}

void fATP3(void *arg){
	if(!arg){
		printf("[ATP3]Usage: ATP3=REMOTE_IP\n\r");
		goto exit;
	}
	strcpy((char*)remote_addr, (char*)arg);
	printf("[ATP3]: _AT_TRANSPORT_REMOTE_IP_ [%s]\n\r", remote_addr);

exit:
	return;
}

void fATP4(void *arg){
	if(!arg){
		printf("[ATP4]Usage: ATP4=REMOTE_PORT\n\r");
		goto exit;
	}
	remote_port = atoi((char*)arg);
	printf("[ATP4]: _AT_TRANSPORT_REMOTE_PORT_ [%d]\n\r", remote_port);

exit:
	return;
}

void fATP5(void *arg){
	int server_status = 0;
	if(!arg){
		printf("[ATP5]Usage: ATP5=0/1(0:server disable; 1: server enable)\n\r");
		goto exit;
	}
	server_status = atoi((char*)arg);
	printf("[ATP5]: _AT_TRANSPORT_START_SERVER_ [%d]\n\r", server_status);
	if(mode == 0){
		if(server_status == 0){
			socket_close();
			type = 0;
		}
		else if(server_status == 1){
			if(server_task == NULL)
			{
				if(xTaskCreate(server_start_task, ((const char*)"server_start_task"), ATCP_STACK_SIZE, NULL, ATCMD_LWIP_TASK_PRIORITY, &server_task) != pdPASS)
					printf("\r\n[ATP5]ERROR: Create tcp server task failed.\r\n");
			}
			type = 1;
		}
		else
		printf("[ATP5]ERROR: Just support two mode : 0 or 1\n\r");
	}
	else if(mode == 1){
		if(server_status == 0){
			socket_close();
			type = 0;
		}
		else if(server_status == 1){
			if(server_task == NULL)
			{
				if(xTaskCreate(server_start_task, ((const char*)"server_start_task"), ATCP_STACK_SIZE, NULL, ATCMD_LWIP_TASK_PRIORITY, &server_task) != pdPASS)
					printf("\r\n[ATP5]ERROR: Create udp server task failed.\r\n");
			}
			type = 3;
		}
		else
			printf("[ATP5]ERROR: Just support two mode : 0 or 1\n\r");
	}
	else 
		printf("[ATP5]Error: mode(TCP/UDP) can't be empty\n\r");
	
exit:
	return;
}

void fATP6(void *arg){
	int client_status = 0;
	if(!arg){
		printf("[ATP6]Usage: ATP6=0/1(0:Client disable; 1: Client enable)\n\r");
		goto exit;
	}
	client_status = atoi((char*)arg);
	printf("[ATP6]: _AT_TRANSPORT_START_CLIENT_ [%d]\n\r", client_status);
	if(mode == 0){
		if(client_status == 0){
			socket_close();
			type = 0;
		}
		else if(client_status == 1){
			printf("\r\n[ATP6]TCP Client mode will start\r\n");
			if(client_task == NULL)
			{
				if(xTaskCreate(client_start_task, ((const char*)"client_start_task"), ATCP_STACK_SIZE, NULL, ATCMD_LWIP_TASK_PRIORITY, &client_task) != pdPASS)
					printf("\r\n[ATP6]ERROR: Create tcp client task failed.\r\n");
			}
			type = 2;
		}
		else
			printf("[ATP6]ERROR: Just support two mode : 0 or 1\n\r");
	}
	else if(mode == 1){
		if(client_status == 0){
			socket_close();
			type = 0;
		}
		else if(client_status == 1){
			if(client_task == NULL)
			{
				if(xTaskCreate(client_start_task, ((const char*)"client_start_task"), ATCP_STACK_SIZE, NULL, ATCMD_LWIP_TASK_PRIORITY, &client_task) != pdPASS)
					printf("\r\n[ATP6]ERROR: Create udp client task failed.\r\n");
			}
			type = 4;
		}
		else
			printf("[ATP6]ERROR: Just support two mode : 0 or 1\n\r");
	}
	else 
		printf("[ATP6]Error: mode(TCP/UDP) can't be empty\n\r");
	
exit:
	return;
}

void fATPZ(void *arg){
	uint8_t *ip;
	printf("\n\r\nThe current Transport settings:");
	printf("\n\r==============================");
	if(mode == 0)
		printf("\n\r Protocol: TCP");
	else if(mode == 1)
		printf("\n\r Protocol: UDP");
	
	ip = (uint8_t *)LwIP_GetIP(&xnetif[0]);
	printf("\n\r LOCAL_IP  => %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
	printf("\n\r LOCAL_PORT => %d", local_port);
	printf("\n\r REMOTE_IP  => %s", remote_addr);
	printf("\n\r REMOTE_PORT => %d", remote_port);

	printf("\n\r");	
}

void fATR0(void *arg){
	if(packet_size <= 0){
		packet_size = MAX_BUFFER;
		printf("[ATR0]Notice: Didn't set the value of packet_size, will using the MAX_BUFFER: %d\r\n", MAX_BUFFER);
	}
	memset(rx_buffer, 0, MAX_BUFFER);
	if(type == 1){//tcp server
		if((read(newsockfd,rx_buffer,MAX_BUFFER)) > 0)
			printf("[ATR0]Receive the data:%s\r\n with packet_size: %d\r\n", rx_buffer, packet_size);
		else
			printf("[ATR0]ERROR: Failed to receive data!\r\n");
		close(newsockfd);
		newsockfd = -1;
	}
	else{
		if((read(sockfd,rx_buffer,MAX_BUFFER)) > 0)
			printf("[ATR0]Receive the data:%s\r\n with packet_size: %d\r\n", rx_buffer, packet_size);
		else
			printf("[ATR0]ERROR: Failed to receive data!\r\n");
	}
}

void fATR1(void *arg){
	int size;
	if(!arg){
		printf("[ATR1]Usage: ATR1=packet_size(cannot exceed %d)\n\r", MAX_BUFFER);
		goto exit;
	}
	size = atoi((char*)arg);
	printf("[ATR1]: _AT_TRANSPORT_RECEIVE_PACKET_SIZE_ [%d]\n\r", size);
	if(size < 1)
		printf("[ATR1]Error: packet size need be larger than 0!\n\r");
	else if(size > MAX_BUFFER)
		printf("[ATR1]Error: packet size exceeds the MAX_BUFFER value: %d!\n\r", MAX_BUFFER);
	else 
		packet_size = size;
exit:
	return;
}


void fATRA(void *arg){
	if(!arg){
		printf("[ATRA]Usage: ATRA=[data](Data size cannot exceed the MAX_BUFFER SIZE: %d)\n\r", MAX_BUFFER);
		return;
	}

	if(packet_size <= 0){
		packet_size = MAX_BUFFER;
		printf("[ATRA]Notice: Didn't set the value of packet_size, will using the MAX_BUFFER SIZE: %d\r\n", MAX_BUFFER);
	}


	int argc;
	char *argv[MAX_ARGC] = {0};

	if((argc = parse_param(arg, argv)) != 2){
		printf("[ATRA]Usage: ATRA=[data](Data size cannot exceed the MAX_BUFFER SIZE: %d)\n\r", MAX_BUFFER);
		goto exit;
	}
	else 
		printf("[ATRA]: _AT_TRANSPORT_WRITE_DATA_ [%s]\n\r", argv[1]);
	memset(tx_buffer, 0, MAX_BUFFER);
	memcpy(tx_buffer, argv[1], strlen(argv[1]));

	if(type == 1){//tcp server
		if((write(newsockfd,tx_buffer,strlen(tx_buffer))) > 0)
			printf("[ATRA] Sending data:%s\r\n with packet_size:%d\r\n", tx_buffer, packet_size);
		else
			printf("[ATRA]ERROR: Failed to send data\r\n");
		close(newsockfd);
		newsockfd = -1;
	}
	else if(type == 4){//udp client
		int ret = 0;
		ret = sendto(sockfd, tx_buffer, strlen(tx_buffer), 0, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

		printf("\r\nThe value of ret is %d\r\n", ret);
	}
	else if(type == 3)
		printf("\r\nThe UDP Server mode not support Sending data service!\r\n");
	else{
		if((write(sockfd,tx_buffer, strlen(tx_buffer))) > 0)
			printf("[ATRA] Sending data:%s\r\n with packet_size:%d\r\n", tx_buffer, packet_size);
		else
			printf("[ATRA]ERROR: Failed to send data\r\n");
	}
exit:
	return;
}

void fATRB(void *arg){
	int size;
	if(!arg){
		printf("[ATRB]Usage: ATRB=packet_size(cannot exceed %d)\n\r", MAX_BUFFER);
		goto exit;
	}
	size = atoi((char*)arg);
	printf("[ATRB]: _AT_TRANSPORT_WRITE_PACKET_SIZE_ [%d]\n\r", size);
	if(size < 1)
		printf("[ATRB]Error: packet size need be larger than 0!\n\r");
	else if(size > MAX_BUFFER)
		printf("[ATRB]Error: packet size exceeds the MAX_BUFFER value: %d!\n\r", MAX_BUFFER);
	else 
		packet_size = size;
exit:
	return;
}
#endif
#if ATCMD_VER == ATVER_2
void fATP0(void *arg){
	AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS, 
		"[ATP0]: _AT_TRANSPORT_ERRNO");
#ifdef ERRNO
	at_printf("\r\n[ATP0] OK:%d", errno);
#else
	AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,"errno isn't enabled");
	at_printf("\r\n[ATP0] ERROR");
#endif
}

void fATPC(void *arg){

	int argc;
	char* argv[MAX_ARGC] = {0};
	node* clientnode = NULL;
	int mode = 0;
	int remote_port;
	int local_port = 0;
	//char remote_addr[DNS_MAX_NAME_LENGTH];
	struct in_addr addr;
	int error_no = 0;
#if LWIP_DNS  
	struct hostent *server_host;
#endif

	AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS, 
		"[ATPC]: _AT_TRANSPORT_START_CLIENT");

	if(atcmd_lwip_is_tt_mode() && mainlist->next){
		error_no = 13;
		goto err_exit;
	}
	
	argc = parse_param(arg, argv);
	if(argc < 4){
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
			"[ATPC] Usage: ATPC=<TCP:0/UDP:1>,<REMOTE_IP>,<REMOTE_Port(1~65535)>,[<LOCAL_PORT>]");
		error_no = 1;
		goto err_exit;
	}

	mode = atoi((char*)argv[1]);//tcp or udp
	//strcpy((char*)remote_addr, (char*)argv[2]);
	
	remote_port = atoi((char*)argv[3]);
	if (inet_aton(argv[2], &addr) == 0) 
	{
#if LWIP_DNS  
		server_host = gethostbyname(argv[2]);
		if (server_host){
			memcpy(&addr, server_host->h_addr, 4);
			AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS,
				"[ATPC] Found name '%s' = %s", 
				argv[2],
				inet_ntoa(addr)
			);
		}
		else
#endif
		{
			AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
				"[ATPC] ERROR: Host '%s' not found.", argv[2]);
			error_no = 2;
			goto err_exit; 
		}
	}
	
	if(remote_port < 0 || remote_port > 65535)    {
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
			"[ATPC] ERROR: remote port invalid");
		error_no = 3;
		goto err_exit;
	}

	if(argv[4]){
		local_port = atoi((char*)argv[4]);
		if(local_port < 0 || local_port > 65535)    {
			AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
				"[ATPC] ERROR: local port invalid");
			error_no = 11;
			goto err_exit;
		}
	}
	
	clientnode = create_node(mode, NODE_ROLE_CLIENT);
	if(clientnode == NULL){
		error_no = 4;
		goto err_exit;
	}
	clientnode->port = remote_port;
	clientnode->addr = ntohl(addr.s_addr);
	clientnode->local_port = local_port;
	
	if(xTaskCreate(client_start_task, ((const char*)"client_start_task"), ATCP_STACK_SIZE, clientnode, ATCMD_LWIP_TASK_PRIORITY, NULL) != pdPASS)
	{	
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
			"[ATPC] ERROR: Create tcp/udp client task failed.");
		error_no = 5;
		goto err_exit;
	}

	goto exit;
err_exit:
	if(clientnode)
		delete_node(clientnode);
	at_printf("\r\n[ATPC] ERROR:%d", error_no);
exit:
	return;
}


void fATPS(void *arg){
	int argc;
	char *argv[MAX_ARGC] = {0};
	node* servernode = NULL;
	int mode;
	int local_port;
	int error_no = 0;

	AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS, 
		"[ATPS]: _AT_TRANSPORT_START_SERVER");

	if(atcmd_lwip_is_tt_mode()){
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
			"[ATPS] ERROR: Server can only start when TT is disabled");
		error_no = 13;
		goto err_exit;
	}
	
	argc = parse_param(arg, argv);
	if(argc != 3){
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
			"[ATPS] Usage: ATPS=[TCP:0/UDP:1],[Local port(1~65535)]");
		error_no = 1;
		goto err_exit;
	}

	mode = atoi((char*)argv[1]);
	local_port = atoi((char*)argv[2]);

	if(local_port < 0 || local_port > 65535){
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
			"[ATPS] Usage: ATPS=[TCP:0/UDP:1],[Local port]");
		error_no = 2;
		goto err_exit;
	}

	servernode = create_node(mode, NODE_ROLE_SERVER);
	if(servernode == NULL){
		error_no = 3;
		goto err_exit;
	}
	servernode->port = local_port;

	if(xTaskCreate(server_start_task, ((const char*)"server_start_task"), ATCP_STACK_SIZE, servernode, ATCMD_LWIP_TASK_PRIORITY, &servernode->handletask) != pdPASS)
	{	
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
			"[ATPS] ERROR: Create tcp/udp server task failed.");
		error_no = 4;
		goto err_exit;
	}

	goto exit;
err_exit:
	if(servernode)
		delete_node(servernode);
	at_printf("\r\n[ATPS] ERROR:%d", error_no);
exit:
	return;
}

void socket_close_all(void)
{
	node *currNode = mainlist->next;
	
	while(currNode)
	{
		delete_node(currNode);
		currNode = mainlist->next;
	}
	currNode = NULL;
}

void fATPD(void *arg){
	int con_id = INVALID_CON_ID;
	int error_no = 0;
	node *s_node;

	AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS, 
		"[ATPD]: _AT_TRANSPORT_CLOSE_CONNECTION");
	
	if(!arg){
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
			"[ATPD] Usage: ATPD=con_id or 0 (close all)");
		error_no = 1;
		goto exit;
	}
	con_id = atoi((char*)arg);

	if(con_id == 0){
		if(atcmd_lwip_is_autorecv_mode()){
			atcmd_lwip_set_autorecv_mode(FALSE);
		}
		socket_close_all();
		goto exit;
	}

	s_node = seek_node(con_id);
	if(s_node == NULL){
		error_no = 3;
		goto exit;
	}
	delete_node(s_node);

exit:
	s_node = NULL;
	if(error_no)
		at_printf("\r\n[ATPD] ERROR:%d", error_no);
	else
		at_printf("\r\n[ATPD] OK");
	return;
}

int atcmd_lwip_send_data(node *curnode, u8 *data, u16 data_sz, struct sockaddr_in cli_addr){
	int error_no = 0;
	
	if((curnode->protocol == NODE_MODE_UDP) && (curnode->role == NODE_ROLE_SERVER))
	{
		if (sendto(curnode->sockfd, data, data_sz, 0, (struct sockaddr *)&cli_addr, sizeof(cli_addr)) <= 0 ){
			AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
				"[ATPT] ERROR:Failed to send data");
			error_no = 5;
		}
	}else{
		if(curnode->protocol == NODE_MODE_UDP) //UDP server
		{
			struct sockaddr_in serv_addr;
			memset(&serv_addr, 0, sizeof(serv_addr));
			serv_addr.sin_family = AF_INET; 
			serv_addr.sin_port = htons(curnode->port);
			serv_addr.sin_addr.s_addr = htonl(curnode->addr);
			if(sendto( curnode->sockfd, data, data_sz, 0, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) <= 0){
				AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
					"[ATPT] ERROR:Failed to send data\n");
				error_no = 6;
			}
		}else if(curnode->protocol == NODE_MODE_TCP)//TCP server
		{
			if(curnode->role == NODE_ROLE_SERVER){
				AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
					"[ATPT] ERROR: TCP Server must send data to the seed");
				error_no = 7;
				goto exit;
			}
			
			if((write(curnode->sockfd, data, data_sz)) <= 0){
				AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
					"[ATPT] ERROR:Failed to send data\n");
				error_no = 8;
			}
		}
	}
	
exit:
	return error_no;
}

void fATPT(void *arg){

	int argc;
	char *argv[MAX_ARGC] = {0};
	int con_id = INVALID_CON_ID;
	int error_no = 0;
	node* curnode = NULL;
	struct sockaddr_in cli_addr;
	int data_sz;
	int data_pos = C_NUM_AT_CMD + C_NUM_AT_CMD_DLT+ strlen(arg) + 1;
	u8 *data = (u8 *)log_buf + data_pos;

	AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS, 
		"[ATPT]: _AT_TRANSPORT_SEND_DATA");

	argc = parse_param(arg, argv);

	if(argc != 3 && argc != 5) {
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
			"[ATPT] Usage: ATPT=<data_size>,"
			"<con_id>[,<dst_ip>,<dst_port>]"
			":<data>(MAX %d)", 
			MAX_BUFFER);
		error_no = 1;
		goto exit;
	}

	data_sz = atoi((char*)argv[1]);
	if(data_sz > MAX_BUFFER){
		error_no = 2;
		goto exit;
	}
	
	con_id = atoi((char*)argv[2]);
	curnode = seek_node(con_id);
	if(curnode == NULL){
		error_no = 3;
		goto exit;
	}

	if((curnode->protocol == NODE_MODE_UDP)
		&&(curnode->role == NODE_ROLE_SERVER))
	{
			char udp_clientaddr[16]={0};
			strcpy((char*)udp_clientaddr, (char*)argv[3]);
			cli_addr.sin_family = AF_INET;
			cli_addr.sin_port = htons(atoi((char*)argv[4]));
			if (inet_aton(udp_clientaddr , &cli_addr.sin_addr) == 0) 
			{
				AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
				"[ATPT]ERROR:inet_aton() failed");
				error_no = 4;
				goto exit;
			}
	}
	error_no = atcmd_lwip_send_data(curnode, data, data_sz, cli_addr);
exit:
	if(error_no)
		at_printf("\r\n[ATPT] ERROR:%d,%d", error_no, con_id);
	else
		at_printf("\r\n[ATPT] OK,%d", con_id);
	return;
}

void fATPR(void *arg){

	int argc,con_id = INVALID_CON_ID;
	char *argv[MAX_ARGC] = {0};
	int error_no = 0;
	int recv_size = 0;	
	int packet_size = 0;
	node* curnode = NULL;
	u8_t udp_clientaddr[16] = {0};
	u16_t udp_clientport = 0;
	
	AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS, 
		"[ATPR]: _AT_TRANSPORT_RECEIVE_DATA");

	if(atcmd_lwip_is_autorecv_mode()){
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
			"[ATPR] ERROR: Receive changed to auto mode.");
		error_no = 10;
		goto exit;
	}
	
	argc = parse_param(arg, argv);
	if( argc != 3){
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR, 
			"[ATPR] Usage: ATPR =<con_id>,<Buffer Size>\n\r");
		error_no = 1;
		goto exit;
	}

	con_id = atoi((char*)argv[1]);
	if(con_id <= 0 || con_id > NUM_NS){
		error_no = 9;
		goto exit;
	}
	
	packet_size = atoi((char*)argv[2]);

	if(packet_size <= 0 || packet_size > MAX_BUFFER){
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR, 
			"[ATPR] Recv Size(%d) exceeds MAX_BUFFER(%d)", packet_size, 
			MAX_BUFFER);
		error_no = 2;
		goto exit;
	}

	curnode = seek_node(con_id);
	if(curnode == NULL){
		error_no = 3;
		goto exit;
	}
	
	if(curnode->protocol == NODE_MODE_TCP && curnode->role == NODE_ROLE_SERVER){
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
			"[ATPR] ERROR: TCP Server must receive data from the seed");
		error_no = 6;
		goto exit;
	}

	memset(rx_buffer, 0, MAX_BUFFER);
	error_no = atcmd_lwip_receive_data(curnode, rx_buffer, ETH_MAX_MTU, &recv_size, udp_clientaddr, &udp_clientport);
exit:
	if(error_no == 0){
		if(curnode->protocol == NODE_MODE_UDP && curnode->role == NODE_ROLE_SERVER){
			AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS,
					"\r\n[ATPR] OK,%d,%d,%s,%d:", recv_size, con_id, udp_clientaddr, udp_clientport);
			at_printf("\r\n[ATPR] OK,%d,%d,%s,%d:", recv_size, con_id, udp_clientaddr, udp_clientport);
		}
		else{
			AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS,
					"\r\n[ATPR] OK,%d,%d:", recv_size, con_id);
			at_printf("\r\n[ATPR] OK,%d,%d:", recv_size, con_id);
		}
		if(recv_size)
			at_print_data(rx_buffer, recv_size);
	}
	else
		at_printf("\r\n[ATPR] ERROR:%d,%d", error_no, con_id);
	return;
}

void fATPK(void *arg){

	int argc;
	int error_no = 0;
	int enable = 0;
	char *argv[MAX_ARGC] = {0};
	
	AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS, 
		"[ATPK]: _AT_TRANSPORT_AUTO_RECV");
	
	argc = parse_param(arg, argv);
	if( argc < 2){
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR, 
			"[ATPK] Usage: ATPK=<0/1>\n\r");
		error_no = 1;
		goto exit;
	}

	enable = atoi((char*)argv[1]);

	if(enable){
		if(atcmd_lwip_is_autorecv_mode()){
			AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS, "[ATPK] already enter auto receive mode");
		}
		else{
			if(atcmd_lwip_start_autorecv_task())
				error_no = 2;
		}
	}else{
		if(atcmd_lwip_is_autorecv_mode())
			atcmd_lwip_set_autorecv_mode(FALSE);
		else{
			AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS,"[ATPK] already leave auto receive mode");
		}
	}

exit:
	if(error_no)
		at_printf("\r\n[ATPK] ERROR:%d", error_no);
	else
		at_printf("\r\n[ATPK] OK");
	return;
}

void fATPU(void *arg){

	int argc;
	int error_no = 0;
	int enable = 0;
	char *argv[MAX_ARGC] = {0};
	
	AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS, 
		"[ATPU]: _AT_TRANSPORT_TT_MODE");
	
	argc = parse_param(arg, argv);
	if( argc < 2){
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR, 
			"[ATPU] Usage: ATPU=<1>\n\r");
		error_no = 1;
		goto exit;
	}

	enable = atoi((char*)argv[1]);

	if(enable){
		if(!mainlist->next){
			AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR, "[ATPU] No conn found");
			error_no = 2;
		}else if(mainlist->next->role == NODE_ROLE_SERVER){
			AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR, "[ATPU] No TT mode for server");
			error_no = 3;
		}
		else if(mainlist->next->next || mainlist->next->nextseed){
			AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR, "[ATPU] More than one conn found");
			error_no = 4;
		}
		else{
			if(atcmd_lwip_start_tt_task()){
				error_no = 5;
			}
		}
	}

exit:
	if(error_no)
		at_printf("\r\n[ATPU] ERROR:%d", error_no);
	else
		at_printf("\r\n[ATPU] OK");
	return;
}

//ATPL=<enable>
void fATPL(void *arg)
{
	int argc, error_no = 0;
	char *argv[MAX_ARGC] = {0};

	if(!arg){
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
			"\r\n[ATPL] Usage : ATPL=<enable>");
		error_no = 1;
		goto exit;
	}
	argc = parse_param(arg, argv);
	if(argc != 2){
		error_no = 2;
		goto exit;
	}

	//ENABLE LWIP FAST CONNECT
	if(argv[1] != NULL){
		int enable = atoi(argv[1]);
		struct atcmd_lwip_conn_info cur_conn = {0};
		node *cur_node = mainlist->next;
		if(enable && cur_node == NULL){
			error_no = 3;
			goto exit;
		}
		cur_conn.role = cur_node->role;
		cur_conn.protocol = cur_node->protocol;
		cur_conn.remote_addr = cur_node->addr;
		cur_conn.remote_port = cur_node->port;
		cur_conn.local_addr = cur_node->local_addr;
		cur_conn.local_port = cur_node->local_port;
		atcmd_lwip_write_info_to_flash(&cur_conn, enable);
	}

exit:
	if(error_no == 0)
		at_printf("\r\n[ATPL] OK");
	else
		at_printf("\r\n[ATPL] ERROR:%d",error_no);

	return;
}

extern void do_ping_call(char *ip, int loop, int count);
void fATPP(void *arg){
	int count, argc = 0;
	char buf[32] = {0};
	char *argv[MAX_ARGC] = {0};
	int con_id=INVALID_CON_ID;
	int error_no = 0;

	AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS, 
		"[ATPP]: _AT_TRANSPORT_PING");
	
	if(!arg){
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,"[ATPP] Usage: ATPP=xxxx.xxxx.xxxx.xxxx[y/loop] or ATPP=[con_id],[y/loop]\n\r");
		error_no = 1;
		goto exit;
	}
	
	argc = parse_param(arg, argv);

	if( strlen(argv[1]) < 3 )
	{
		node* curnode;
		struct in_addr addr;
		con_id = atoi( (char*)argv[1] );
		curnode = seek_node(con_id);
		if(curnode == NULL){
			error_no = 2;
			goto exit;
		}
		if( curnode->role == 1){ //ping remote server
			addr.s_addr = htonl(curnode->addr);
			inet_ntoa_r(addr, buf, sizeof(buf));
		}else if( curnode->role == 0){//ping local server
			strcpy(buf,SERVER);
		}else if( curnode->role == 2){ //ping seed
			strcpy(buf,(char*) curnode->addr);
		}
	}else
		strcpy(buf, argv[1]);

	if(argc == 2){
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS,"[ATPP]Repeat Count: 5");
		do_ping_call(buf, 0, 5);	//Not loop, count=5
	}else{
		if(strcmp(argv[2], "loop") == 0){
			AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS,"[ATPP]Repeat Count: %s", "loop");
			do_ping_call(buf, 1, 0);	//loop, no count
		}else{
			count = atoi(argv[2]);
			AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS,"[ATPP]Repeat Count: %d", count);
			do_ping_call(buf, 0, count);	//Not loop, with count
		}
	}

exit:
	if(error_no)
		at_printf("\r\n[ATPP] ERROR:%d", error_no);
	else
		at_printf("\r\n[ATPP] OK");
	return;
}

void fATPI(void *arg){
	node* n = mainlist->next;
	struct in_addr addr;

	AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS, 
		"[ATPI]: _AT_TRANSPORT_CONNECTION_INFO");
	
	while (n != NULL)
	{
		if(n->con_id == 0)
			continue;

		at_printf("\r\ncon_id:%d,", n->con_id);

		if(n->role == 0)
			at_printf("server,");
		else
			at_printf("client,");
		if(n->protocol == 0)
			at_printf("tcp,");
		else
			at_printf("udp,");

		addr.s_addr = htonl(n->addr);
		at_printf("address:%s,port:%d,socket:%d", inet_ntoa(addr) ,n->port, n->sockfd);
		if(n->nextseed != NULL)
		{
			node* seed = n;
			do{
				seed = seed->nextseed;
				at_printf("\r\ncon_id:%d,seed,", seed->con_id);
				if(seed->protocol == 0)
					at_printf("tcp,");
				else
					at_printf("udp,");
				addr.s_addr = htonl(seed->addr);
				at_printf("address:%s,port:%d,socket:%d", inet_ntoa(addr), seed->port, seed->sockfd);
			}while (seed->nextseed != NULL);
		}
		n = n->next;
	}

	at_printf("\r\n[ATPI] OK");

	return;
}

void init_node_pool(void){
	int i;
	memset(node_pool, 0, sizeof(node_pool));
	for(i=0;i<NUM_NS;i++){
		node_pool[i].con_id = INVALID_CON_ID;
	}
}

node* create_node(int mode, s8_t role){
	int i;

	SYS_ARCH_DECL_PROTECT(lev);
	for (i = 0; i < NUM_NS; ++i) {
		SYS_ARCH_PROTECT(lev);
		if (node_pool[i].con_id == INVALID_CON_ID) {
			node_pool[i].con_id = i;
			SYS_ARCH_UNPROTECT(lev);
			node_pool[i].sockfd = INVALID_SOCKET_ID;
			node_pool[i].protocol = mode; // 0:TCP, 1:UDP
			node_pool[i].role = role; // 0:server, 1:client, 2:SEED
			node_pool[i].addr = 0;
			node_pool[i].port = -1;
			node_pool[i].handletask = NULL;
			node_pool[i].next = NULL;
			node_pool[i].nextseed = NULL;
			return &node_pool[i];
		}
		SYS_ARCH_UNPROTECT(lev);
	}
	AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR, "No con_id available");
	return NULL;
}

void delete_node(node *n)
{
	node *currNode, *prevNode, *currSeed, *precvSeed;
	if(n == NULL)
		return;
	SYS_ARCH_DECL_PROTECT(lev);
	SYS_ARCH_PROTECT(lev);
	//need to remove it from mainlist first 
	for(currNode = mainlist; currNode != NULL; prevNode = currNode, currNode = currNode->next )
	{
		if(currNode == n){
			prevNode->next = currNode->next;
		}

		if(currNode->role != NODE_ROLE_SERVER)
			continue;
		
		precvSeed = currNode;
		currSeed = currNode->nextseed;
		while (currSeed != NULL)
		{
			if(currSeed == n){
				precvSeed->nextseed = n->nextseed;
			}
			precvSeed = currSeed;
			currSeed = currSeed->nextseed;
		}	
	}
	SYS_ARCH_UNPROTECT(lev);
	
	if(n->role == NODE_ROLE_SERVER){
		//node may have seed if it's under server mode
		while(n->nextseed != NULL){
			currSeed = n->nextseed;
			// only tcp seed has its own socket, udp seed uses its server's
			// so delete udp seed can't close socket which is used by server
			if(currSeed->protocol == NODE_MODE_TCP && currSeed->sockfd != INVALID_SOCKET_ID){
				close(currSeed->sockfd);
				currSeed->sockfd = INVALID_SOCKET_ID;
			}
			// no task created for seed
			//if(s->handletask != NULL)
			//	vTaskDelete(s->handletask);
			n->nextseed = currSeed->nextseed;
			currSeed->con_id = INVALID_CON_ID;
		};
	}
	
	if(!((n->protocol == NODE_MODE_UDP)&&(n->role == NODE_ROLE_SEED))){
		if(n->sockfd != INVALID_SOCKET_ID){
			close(n->sockfd);
			n->sockfd = INVALID_SOCKET_ID;
		}
	}
	//task will exit itself in fail case
	if(n->handletask){
		vTaskDelete(n->handletask);
		n->handletask = NULL;
	}
	n->con_id = INVALID_CON_ID;
	return;
}

int hang_node(node* insert_node)
{
	node* n = mainlist;
	SYS_ARCH_DECL_PROTECT(lev);
	SYS_ARCH_PROTECT(lev);
	while (n->next != NULL)
	{
		n = n->next;
		if(insert_node->role == NODE_ROLE_SERVER) //need to check for server in case that two conns are binded to same port, because SO_REUSEADDR is enabled
		{
			if( (n->port == insert_node->port) && ((n->addr== insert_node->addr) && (n->role == insert_node->role) && (n->protocol == insert_node->protocol) ) ){
				SYS_ARCH_UNPROTECT(lev);
				struct in_addr addr;
				addr.s_addr = htonl(insert_node->addr); 
				AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
					"This conn(IP:%s PORT:%d) already exist",
					inet_ntoa(addr),insert_node->port);
				return -1;
			}
		}
	}

	n->next = insert_node;
	SYS_ARCH_UNPROTECT(lev);
	return 0;
}

int hang_seednode(node* main_node ,node* insert_node)
{
	node* n = main_node;

	SYS_ARCH_DECL_PROTECT(lev);
	SYS_ARCH_PROTECT(lev);	 
	while (n->nextseed != NULL)
	{
		n = n->nextseed;
		if( (n->port == insert_node->port) && (n->addr == insert_node->addr)){
			SYS_ARCH_UNPROTECT(lev);
			struct in_addr addr;
			addr.s_addr = htonl(insert_node->addr); 
			AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS,
				"This seed IP:%s PORT:%d already exist",
				inet_ntoa(addr),insert_node->port);
			return -1;
		}
	}

	n->nextseed = insert_node;
	SYS_ARCH_UNPROTECT(lev);
	return 0;
}

node *seek_node(int con_id)
{
	node* n = mainlist;
	while (n->next != NULL)
	{
		n = n->next;
		if(n->con_id == con_id)
			return n;

		if(n->nextseed != NULL)
		{
			node* seed = n;
			do{
				seed = seed->nextseed;
				if(seed->con_id == con_id)
					return seed;
			}while (seed->nextseed != NULL);
		}
	}
	return NULL;
}

node *tryget_node(int n)
{
	SYS_ARCH_DECL_PROTECT(lev);
	if ((n <= 0) || (n > NUM_NS)) {
		return NULL;
	}
	SYS_ARCH_PROTECT(lev);
	if (node_pool[n].con_id == INVALID_CON_ID || node_pool[n].sockfd == INVALID_SOCKET_ID) {
		SYS_ARCH_UNPROTECT(lev);
		return NULL;
	}
	SYS_ARCH_UNPROTECT(lev);
	return &node_pool[n];
}

int atcmd_lwip_receive_data(node *curnode, u8 *buffer, u16 buffer_size, int *recv_size, 
	u8_t *udp_clientaddr, u16_t *udp_clientport){
		
	struct timeval tv;
	fd_set readfds;
	int error_no = 0, ret = 0, size = 0;

	FD_ZERO(&readfds);
	FD_SET(curnode->sockfd, &readfds);
	tv.tv_sec = RECV_SELECT_TIMEOUT_SEC;
	tv.tv_usec = RECV_SELECT_TIMEOUT_USEC;
	ret = select(curnode->sockfd + 1, &readfds, NULL, NULL, &tv);
	if(!((ret > 0)&&(FD_ISSET(curnode->sockfd, &readfds))))
	{
		//AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS, 
		//	"[ATPR] No receive event for con_id %d", curnode->con_id);
		goto exit;
	}

	if(curnode->protocol == NODE_MODE_UDP) //udp server receive from client
	{
		if(curnode->role == NODE_ROLE_SERVER){
			//node * clinode;
			struct sockaddr_in client_addr;
			u32_t addr_len = sizeof(struct sockaddr_in);  
			memset((char *) &client_addr, 0, sizeof(client_addr));

			if ((size = recvfrom(curnode->sockfd, buffer, buffer_size, 0, (struct sockaddr *) &client_addr, &addr_len)) > 0){
				//at_printf("[ATPR]:%d,%s,%d,%s\r\n with packet_size: %d\r\n",con_id, inet_ntoa(client_addr.sin_addr.s_addr), ntohs(client_addr.sin_port), rx_buffer, packet_size);	
				//at_printf("\r\nsize: %d\r\n", recv_size);
				//at_printf("%s", rx_buffer);
			}
			else{
				AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
					"[ATPR] ERROR:Failed to receive data");
				error_no = 4;
			}
#if 0
			clinode = create_node(NODE_MODE_UDP, NODE_ROLE_SEED);
			clinode->sockfd = curnode->sockfd;
			clinode->addr = ntohl(client_addr.sin_addr.s_addr);
			clinode->port = ntohs(client_addr.sin_port);
			if(hang_seednode(curnode,clinode) < 0){
				delete_node(clinode);
				clinode = NULL;
			}
#else
			inet_ntoa_r(client_addr.sin_addr.s_addr, (char *)udp_clientaddr, 16);
			*udp_clientport = ntohs(client_addr.sin_port);
#endif
		}
		else{
			struct sockaddr_in serv_addr;
			u32_t addr_len = sizeof(struct sockaddr_in);  
			memset((char *) &serv_addr, 0, sizeof(serv_addr));
			serv_addr.sin_family = AF_INET;
			serv_addr.sin_port = htons(curnode->port);
			serv_addr.sin_addr.s_addr = htonl(curnode->addr);
			
			if ((size = recvfrom(curnode->sockfd, buffer, buffer_size, 0, (struct sockaddr *) &serv_addr, &addr_len)) > 0){
				//at_printf("[ATPR]:%d,%s,%d,%s\r\n with packet_size: %d\r\n",con_id, inet_ntoa(serv_addr.sin_addr.s_addr), ntohs(serv_addr.sin_port), rx_buffer, packet_size);
				//at_printf("\r\nsize: %d\r\n", recv_size);
				//at_printf("%s", rx_buffer);
			}
			else{
				AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
					"[ATPR] ERROR:Failed to receive data");
				error_no = 5;
			}
		}
	}
	else{
		#if 0
		if(curnode->role == NODE_ROLE_SERVER){
			AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
				"[ATPR] ERROR: TCP Server must receive data from the seed");
			error_no = 6;
		}
		#endif
		//receive from seed or server
		if((size = read(curnode->sockfd,buffer,buffer_size)) > 0)
		{
			//struct in_addr addr;
			//addr.s_addr = htonl(curnode->addr);
			//at_printf("[ATPR]:%d,%s,%d,%s\r\n with packet_size: %d\r\n",con_id, inet_ntoa(addr), curnode->port, rx_buffer, packet_size);
			//at_printf("\r\nsize: %d\r\n", recv_size);
			//at_printf("%s", rx_buffer);
		}
		else{
			if(size == 0){
				AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
				"[ATPR] ERROR:Connection is closed!");
				error_no = 7;
			}
			else{
				AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
				"[ATPR] ERROR:Failed to receive data!");
				error_no = 8;
			}
		}
	}
exit:
	if(error_no == 0)
		*recv_size = size;
	else{
		close(curnode->sockfd);
		curnode->sockfd = INVALID_SOCKET_ID;
	}
	return error_no;
}

static void atcmd_lwip_receive_task(void *param)
{

	int i;
	int packet_size = ETH_MAX_MTU;

	AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS, 
			"Enter auto receive mode");
	
	while(atcmd_lwip_is_autorecv_mode())
	{
		for (i = 0; i < NUM_NS; ++i) {
			node* curnode = NULL;
			int error_no = 0;
			int recv_size = 0;	
			u8_t udp_clientaddr[16] = {0};
			u16_t udp_clientport = 0;			
			curnode = tryget_node(i);
			if(curnode == NULL)
				continue;
			if(curnode->protocol == NODE_MODE_TCP && curnode->role == NODE_ROLE_SERVER){
				//TCP Server must receive data from the seed
				continue;
			}
			error_no = atcmd_lwip_receive_data(curnode, rx_buffer, packet_size, &recv_size, udp_clientaddr, &udp_clientport);

			if(atcmd_lwip_is_tt_mode()){
				if((error_no == 0) && recv_size){
					rx_buffer[recv_size] = '\0';
					AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS,"Recv[%d]:%s", recv_size, rx_buffer);
					at_print_data(rx_buffer, recv_size);
					rtw_msleep_os(20);
				}
				continue;
			}
			
			if(error_no == 0){
				if(recv_size){
					#if CONFIG_LOG_SERVICE_LOCK
					log_service_lock();
					#endif
					if(curnode->protocol == NODE_MODE_UDP && curnode->role == NODE_ROLE_SERVER){
						AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS,
								"\r\n[ATPR] OK,%d,%d,%s,%d:", recv_size, curnode->con_id, udp_clientaddr, udp_clientport);
						at_printf("\r\n[ATPR] OK,%d,%d,%s,%d:", recv_size, curnode->con_id, udp_clientaddr, udp_clientport);
					}
					else{
						AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS,
								"\r\n[ATPR] OK,%d,%d:", 
								recv_size, 
								curnode->con_id);
						at_printf("\r\n[ATPR] OK,%d,%d:", recv_size, curnode->con_id);
					}
					at_print_data(rx_buffer, recv_size);
					at_printf(STR_END_OF_ATCMD_RET);
					#if CONFIG_LOG_SERVICE_LOCK
					log_service_unlock();
					#endif
				}
			}
			else{
				#if CONFIG_LOG_SERVICE_LOCK
				log_service_lock();
				#endif
				at_printf("\r\n[ATPR] ERROR:%d,%d", error_no, curnode->con_id);				
				at_printf(STR_END_OF_ATCMD_RET);
				#if CONFIG_LOG_SERVICE_LOCK
				log_service_unlock();
				#endif
			}
		}
	}

	AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS, 
			"Leave auto receive mode");
	
	vTaskDelete(NULL);
}

int atcmd_lwip_start_autorecv_task(void){
	atcmd_lwip_set_autorecv_mode(TRUE);
	if(xTaskCreate(atcmd_lwip_receive_task, ((const char*)"atcmd_lwip_receive_task"), ATCP_STACK_SIZE, NULL, ATCMD_LWIP_TASK_PRIORITY, NULL) != pdPASS)
	{	
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
			"ERROR: Create receive task failed.");
		atcmd_lwip_set_autorecv_mode(FALSE);
		return -1;
	}
	return 0;
}

int atcmd_lwip_is_tt_mode(void){
	return (atcmd_lwip_tt_mode == TRUE);
}
void atcmd_lwip_set_tt_mode(int enable){
	atcmd_lwip_tt_mode = enable;
}

int atcmd_lwip_is_autorecv_mode(void){
	return (atcmd_lwip_auto_recv == TRUE);
}
void atcmd_lwip_set_autorecv_mode(int enable){
	atcmd_lwip_auto_recv = enable;
}

static void _tt_wait_rx_complete(){
	s32 tick_current = rtw_get_current_time();

	while(rtw_systime_to_ms(tick_current -atcmd_lwip_tt_lasttickcnt) < ATCMD_LWIP_TT_MAX_DELAY_TIME_MS ){
		rtw_msleep_os(5);
		tick_current = rtw_get_current_time();
	}
}

static void atcmd_lwip_tt_handler(void* param)
{
	struct sockaddr_in cli_addr;
	AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS, 
			"Enter TT data mode");
	while(RtlDownSema((_Sema *)&atcmd_lwip_tt_sema) == _SUCCESS) {
		_lock lock;
		_irqL irqL;
		int tt_size = 0;
		_tt_wait_rx_complete();
		
		rtw_enter_critical(&lock, &irqL);
		if((atcmd_lwip_tt_datasize >= 4) && (memcmp(log_buf, "----", 4) == 0)){
			atcmd_lwip_set_tt_mode(FALSE);
			atcmd_lwip_tt_datasize = 0;
			rtw_exit_critical(&lock, &irqL);
			break;
		}
		rtw_memcpy(tx_buffer, log_buf, atcmd_lwip_tt_datasize);
		tt_size = atcmd_lwip_tt_datasize;
		atcmd_lwip_tt_datasize = 0;
		rtw_exit_critical(&lock, &irqL);
		tx_buffer[tt_size] = '\0';
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS,"Send[%d]:%s", tt_size, tx_buffer);
		atcmd_lwip_send_data(mainlist->next, tx_buffer, tt_size, cli_addr);
	}

	AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS, 
			"Leave TT data mode");
	RtlFreeSema((_Sema *)&atcmd_lwip_tt_sema);
	atcmd_lwip_set_autorecv_mode(FALSE);
	at_printf(STR_END_OF_ATCMD_RET); //mark return to command mode
	vTaskDelete(NULL);
}

int atcmd_lwip_start_tt_task(void){
	RtlInitSema((_Sema *)&atcmd_lwip_tt_sema, 0);
	atcmd_lwip_set_tt_mode(TRUE);
	if(xTaskCreate(atcmd_lwip_tt_handler, ((const char*)"tt_hdl"), ATCP_STACK_SIZE, NULL, ATCMD_LWIP_TASK_PRIORITY, &atcmd_lwip_tt_task) != pdPASS){
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,
			"ERROR: Create tt task failed.");
		goto err_exit;
	}
	RtlMsleepOS(20);
	if(atcmd_lwip_is_autorecv_mode() != 1){
		if(atcmd_lwip_start_autorecv_task()){
			vTaskDelete(atcmd_lwip_tt_task);
			goto err_exit;
		}
	}

	return 0;
	
err_exit:
	atcmd_lwip_set_tt_mode(FALSE);
	return -1;
}

void atcmd_lwip_erase_info(void){
	atcmd_update_partition_info(AT_PARTITION_LWIP, AT_PARTITION_ERASE, NULL, 0);
}

int atcmd_lwip_write_info_to_flash(struct atcmd_lwip_conn_info *cur_conn, int enable)
{
	struct atcmd_lwip_conf read_data = {0};
	int i = 0, found = 0;	
	
	atcmd_update_partition_info(AT_PARTITION_LWIP, AT_PARTITION_READ, (u8 *) &read_data, sizeof(struct atcmd_lwip_conf));

	//fake that the conn exists already when disabling or there is no active conn on this moment
	if(enable == 0){
		atcmd_lwip_erase_info();
		goto exit;
	}

	if(read_data.conn_num < 0 || read_data.conn_num > ATCMD_LWIP_CONN_STORE_MAX_NUM){
		read_data.conn_num = 0;
		read_data.last_index = -1;
	}

	for(i = 0; i < read_data.conn_num; i++){
		if(memcmp((u8 *)cur_conn, (u8 *)&read_data.conn[i], sizeof(struct atcmd_lwip_conn_info)) == 0) {
			AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS,
				"the same profile found in flash");
			found = 1;
			break;
		}
	}

	if(!found){
		read_data.last_index++;
		if(read_data.last_index >= ATCMD_LWIP_CONN_STORE_MAX_NUM)
			read_data.last_index -= ATCMD_LWIP_CONN_STORE_MAX_NUM;
		memcpy((u8 *)&read_data.conn[read_data.last_index], (u8 *)cur_conn, sizeof(struct atcmd_lwip_conn_info));
		read_data.conn_num++;
		if(read_data.conn_num > ATCMD_LWIP_CONN_STORE_MAX_NUM)
			read_data.conn_num = ATCMD_LWIP_CONN_STORE_MAX_NUM;
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS,
			"not the same proto/addr/port, write new profile to flash");
	}
	if(!found || read_data.enable != enable){
		read_data.enable = enable;
		atcmd_update_partition_info(AT_PARTITION_LWIP, AT_PARTITION_WRITE, (u8 *) &read_data, sizeof(struct atcmd_lwip_conf)); 
	}
exit:	
	return 0;
}

int atcmd_lwip_read_info_from_flash(u8 *read_data, u32 read_len)
{
	atcmd_update_partition_info(AT_PARTITION_LWIP, AT_PARTITION_READ, read_data, read_len);
	return 0;
}

int atcmd_lwip_auto_connect(void)
{
	struct atcmd_lwip_conf read_data = {0};
	struct atcmd_lwip_conn_info *re_conn;
	node* re_node = NULL;
	int i, error_no = 0;
	int last_index;
	
	atcmd_lwip_read_info_from_flash((u8 *)&read_data, sizeof(struct atcmd_lwip_conf));
	if(read_data.enable == 0){
		error_no = 1;
		goto exit;
	}
	if(read_data.conn_num > ATCMD_LWIP_CONN_STORE_MAX_NUM || read_data.conn_num <= 0){
		error_no = 2;
		goto exit;
	}

	last_index = read_data.last_index;
	for(i = 0; i < read_data.conn_num; i++){
		re_conn = &read_data.conn[last_index];
		last_index ++;
		if(last_index >= ATCMD_LWIP_CONN_STORE_MAX_NUM)
			last_index -= ATCMD_LWIP_CONN_STORE_MAX_NUM;
		re_node = create_node(re_conn->protocol, re_conn->role);
		if(re_node == NULL){
			error_no = 3;
			break;
		}
		re_node->addr = re_conn->remote_addr;
		re_node->port = re_conn->remote_port;
		re_node->local_addr = re_conn->local_addr;
		re_node->local_port = re_conn->local_port;
		if(re_node->protocol == NODE_MODE_UDP)
			re_node->sockfd = socket(AF_INET,SOCK_DGRAM,0);
		else
			re_node->sockfd = socket(AF_INET, SOCK_STREAM, 0);
		
		if (re_node->sockfd == INVALID_SOCKET_ID) {
			AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,"Failed to create sock_fd!");
			error_no = 4;
			break;
		}
		
		struct in_addr addr;
		addr.s_addr = htonl(re_node->addr);
		AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS,
			"\r\nTry connect: %d,%d,%s,%d", 
			re_node->sockfd, re_node->protocol, 
			inet_ntoa(addr), re_node->port);

		if(re_node->role == NODE_ROLE_SERVER){
			//TODO: start server here
			goto exit;
		}

		if (re_node->protocol == NODE_MODE_TCP){//TCP MODE
			struct sockaddr_in c_serv_addr;
			memset(&c_serv_addr, 0, sizeof(c_serv_addr));
			c_serv_addr.sin_family = AF_INET;
			c_serv_addr.sin_addr.s_addr = htonl(re_node->addr);
			c_serv_addr.sin_port = htons(re_node->port);
			if(connect(re_node->sockfd, (struct sockaddr *)&c_serv_addr,  sizeof(c_serv_addr)) == 0){
				AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS,"Connect to Server successful!");
				if(hang_node(re_node) < 0){
					error_no = 5;
				}
				break;
			}else{
				AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,"Connect to Server failed(%d)!", errno);
				error_no = 6;
				delete_node(re_node);
				re_node = NULL;
				continue; //try next conn
			}
		}
		else{
			if(re_node->local_port){
				struct sockaddr_in addr;  
				memset(&addr, 0, sizeof(addr));  
				addr.sin_family=AF_INET;  
				addr.sin_port=htons(re_node->local_port);  
				addr.sin_addr.s_addr=htonl(INADDR_ANY) ;  
				if (bind(re_node->sockfd, (struct sockaddr *)&addr, sizeof(addr))<0) {
					AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ERROR,"bind sock error!"); 
					error_no = 7;
					delete_node(re_node);
					re_node = NULL;
					continue;
				}
			}
			if(hang_node(re_node) < 0){
				error_no = 8;
			}
			AT_DBG_MSG(AT_FLAG_LWIP, AT_DBG_ALWAYS,"UDP client starts successful!");
			break;
		}
	}
	
exit:
	if(re_node && error_no)
		delete_node(re_node);
	return error_no;
}

int atcmd_lwip_restore_from_flash(void){
	int ret = -1;
	if(atcmd_lwip_auto_connect() == 0){
		if(atcmd_lwip_start_tt_task() == 0)
			ret = 0;
	}
	return ret;
}
#endif	

#if CONFIG_TRANSPORT
log_item_t at_transport_items[ ] = {
#if ATCMD_VER == ATVER_1
	{"ATP1", fATP1,},//mode TCP=0,UDP=1
	{"ATP2", fATP2,},//LOCAL PORT
	{"ATP3", fATP3,},//REMOTE IP
	{"ATP4", fATP4,},//REMOTE PORT
	{"ATP5", fATP5,},//START SERVER
	{"ATP6", fATP6,},//START CLIENT
	{"ATP?", fATPZ,},//SETTING
	{"ATR0", fATR0,},//READ DATA
	{"ATR1", fATR1,},//SET PACKET SIZE
	{"ATRA", fATRA,},//WRITE DATA
	{"ATRB", fATRB,},//SET WRITE PACKET SIZE
#endif
#if ATCMD_VER == ATVER_2 
	{"ATP0", fATP0,},//query errno if defined
	{"ATPS", fATPS,},//Create Server
	{"ATPD", fATPD,},//Close Server/Client connection
	{"ATPC", fATPC,},//Create Client
	{"ATPT", fATPT,},//WRITE DATA
	{"ATPR", fATPR,},//READ DATA
	{"ATPK", fATPK,},//Auto recv
	{"ATPP", fATPP,},//PING
	{"ATPI", fATPI,},//printf connection status
	{"ATPU", fATPU,}, //transparent transmission mode
	{"ATPL", fATPL,}, //lwip auto reconnect setting
#endif	
};

#if ATCMD_VER == ATVER_2
void print_tcpip_at(void *arg){
	int index;
	int cmd_len = 0;

	cmd_len = sizeof(at_transport_items)/sizeof(at_transport_items[0]);
	for(index = 0; index < cmd_len; index++)
		at_printf("\r\n%s", at_transport_items[index].log_cmd);
}
#endif

void at_transport_init(void)
{    
#if ATCMD_VER == ATVER_2 
	init_node_pool();
	mainlist = create_node(-1,-1);
#endif	
	log_service_add_table(at_transport_items, sizeof(at_transport_items)/sizeof(at_transport_items[0]));
}

log_module_init(at_transport_init);
#endif
