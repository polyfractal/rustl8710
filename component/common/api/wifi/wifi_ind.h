#ifndef _WIFI_INDICATE_H
#define _WIFI_INDICATE_H
#include "wifi_conf.h"  

typedef void (*rtw_event_handler_t)(char *buf, int buf_len, int flags, void* handler_user_data );

typedef struct
{
//	WIFI_EVENT_INDICATE	event_cmd;
	rtw_event_handler_t	handler;
	void*	handler_user_data;
} event_list_elem_t;

void init_event_callback_list(void);
extern void wifi_indication( WIFI_EVENT_INDICATE event, char *buf, int buf_len, int flags);
/** Register the event listener
 *
 * @param[in] event_cmds : The event command number indicated
 * @param[in] handler_func : the callback function which will 
 *  			  receive and process the event
 * @param[in] handler_user_data : user specific data that will be 
 *  			   passed directly to the callback function
 *  
 * @note : Set the same event_cmds with empty handler_func will
 *  	 unregister the event_cmds
 *  
 * @return    RTW_SUCCESS : if successfully registers the event 
 *  		  RTW_ERROR : if an error occurred
 */
extern void wifi_reg_event_handler(unsigned int event_cmds, rtw_event_handler_t handler_func, void *handler_user_data);
extern void wifi_unreg_event_handler(unsigned int event_cmds, rtw_event_handler_t handler_func);

#endif //_WIFI_INDICATE_H

