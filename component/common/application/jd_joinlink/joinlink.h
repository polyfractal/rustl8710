/******************************* joinlink **************************/
#ifndef __JOINLINK_H
#define __JOINLINK_H

#include "autoconf.h"
#include "platform_stdlib.h"
#include "wifi_conf.h"
#include "wifi_structures.h"
#include "osdep_service.h"
#include "lwip_netconf.h"
#include "task.h"
#include "hal_crypto.h"

#define SSID_SWITCH_TIME    500  //ssid switch time in phase1,units:ms, 50
#define CHANNEL_SWITCH_TIME 500	//channel switch time in phase2,units:ms, 50
#define JOINLINK_TIME       120  //timeout for joinlink process, units: s

/*
 * store AP profile after successfully decode
 * SUM +£¨length£¬pass£©+£¨IP+Port£©+£¨length£¬SSID)
 */
typedef struct
{
	unsigned char sum;
	unsigned char pwd_length;
	char pwd[65];						
  int source_ip[4];
  unsigned int source_port;
  unsigned char ssid_length;
	char ssid[65];						
} joinlink_result_t;

/*
 * return value of joinlink_recv()
 */
typedef enum
{
	JOINLINK_STATUS_CONTINUE = 0,
	JOINLINK_STATUS_CHANNEL_LOCKED = 1,
	JOINLINK_STATUS_COMPLETE = 2
} joinlink_status_t;

//initialize the related data structure
int joinlink_init();
/*
  handler to decode pkt
 */
joinlink_status_t joinlink_recv(unsigned char *da, unsigned char *sa, int len, void *user_data);

/*
 * get the AP profile after decode
 */
int joinlink_get_result(joinlink_result_t *result);

/*
 * set the aes_key, the max len should be 16
 * ret 1: success; ret 0: the len is invalid;
 */ 
int set_aes_key(char *key, int len);

// call this after finish join_link process
void joinlink_deinit();

#endif //__JOINLINK_H	
