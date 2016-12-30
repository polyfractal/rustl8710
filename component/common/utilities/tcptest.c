#include "FreeRTOS.h"
#include "task.h"
#include "main.h"

#include <lwip/sockets.h>
#include <lwip/raw.h>
#include <lwip/icmp.h>
#include <lwip/inet_chksum.h>
#include <platform/platform_stdlib.h>

#define PER_SECOND_REPORT       1
#define BSD_STACK_SIZE		    2048
#define DEFAULT_PORT            5001
#define DEFAULT_TIME            10
#define SERVER_BUF_SIZE         1500
#define CLIENT_BUF_SIZE         1460
#define KB                      1024
#define MB                      1048576//1024*1024
#define DEFAULT_TCP_BANDWIDTH   131072 //128*1024Bytes = 1Mbits
#define DEFAULT_UDP_BANDWIDTH   131072 //128*1024Bytes = 1Mbits
#define DEFAULT_UDP_TOS_VALUE   96 // BE=96


struct iperf_data_t{
    char server_ip[16];
    int  server_fd;
    int  client_fd;
    uint16_t port;
    unsigned char start;
    unsigned int buf_size;
    uint32_t time;
    uint64_t total_size;
    uint64_t bandwidth;
    uint8_t tos_value;
};

struct iperf_data_t tcp_server_data,tcp_client_data,udp_server_data,udp_client_data;

xTaskHandle g_tcp_server_task = NULL;
xTaskHandle g_tcp_client_task = NULL;
xTaskHandle g_udp_client_task = NULL;
xTaskHandle g_udp_server_task = NULL;
unsigned char g_tcp_terminate = 0;
unsigned char g_udp_terminate = 0;

int tcp_client_func(struct iperf_data_t iperf_data)
{
    struct sockaddr_in  ser_addr;
    char                *buffer = NULL;
    int                 i=0;
    uint32_t            start_time, end_time, report_start_time;
    uint64_t            total_size=0,report_size=0;

	buffer = pvPortMalloc(iperf_data.buf_size);
	if(!buffer){
		printf("\n\r[ERROR] %s: Alloc buffer failed",__func__);
        goto Exit2;
	}
    //filling the buffer
    for (i = 0; i < iperf_data.buf_size; i++){
        buffer[i] = (char)(i % 10);
    }

    //create socket
    if( (iperf_data.client_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0){
        printf("\n\r[ERROR] %s: Create TCP socket failed",__func__);
        goto Exit2;
    }

    //initialize value in dest
    memset(&ser_addr, 0, sizeof(ser_addr));
    ser_addr.sin_family = AF_INET;
    ser_addr.sin_port = htons(iperf_data.port);
    ser_addr.sin_addr.s_addr = inet_addr(iperf_data.server_ip);

    printf("\n\r%s: Server IP=%s, port=%d", __func__,iperf_data.server_ip, iperf_data.port);
    printf("\n\r%s: Create socket fd = %d", __func__,iperf_data.client_fd);

    //Connecting to server
    if( connect(iperf_data.client_fd, (struct sockaddr*)&ser_addr, sizeof(ser_addr)) < 0){
        printf("\n\r[ERROR] %s: Connect to server failed",__func__);
        goto Exit1;
    }
    printf("\n\r%s: Connect to server successfully",__func__);
    if(iperf_data.total_size == 0){
        start_time = xTaskGetTickCount();
        end_time = start_time;
        report_start_time = start_time;
        while ( ((end_time - start_time) <= (configTICK_RATE_HZ * iperf_data.time)) && (!g_tcp_terminate) ) {
            if( send(iperf_data.client_fd, buffer, iperf_data.buf_size,0) <= 0){
                printf("\n\r[ERROR] %s: TCP client send data error",__func__);
                goto Exit1;
            }
            total_size+=iperf_data.buf_size;
            report_size+=iperf_data.buf_size;
            end_time = xTaskGetTickCount();
            if(((end_time - report_start_time) >= (configTICK_RATE_HZ * 1)) && ((end_time - report_start_time) <= (configTICK_RATE_HZ * 2))) {
#if PER_SECOND_REPORT
                printf("\n\r%s: Send %d KBytes in %d ms, %d Kbits/sec",__func__, (uint32_t)(report_size/KB),(uint32_t)(end_time-report_start_time),((uint32_t)(report_size*8)/(end_time - report_start_time)));
#endif
                report_start_time = end_time;
                report_size = 0;
            }
            else if( (iperf_data.bandwidth!=0) && (report_size >= iperf_data.bandwidth) ){
            	while((end_time - report_start_time) < configTICK_RATE_HZ){
					end_time = xTaskGetTickCount();
				}
#if PER_SECOND_REPORT
            	printf("\n\r%s: Send %d KBytes in %d ms, %d Kbits/sec",__func__, (uint32_t)(report_size/KB),(uint32_t)(end_time-report_start_time),((uint32_t)(report_size*8)/(end_time - report_start_time)));
#endif
            	report_start_time = end_time;
                report_size = 0;
            }
        }
    }
    else{
    	start_time = xTaskGetTickCount();
		end_time = start_time;
		report_start_time = start_time;
        while ( (total_size < iperf_data.total_size) && (!g_tcp_terminate) ) {
            if( send(iperf_data.client_fd, buffer, iperf_data.buf_size,0) <= 0){
                printf("\n\r[ERROR] %s: TCP client send data error",__func__);
                goto Exit1;
            }
            total_size+=iperf_data.buf_size;
            report_size+=iperf_data.buf_size;
            end_time = xTaskGetTickCount();
            if(((end_time - report_start_time) >= (configTICK_RATE_HZ * 1)) && ((end_time - report_start_time) <= (configTICK_RATE_HZ * 2))) {
#if PER_SECOND_REPORT
            	printf("\n\r%s: Send %d KBytes in %d ms, %d Kbits/sec",__func__, (uint32_t)(report_size/KB),(uint32_t)(end_time-report_start_time),((uint32_t)(report_size*8)/(end_time - report_start_time)));
#endif
            	report_start_time = end_time;
                report_size = 0;
            }
            else if( (iperf_data.bandwidth!=0) && (report_size >= iperf_data.bandwidth) ){
            	while((end_time - report_start_time) < configTICK_RATE_HZ){
					end_time = xTaskGetTickCount();
				}
#if PER_SECOND_REPORT
				printf("\n\r%s: Send %d KBytes in %d ms, %d Kbits/sec",__func__, (uint32_t)(report_size/KB),(uint32_t)(end_time-report_start_time),((uint32_t)(report_size*8)/(end_time - report_start_time)));
#endif
				report_start_time = end_time;
                report_size = 0;
            }
        }
    }
    printf("\n\r%s: [END] Totally send %d KBytes in %d ms, %d Kbits/sec",__func__, (uint32_t)(total_size/KB),(uint32_t)(end_time-start_time),((uint32_t)(total_size*8)/(end_time - start_time)));

Exit1:
    closesocket(iperf_data.client_fd);
Exit2:
    printf("\n\r%s: Close client socket",__func__);
	if(buffer)
		vPortFree(buffer);
    return 0;
}

int tcp_server_func(struct iperf_data_t iperf_data)
{
    struct sockaddr_in   ser_addr , client_addr;
    char                 *buffer = NULL;
    int                  addrlen = sizeof(struct sockaddr_in);
    int                  n = 1;
    int                  recv_size=0;
    uint64_t             total_size=0,report_size=0;
    uint32_t             start_time, report_start_time, report_end_time;

	buffer = pvPortMalloc(iperf_data.buf_size);
	if(!buffer){
		printf("\n\r[ERROR] %s: Alloc buffer failed",__func__);
        goto Exit3;
	}

    //create socket
    if((iperf_data.server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0){
        printf("\n\r[ERROR] %s: Create socket failed",__func__);
        goto Exit3;
    }

    printf("\n\r%s: Create socket fd = %d", __func__,iperf_data.server_fd);

    setsockopt( iperf_data.server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *) &n, sizeof( n ) );

    //initialize structure dest
    memset(&ser_addr, 0, sizeof(ser_addr));
    ser_addr.sin_family = AF_INET;
    ser_addr.sin_port = htons(iperf_data.port);
    ser_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // binding the TCP socket to the TCP server address
    if( bind(iperf_data.server_fd, (struct sockaddr*)&ser_addr, sizeof(ser_addr)) < 0){
        printf("\n\r[ERROR] %s: Bind socket failed",__func__);
        goto Exit2;
    }
    printf("\n\r%s: Bind socket successfully",__func__);

    //Make it listen to socket with max 20 connections
    if( listen(iperf_data.server_fd, 20) != 0){
        printf("\n\r[ERROR] %s: Listen socket failed",__func__);
        goto Exit2;
    }
    printf("\n\r%s: Listen port %d",__func__,iperf_data.port);

Restart:
    if( (iperf_data.client_fd = accept(iperf_data.server_fd, (struct sockaddr*)&client_addr, &addrlen)) < 0){
        printf("\n\r[ERROR] %s: Accept TCP client socket error!",__func__);
        goto Exit2;
    }
    printf("\n\r%s: Accept connection successfully",__func__);

    start_time = xTaskGetTickCount();
    report_start_time = start_time;
    while (!g_tcp_terminate) {
        recv_size = recv(iperf_data.client_fd, buffer, iperf_data.buf_size, 0);  //MSG_DONTWAIT   MSG_WAITALL
        if( recv_size < 0){
            printf("\n\r[ERROR] %s: Receive data failed",__func__);
            goto Exit1;
        }
        else if(recv_size == 0){
        	printf("\n\r%s: [END] Totally receive %d KBytes in %d ms, %d Kbits/sec",__func__, (uint32_t) (total_size/KB),(uint32_t) (report_end_time-start_time),((uint32_t)(total_size*8)/(report_end_time - start_time)));
            total_size=0;
            close(iperf_data.client_fd);
            goto Restart;
        }
        report_end_time = xTaskGetTickCount();
        total_size+=recv_size;
        report_size+=recv_size;
        if(((report_end_time - report_start_time) >= (configTICK_RATE_HZ * 1)) && ((report_end_time - report_start_time) <= (configTICK_RATE_HZ * 2))) {
#if PER_SECOND_REPORT
        	printf("\n\r%s: Receive %d KBytes in %d ms, %d Kbits/sec",__func__, (uint32_t) (report_size/KB),(uint32_t) (report_end_time-report_start_time),((uint32_t)(report_size*8)/(report_end_time - report_start_time)));
#endif
        	report_start_time = report_end_time;
            report_size = 0;
        }
        else if((report_end_time - report_start_time) > (configTICK_RATE_HZ * 2)){
            report_start_time = report_end_time;
            start_time = report_end_time;
            report_size = 0;
        }
    }
    printf("\n\r%s: [END] Totally receive %d KBytes in %d ms, %d Kbits/sec",__func__, (uint32_t) (total_size/KB),(uint32_t) (report_end_time-start_time),((uint32_t)(total_size*8)/(report_end_time - start_time)));

Exit1:
    // close the connected socket after receiving from connected TCP client
    close(iperf_data.client_fd);

Exit2:
    // close the listening socket
    close(iperf_data.server_fd);

Exit3:
	if(buffer)
		vPortFree(buffer);
    return 0;
}

int udp_client_func(struct iperf_data_t iperf_data)
{
    struct sockaddr_in  ser_addr;
    char                *buffer = NULL;
    int                 i=0;
    int                 addrlen = sizeof(struct sockaddr_in);
    uint32_t            start_time, end_time, bandwidth_time;
    uint64_t            total_size=0, bandwidth_size=0;


	buffer = pvPortMalloc(iperf_data.buf_size);
	if(!buffer){
		printf("\n\r[ERROR] %s: Alloc buffer failed",__func__);
        goto Exit2;
	}

    //filling the buffer
	for (i = 0; i < iperf_data.buf_size; i++){
		buffer[i] = (char)(i % 10);
	}

    //create socket
    if( (iperf_data.client_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0){
        printf("\n\r[ERROR] %s: Create UDP socket failed",__func__);
        goto Exit2;
    }

    //initialize value in dest
    memset(&ser_addr, 0, sizeof(ser_addr));
    ser_addr.sin_family = AF_INET;
    ser_addr.sin_port = htons(iperf_data.port);
    ser_addr.sin_addr.s_addr = inet_addr(iperf_data.server_ip);

    printf("\n\r%s: Server IP=%s, port=%d", __func__,iperf_data.server_ip, iperf_data.port);
    printf("\n\r%s: Create socket fd = %d", __func__,iperf_data.client_fd);

    lwip_setsockopt(iperf_data.client_fd,IPPROTO_IP,IP_TOS,&iperf_data.tos_value,sizeof(iperf_data.tos_value));

    if(iperf_data.total_size == 0){
        start_time = xTaskGetTickCount();
        end_time = start_time;
        bandwidth_time = start_time;
        while ( ((end_time - start_time) <= (configTICK_RATE_HZ * iperf_data.time)) && (!g_udp_terminate) ) {
            if( sendto(iperf_data.client_fd, buffer, iperf_data.buf_size,0,(struct sockaddr*)&ser_addr, addrlen) < 0){
                //printf("\n\r[ERROR] %s: UDP client send data error",__func__);
            }else{
                total_size+=iperf_data.buf_size;
                bandwidth_size+=iperf_data.buf_size;
            }

            if((end_time - bandwidth_time) >= (configTICK_RATE_HZ*1)){
#if PER_SECOND_REPORT
                printf("\n\r%s: Send %d KBytes in %d ms, %d Kbits/sec",__func__,(uint32_t)( bandwidth_size/KB),(uint32_t)(end_time-bandwidth_time),((uint32_t)(bandwidth_size*8)/(end_time - bandwidth_time)));
#endif
                bandwidth_time = end_time;
                bandwidth_size = 0;
            }else{
                if(bandwidth_size >= iperf_data.bandwidth){
                    while((end_time - bandwidth_time) < configTICK_RATE_HZ){
                        end_time = xTaskGetTickCount();
                    }
#if PER_SECOND_REPORT
                    printf("\n\r%s: Send %d KBytes in %d ms, %d Kbits/sec",__func__, (uint32_t)(bandwidth_size/KB),(uint32_t)(end_time-bandwidth_time),((uint32_t)(bandwidth_size*8)/(end_time - bandwidth_time)));
#endif
                    bandwidth_time = end_time;
                    bandwidth_size = 0;
                }
            }

            end_time = xTaskGetTickCount();
        }
    }
    else{
    	start_time = xTaskGetTickCount();
		end_time = start_time;
		bandwidth_time = start_time;
        while ( (total_size < iperf_data.total_size) && (!g_udp_terminate) ) {
            if( sendto(iperf_data.client_fd, buffer, iperf_data.buf_size,0,(struct sockaddr*)&ser_addr, addrlen) < 0){
                //printf("\n\r[ERROR] %s: UDP client send data error",__func__);
            }else{
                total_size+=iperf_data.buf_size;
                bandwidth_size+=iperf_data.buf_size;
            }
            if((end_time - bandwidth_time) >= (configTICK_RATE_HZ*1)){
#if PER_SECOND_REPORT
                printf("\n\r%s: Send %d KBytes in %d ms, %d Kbits/sec",__func__, (uint32_t)(bandwidth_size/KB),(uint32_t)(end_time-bandwidth_time),((uint32_t)(bandwidth_size*8)/(end_time - bandwidth_time)));
#endif
                bandwidth_time = end_time;
                bandwidth_size = 0;
            }else{
                if(bandwidth_size >= iperf_data.bandwidth){
                    while((end_time - bandwidth_time) < (configTICK_RATE_HZ*1)){
                        end_time = xTaskGetTickCount();
                    }
#if PER_SECOND_REPORT
                    printf("\n\r%s: Send %d KBytes in %d ms, %d Kbits/sec",__func__, (uint32_t)(bandwidth_size/KB),(uint32_t)(end_time-bandwidth_time),((uint32_t)(bandwidth_size*8)/(end_time - bandwidth_time)));
#endif
                    bandwidth_time = end_time;
                    bandwidth_size = 0;
                }
            }
            end_time = xTaskGetTickCount();
        }
    }
    printf("\n\r%s: [END] Totally send %d KBytes in %d ms, %d Kbits/sec",__func__, (uint32_t)(total_size/KB),(uint32_t)(end_time-start_time),((uint32_t)(total_size*8)/(end_time - start_time)));

Exit1:
    close(iperf_data.client_fd);
Exit2:
    printf("\n\r%s: Close client socket",__func__);
	if(buffer)
		vPortFree(buffer);
    return 0;
}

int udp_server_func(struct iperf_data_t iperf_data)
{
    int                  server_fd;
    struct sockaddr_in   ser_addr , client_addr;
    char                 *buffer = NULL;
    int                  addrlen = sizeof(struct sockaddr_in);
    int                  n = 1;
    uint32_t             start_time, report_start_time, report_end_time;
    int                  recv_size=0;
    uint64_t             total_size=0,report_size=0;

	buffer = pvPortMalloc(iperf_data.buf_size);
	if(!buffer){
		printf("\n\r[ERROR] %s: Alloc buffer failed",__func__);
        goto Exit2;
	}

    //create socket
    if((iperf_data.server_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0){
        printf("\n\r[ERROR] %s: Create socket failed",__func__);
        goto Exit2;
    }

    printf("\n\r%s: Create socket fd = %d, port = %d", __func__,iperf_data.server_fd,iperf_data.port);

    setsockopt( iperf_data.server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *) &n, sizeof( n ) );

    //initialize structure dest
    memset(&ser_addr, 0, sizeof(ser_addr));
    ser_addr.sin_family = AF_INET;
    ser_addr.sin_port = htons(iperf_data.port);
    ser_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // binding the TCP socket to the TCP server address
    if( bind(iperf_data.server_fd, (struct sockaddr*)&ser_addr, sizeof(ser_addr)) < 0){
        printf("\n\r[ERROR] %s: Bind socket failed",__func__);
        goto Exit1;
    }

    printf("\n\r%s: Bind socket successfully",__func__);

    start_time = xTaskGetTickCount();
    report_start_time = start_time;

    while (!g_udp_terminate) {
        recv_size = recvfrom(iperf_data.server_fd,buffer,iperf_data.buf_size,0,(struct sockaddr *) &client_addr,&addrlen);
        if( recv_size < 0){
            printf("\n\r[ERROR] %s: Receive data failed",__func__);
            goto Exit1;
        }
        // ack data to client
        // Not send ack to prevent send fail due to limited skb, but it will have warning at iperf client
        //sendto(server_fd,buffer,ret,0,(struct sockaddr*)&client_addr,sizeof(client_addr));

        report_end_time = xTaskGetTickCount();
        total_size+=recv_size;
        report_size+=recv_size;
        if(((report_end_time - report_start_time) >= (configTICK_RATE_HZ * 1)) && ((report_end_time - report_start_time) <= (configTICK_RATE_HZ * 2))) {
#if PER_SECOND_REPORT
        	printf("\n\r%s: Receive %d KBytes in %d ms, %d Kbits/sec",__func__,(uint32_t) (report_size/KB),(uint32_t)(report_end_time-report_start_time),((uint32_t)(report_size*8)/(report_end_time - report_start_time)));
#endif
        	report_start_time = report_end_time;
            report_size = 0;
        }
        else if((report_end_time - report_start_time) > (configTICK_RATE_HZ * 2)){
            report_start_time = report_end_time;
            start_time = report_end_time;
            report_size = 0;
        }
    }
    printf("\n\r%s: [END] Totally receive %d KBytes in %d ms, %d Kbits/sec",__func__,(uint32_t) (total_size/KB),(uint32_t)(report_end_time-start_time),((uint32_t)(total_size*8)/(report_end_time - start_time)));

Exit1:
    // close the listening socket
    close(iperf_data.server_fd);

Exit2:
	if(buffer)
		vPortFree(buffer);
    return 0;
}

static void tcp_client_handler(void *param)
{
    vTaskDelay(100);
    if(tcp_client_data.port == 0)
        tcp_client_data.port = DEFAULT_PORT;
    if(tcp_client_data.time == 0)
        tcp_client_data.time = DEFAULT_TIME;
    if(tcp_client_data.buf_size == 0)
        tcp_client_data.buf_size = CLIENT_BUF_SIZE;

    printf("\n\rTCP: Start TCP client!");
    tcp_client_func(tcp_client_data);

#if defined(INCLUDE_uxTaskGetStackHighWaterMark) && (INCLUDE_uxTaskGetStackHighWaterMark == 1)
    printf("\n\rMin available stack size of %s = %d * %d bytes\n\r", __FUNCTION__, uxTaskGetStackHighWaterMark(NULL), sizeof(portBASE_TYPE));
#endif
    printf("\n\rTCP: TCP client stopped!");
    g_tcp_client_task = NULL;
    vTaskDelete(NULL);
}

static void tcp_server_handler(void *param)
{
    vTaskDelay(100);
    if(tcp_server_data.port == 0)
        tcp_server_data.port = DEFAULT_PORT;
    if(tcp_server_data.buf_size == 0)
        tcp_server_data.buf_size = SERVER_BUF_SIZE;

    printf("\n\rTCP: Start TCP server!");
    tcp_server_func(tcp_server_data);

#if defined(INCLUDE_uxTaskGetStackHighWaterMark) && (INCLUDE_uxTaskGetStackHighWaterMark == 1)
    printf("\n\rMin available stack size of %s = %d * %d bytes\n\r", __FUNCTION__, uxTaskGetStackHighWaterMark(NULL), sizeof(portBASE_TYPE));
#endif
    printf("\n\rTCP: TCP server stopped!");
    g_tcp_server_task = NULL;
    vTaskDelete(NULL);
}

void udp_client_handler(void *param)
{
    vTaskDelay(100);
    if(udp_client_data.port == 0)
        udp_client_data.port = DEFAULT_PORT;
    if(udp_client_data.time == 0)
        udp_client_data.time = DEFAULT_TIME;
    if(udp_client_data.bandwidth == 0)
        udp_client_data.bandwidth = DEFAULT_UDP_BANDWIDTH;
    if(udp_client_data.buf_size == 0)
        udp_client_data.buf_size = CLIENT_BUF_SIZE;
    if(udp_client_data.tos_value == 0)
    	udp_client_data.tos_value = DEFAULT_UDP_TOS_VALUE;

    printf("\n\rUDP: Start UDP client!");
	udp_client_func(udp_client_data);

#if defined(INCLUDE_uxTaskGetStackHighWaterMark) && (INCLUDE_uxTaskGetStackHighWaterMark == 1)
	printf("\n\rMin available stack size of %s = %d * %d bytes", __FUNCTION__, uxTaskGetStackHighWaterMark(NULL), sizeof(portBASE_TYPE));
#endif

	printf("\n\rUDP: UDP client stopped!");
	g_udp_client_task = NULL;
	vTaskDelete(NULL);	
}

void udp_server_handler(void *param)
{
    vTaskDelay(100);
    if(udp_server_data.port == 0)
        udp_server_data.port = DEFAULT_PORT;
    if(udp_server_data.buf_size == 0)
        udp_server_data.buf_size = SERVER_BUF_SIZE;

    printf("\n\rUDP: Start UDP server!");
	udp_server_func(udp_server_data);

#if defined(INCLUDE_uxTaskGetStackHighWaterMark) && (INCLUDE_uxTaskGetStackHighWaterMark == 1)
	printf("\n\rMin available stack size of %s = %d * %d bytes", __FUNCTION__, uxTaskGetStackHighWaterMark(NULL), sizeof(portBASE_TYPE));
#endif

	printf("\n\rUDP: UDP server stopped!");
	g_udp_server_task = NULL;
	vTaskDelete(NULL);	
}

uint64_t km_parser(char *buf, int len)
{
    uint64_t ret=0;
    int keyword_num=0;
    char num_str[17] = "\0";
    uint64_t num;

    if(len>16)
        return ret;

    while((buf[keyword_num] != '\0')&&(keyword_num<len)){
        if((buf[keyword_num] == 'k')||(buf[keyword_num] == 'K')){
            strncpy(num_str,buf,keyword_num);
            num = strtol(num_str,NULL,10);
            ret = num * KB;
            break;
        }
        else if((buf[keyword_num] == 'm')||(buf[keyword_num] == 'M')){
            strncpy(num_str,buf,keyword_num);
            num = strtol(num_str,NULL,10);
            ret = num * MB;
            break;
        }
        keyword_num++;
        if(keyword_num == len){
            strncpy(num_str,buf,keyword_num);
            num = strtol(num_str,NULL,10);
            ret = num;
            break;
        }
    }
    return ret;
}

void cmd_tcp(int argc, char **argv)
{
    int argv_count = 2;

	if(argc < 2)
	    goto Exit;

	g_tcp_terminate = 0;

	while(argv_count<=argc){
	    //first operation
	    if(argv_count == 2){
	        if(strcmp(argv[argv_count-1], "-s") == 0){
                if(g_tcp_server_task){
                    printf("\n\rTCP: TCP Server is already running.");
                    return;
                }else{
                    memset(&tcp_server_data,0,sizeof(struct iperf_data_t));
                    tcp_server_data.start = 1;
                    argv_count++;
                }
	        }
	        else if(strcmp(argv[argv_count-1], "stop") == 0){
	            if(argc == 2){
	                vTaskDelay(100);
	                g_tcp_terminate = 1;
	                tcp_server_data.start = 0;
                    tcp_client_data.start = 0;

                    if(g_tcp_server_task){
                    	if(tcp_server_data.server_fd >=0){
                    		close(tcp_server_data.server_fd);
                    	}
                    	if(tcp_server_data.client_fd >=0){
							close(tcp_server_data.client_fd);
						}
                    	printf("\n\rTCP server stopped!\n");
                    	vTaskDelete(g_tcp_server_task);
                    	g_tcp_server_task = NULL;
                    }

                    return;
	            }
	            else{
	                goto Exit;
	            }
	        }
	        else if(strcmp(argv[argv_count-1], "-c") == 0){
	            if(g_tcp_client_task){
                    printf("\n\rTCP: TCP client is already running. Please enter \"ATWT=stop\" to stop it.");
                    return;
                }else{
                    if(argc < (argv_count+1))
                        goto Exit;
                    memset(&tcp_client_data,0,sizeof(struct iperf_data_t));
                    tcp_client_data.start = 1;
                    strncpy(tcp_client_data.server_ip, argv[2], (strlen(argv[2])>16)?16:strlen(argv[2]));
                    argv_count+=2;
                }
	        }
	        else{
	            goto Exit;
	        }
	    }
	    else{
	        if(strcmp(argv[argv_count-1], "-t") == 0){
	            if(argc < (argv_count+1))
                    goto Exit;
                if(tcp_client_data.start){
                    tcp_client_data.time = atoi(argv[argv_count]);
                }
                else{
                    goto Exit;
                }
                argv_count+=2;
            }
	        else if(strcmp(argv[argv_count-1], "-p") == 0){
                if(argc < (argv_count+1))
                    goto Exit;
                if(tcp_server_data.start){
                    tcp_server_data.port = (uint16_t) atoi(argv[argv_count]);
                }
                else if(tcp_client_data.start){
                    tcp_client_data.port = (uint16_t) atoi(argv[argv_count]);
                }
                else{
                    goto Exit;
                }
                argv_count+=2;
            }
	        else if(strcmp(argv[argv_count-1], "-n") == 0){
                if(argc < (argv_count+1))
                    goto Exit;
                if(tcp_client_data.start){
                    tcp_client_data.total_size = km_parser(argv[argv_count],strlen(argv[argv_count]));
                }
                else{
                    goto Exit;
                }
                argv_count+=2;
            }
	        else if(strcmp(argv[argv_count-1], "-d") == 0){
				if(tcp_client_data.start){
					tcp_client_data.bandwidth = DEFAULT_TCP_BANDWIDTH;
				}
				else{
					goto Exit;
				}
				argv_count+=2;
			}
	        else if(strcmp(argv[argv_count-1], "-l") == 0){
                if(argc < (argv_count+1))
                    goto Exit;
                if(tcp_server_data.start){
                    tcp_server_data.buf_size = atoi(argv[argv_count]);
                }
                else if(tcp_client_data.start){
                    tcp_client_data.buf_size = atoi(argv[argv_count]);
                }
                else{
                    goto Exit;
                }
                argv_count+=2;
            }
            else{
                goto Exit;
            }
	    }
    }

	if(tcp_server_data.start && (NULL == g_tcp_server_task)){
		if(xTaskCreate(tcp_server_handler, "tcp_server_handler", BSD_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1 + PRIORITIE_OFFSET, &g_tcp_server_task) != pdPASS)
			printf("\n\rTCP ERROR: Create TCP server task failed.");
	}
	if(tcp_client_data.start && (NULL == g_tcp_client_task)){
		if(xTaskCreate(tcp_client_handler, "tcp_client_handler", BSD_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1 + PRIORITIE_OFFSET, &g_tcp_client_task) != pdPASS)
			printf("\n\rTCP ERROR: Create TCP client task failed.");
	}

	return;
Exit:
	printf("\n\r[ATWT] Command format ERROR!\n");
	printf("\n\r[ATWT] Usage: ATWT=[-s|-c,host|stop],[options]\n");
    printf("\n\r   Client/Server:\n");
    printf("  \r     stop           terminate client & server\n");
    printf("  \r     -l    #        length of buffer to read or write (default 1460 Bytes)\n");
    printf("  \r     -p    #        server port to listen on/connect to (default 5001)\n");
    printf("\n\r   Server specific:\n");
    printf("  \r     -s             run in server mode\n");
    printf("\n\r   Client specific:\n");
    printf("  \r     -c    <host>   run in client mode, connecting to <host>\n");
    printf("  \r     -d             Do a bidirectional test simultaneously\n");
    printf("  \r     -t    #        time in seconds to transmit for (default 10 secs)\n");
    printf("  \r     -n    #[KM]    number of bytes to transmit (instead of -t)\n");
    printf("\n\r   Example:\n");
    printf("  \r     ATWT=-s,-p,5002\n");
    printf("  \r     ATWT=-c,192.168.1.2,-t,100,-p,5002\n");
	return;
}

void cmd_udp(int argc, char **argv)
{
    int argv_count = 2;
    uint8_t tos_value = 0;

	if(argc < 2)
	    goto Exit;

	g_udp_terminate = 0;

	while(argv_count<=argc){
        //first operation
        if(argv_count == 2){
            if(strcmp(argv[argv_count-1], "-s") == 0){
                if(g_udp_server_task){
                    printf("\n\rUDP: UDP Server is already running.");
                    return;
                }else{
                    memset(&udp_server_data,0,sizeof(struct iperf_data_t));
                    udp_server_data.start = 1;
                    argv_count++;
                }
            }
            else if(strcmp(argv[argv_count-1], "stop") == 0){
                if(argc == 2){
                    vTaskDelay(100);
                    g_udp_terminate = 1;
                    udp_server_data.start = 0;
                    udp_client_data.start = 0;

                    if(g_udp_server_task){
						if(udp_server_data.server_fd >=0){
							close(udp_server_data.server_fd);
						}
						printf("\n\rUDP server stopped!\n");
						vTaskDelete(g_udp_server_task);
						g_udp_server_task = NULL;
					}

                    return;
                }
                else{
                    goto Exit;
                }
            }
            else if(strcmp(argv[argv_count-1], "-c") == 0){
                if(g_udp_client_task){
                    printf("\n\rUDP: UDP client is already running. Please enter \"ATWU=stop\" to stop it.");
                    return;
                }else{
                    if(argc < (argv_count+1))
                        goto Exit;
                    memset(&udp_client_data,0,sizeof(struct iperf_data_t));
                    udp_client_data.start = 1;
                    strncpy(udp_client_data.server_ip, argv[2], (strlen(argv[2])>16)?16:strlen(argv[2]));
                    argv_count+=2;
                }
            }
            else{
                goto Exit;
            }
        }
        else{
            if(strcmp(argv[argv_count-1], "-t") == 0){
                if(argc < (argv_count+1))
                    goto Exit;
                if(udp_client_data.start){
                    udp_client_data.time = atoi(argv[argv_count]);
                }
                else{
                    goto Exit;
                }
                argv_count+=2;
            }
            else if(strcmp(argv[argv_count-1], "-p") == 0){
                if(argc < (argv_count+1))
                    goto Exit;
                if(udp_server_data.start){
                    udp_server_data.port = (uint16_t) atoi(argv[argv_count]);
                }
                else if(udp_client_data.start){
                    udp_client_data.port = (uint16_t) atoi(argv[argv_count]);
                }
                else{
                    goto Exit;
                }
                argv_count+=2;
            }
            else if(strcmp(argv[argv_count-1], "-n") == 0){
                if(argc < (argv_count+1))
                    goto Exit;
                if(udp_client_data.start){
                    udp_client_data.total_size = km_parser(argv[argv_count],strlen(argv[argv_count]));
                }
                else{
                    goto Exit;
                }
                argv_count+=2;
            }
            else if(strcmp(argv[argv_count-1], "-b") == 0){
                if(argc < (argv_count+1))
                    goto Exit;
                if(udp_client_data.start){
                    udp_client_data.bandwidth = km_parser(argv[argv_count],strlen(argv[argv_count]));
                    udp_client_data.bandwidth = udp_client_data.bandwidth/8;//bits to Bytes
                }
                else{
                    goto Exit;
                }
                argv_count+=2;
            }
            else if(strcmp(argv[argv_count-1], "-d") == 0){
				if(udp_client_data.start){
					udp_client_data.bandwidth = DEFAULT_UDP_BANDWIDTH;
				}
				else{
					goto Exit;
				}
				argv_count+=1;
			}
#if CONFIG_WLAN
            else if(strcmp(argv[argv_count-1], "-S") == 0){ //for wmm test
				if(argc < (argv_count+1))
					goto Exit;
				if(udp_client_data.start){
					if(atoi(argv[argv_count]) >= 0 && atoi(argv[argv_count]) <= 255){
						tos_value = (uint8_t) atoi(argv[argv_count]);
						wext_set_tos_value(WLAN0_NAME, &tos_value);
						udp_client_data.tos_value = tos_value;
					}
					else{
						goto Exit;
					}
				}
				else{
					goto Exit;
				}
				argv_count+=2;
			}
#endif			
            else if(strcmp(argv[argv_count-1], "-l") == 0){
                if(argc < (argv_count+1))
                    goto Exit;
                if(udp_server_data.start){
                    udp_server_data.buf_size = atoi(argv[argv_count]);
                }
                else if(udp_client_data.start){
                    udp_client_data.buf_size = atoi(argv[argv_count]);
                }
                else{
                    goto Exit;
                }
                argv_count+=2;
            }
            else{
                goto Exit;
            }
        }
    }

	if(udp_server_data.start && (NULL == g_udp_server_task)){
		if(xTaskCreate(udp_server_handler, "udp_server_handler", BSD_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1 + PRIORITIE_OFFSET, &g_udp_server_task) != pdPASS)
			printf("\r\nUDP ERROR: Create UDP server task failed.");
	}
	
	if(udp_client_data.start && (NULL == g_udp_client_task)){
		if(xTaskCreate(udp_client_handler, "udp_client_handler", BSD_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1 + PRIORITIE_OFFSET, &g_udp_client_task) != pdPASS)
			printf("\r\nUDP ERROR: Create UDP client task failed.");
	}

	return;

Exit:
    printf("\n\r[ATWU] Command format ERROR!\n");
	printf("\n\r[ATWU] Usage: ATWU=[-s|-c,host|stop][options]\n");
	printf("\n\r   Client/Server:\n");
	printf("  \r     stop           terminate client & server\n");
	printf("  \r     -l    #        length of buffer to read or write (default 1460 Bytes)\n");
	printf("  \r     -p    #        server port to listen on/connect to (default 5001)\n");
	printf("\n\r   Server specific:\n");
	printf("  \r     -s             run in server mode\n");
	printf("\n\r   Client specific:\n");
	printf("  \r     -b    #[KM]    for UDP, bandwidth to send at in bits/sec (default 1 Mbit/sec)\n");
	printf("  \r     -c    <host>   run in client mode, connecting to <host>\n");
	printf("  \r     -d             Do a bidirectional test simultaneously\n");
	printf("  \r     -t    #        time in seconds to transmit for (default 10 secs)\n");
	printf("  \r     -n    #[KM]    number of bytes to transmit (instead of -t)\n");
#if CONFIG_WLAN
	printf("  \r     -S    #        set the IP 'type of service'\n");
#endif
	printf("\n\r   Example:\n");
	printf("  \r     ATWU=-s,-p,5002\n");
	printf("  \r     ATWU=-c,192.168.1.2,-t,100,-p,5002\n");
	return;
}
