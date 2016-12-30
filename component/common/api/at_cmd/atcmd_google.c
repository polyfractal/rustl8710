#include <lwip_netconf.h>
#include <stdio.h>
#include "log_service.h"
#include "cmsis_os.h"
#include <platform/platform_stdlib.h>
#include <lwip/sockets.h>
#include <lwip/tcpip.h>
#include "wifi_conf.h"
#include "google/google_nest.h"
#include <cJSON.h>
#include <platform_opts.h>

#define GN_PORT 443


#define	_AT_GOOGLE_NEST_		"ATG0"

//functions that using Google Nest's API
void google_data_retrieve_cb(char *response_buf);

void googlenest_get(char *host_addr, char *host_file)
{
	unsigned char buffer[512];
	googlenest_context googlenest;
	char *googlenest_host = host_addr;
	char *googlenest_uri = host_file;


	memset(&googlenest, 0, sizeof(googlenest_context));
	if(gn_connect(&googlenest, googlenest_host, GN_PORT) == 0) {
		if(gn_get(&googlenest, googlenest_uri, buffer, sizeof(buffer)) == 0)
			printf("\r\n\r\nGet data from googlenest: %s", buffer);
		gn_close(&googlenest);
	
	}
}

void google_data_retrieve_cb(char *response_buf) {
	printf("\r\nResponse_buf:\r\n%s\r\n", response_buf);

}

void googlenest_stream(char *host_addr, char *host_file)
{
	googlenest_context googlenest;
	char *googlenest_host = host_addr;
	char *googlenest_uri = host_file;

	memset(&googlenest, 0, sizeof(googlenest_context));
	if(gn_connect(&googlenest, googlenest_host, GN_PORT) == 0) {
		google_retrieve_data_hook_callback(google_data_retrieve_cb);
		gn_stream(&googlenest, googlenest_uri);
		gn_close(&googlenest);
	
	}
}

void googlenest_delete(char *host_addr, char *host_file)
{
	googlenest_context googlenest;
	char *googlenest_host = host_addr;
	char *googlenest_uri = host_file;


	memset(&googlenest, 0, sizeof(googlenest_context));
	if(gn_connect(&googlenest, googlenest_host, GN_PORT) == 0) {
		if(gn_delete(&googlenest, googlenest_uri) == 0)
		printf("\r\n\r\nDelete the data is successful!");
		gn_close(&googlenest);
	
	}
}

void googlenest_put(char *host_addr, char *host_file, char *data)
{
	googlenest_context googlenest;
	char *googlenest_host = host_addr;
	char *googlenest_uri = host_file;

	memset(&googlenest, 0, sizeof(googlenest_context));
	if(gn_connect(&googlenest, googlenest_host, GN_PORT) == 0) {
		if(gn_put(&googlenest, googlenest_uri, data) == 0)
			printf("\r\n\r\nSaving data in firebase is successful!");
		gn_close(&googlenest);
	}
}

void googlenest_patch(char *host_addr, char *host_file, char *data)
{
	googlenest_context googlenest;
	char *googlenest_host = host_addr;
	char *googlenest_uri = host_file;

	memset(&googlenest, 0, sizeof(googlenest_context));
	if(gn_connect(&googlenest, googlenest_host, GN_PORT) == 0) {
		if(gn_patch(&googlenest, googlenest_uri, data) == 0)
			printf("\r\n\r\nUpdating data in firebase is successful!");
		gn_close(&googlenest);
	}
}

void googlenest_post(char *host_addr, char *host_file, char *data)
{
	googlenest_context googlenest;
	char *googlenest_host = host_addr;
	char *googlenest_uri = host_file;
	unsigned char buffer[64];

	memset(&googlenest, 0, sizeof(googlenest_context));
	if(gn_connect(&googlenest, googlenest_host, GN_PORT) == 0) {
		if(gn_post(&googlenest, googlenest_uri, data, buffer, sizeof(buffer)) == 0)
			printf("\r\n\r\nInserting data to firebase is successful!\r\n\r\nThe unique name for this list of data is: %s", buffer);
		gn_close(&googlenest);
	}
}

void cmd_googlenest(int argc, char **argv)
{	
	if(strcmp(argv[1], "get") == 0) {
		if(argc != 4)
			printf("\n\rUsage: gn get address file");
		else {
			googlenest_get(argv[2], argv[3]);
		}
	}
	else if(strcmp(argv[1], "delete") ==0){
		if(argc != 4)
			printf("\n\rUsage: gn delete address file");
		else {
			googlenest_delete(argv[2], argv[3]);
		}
	}
	else if(strcmp(argv[1], "put") ==0){
		if(argc != 5)
			printf("\n\rUsage: gn put address file data");
		else {
			googlenest_put(argv[2], argv[3], argv[4]);
		}
	}
	else if(strcmp(argv[1], "patch") ==0){
		if(argc != 5)
			printf("\n\rUsage: gn patch address file data");
		else {
			googlenest_patch(argv[2], argv[3], argv[4]);
		}
	}
	else if(strcmp(argv[1], "post") ==0){
		if(argc != 5)
			printf("\n\rUsage: gn post address file data");
		else {
			googlenest_post(argv[2], argv[3], argv[4]);
		}
	}
	else if(strcmp(argv[1], "stream") ==0){
		if(argc != 4)
			printf("\n\rUsage: gn stream address file");
		else {
			googlenest_stream(argv[2], argv[3]);
		}
	}
	else
		printf("\n\rUsage: gn method addr file (data)");
}

//AT Command function

void fATG0(void *arg){
	int argc;
	char *argv[MAX_ARGC] = {0};
        printf("[ATG0]: _AT_WLAN_GOOGLENEST_\n\r");
        if(!arg){
          printf("[ATG0]Usage: ATWG=[method,address,file,data] or ATG0=[method,address,file]\n\r");
          return;
        }
        argv[0] = "gn";
        if((argc = parse_param(arg, argv)) > 1){
          cmd_googlenest(argc, argv);
          } 
        else
          printf("[ATG0]Usage: ATG0=[method,address,file,data] or ATG0=[method,address,file]\n\r");
}

#if CONFIG_GOOGLE_NEST
log_item_t at_google_items[ ] = {

	{"ATG0", fATG0,}

};

void at_google_init(void)
{
	log_service_add_table(at_google_items, sizeof(at_google_items)/sizeof(at_google_items[0]));
}

log_module_init(at_google_init);
#endif
