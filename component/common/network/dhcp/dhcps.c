
#include "dhcps.h"
#include "tcpip.h"

//static struct dhcp_server_state dhcp_server_state_machine;
static uint8_t dhcp_server_state_machine = DHCP_SERVER_STATE_IDLE;
/* recorded the client MAC addr(default sudo mac) */
//static uint8_t dhcps_record_first_client_mac[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
/* recorded transaction ID (default sudo id)*/
static uint8_t dhcp_recorded_xid[4] = {0xff, 0xff, 0xff, 0xff}; 

/* UDP Protocol Control Block(PCB) */
static struct udp_pcb *dhcps_pcb;

static struct ip_addr dhcps_send_broadcast_address;
static struct ip_addr dhcps_local_address;
static struct ip_addr dhcps_local_mask;
static struct ip_addr dhcps_local_gateway;
static struct ip_addr dhcps_network_id;
static struct ip_addr dhcps_subnet_broadcast; 
static struct ip_addr dhcps_allocated_client_address;
static int dhcps_addr_pool_set = 0;
static struct ip_addr dhcps_addr_pool_start;
static struct ip_addr dhcps_addr_pool_end;
#if 0
static struct ip_addr dhcps_owned_first_ip;
static struct ip_addr dhcps_owned_last_ip;
static uint8_t dhcps_num_of_available_ips;
#endif
static struct dhcp_msg *dhcp_message_repository;
static int dhcp_message_total_options_lenth;

/* allocated IP range */  
static struct table  ip_table;
static struct ip_addr client_request_ip;

static uint8_t dhcp_client_ethernet_address[16];
static uint8_t bound_client_ethernet_address[16];

static xSemaphoreHandle dhcps_ip_table_semaphore;

static struct netif * dhcps_netif = NULL;
/**
  * @brief  latch the specific ip in the ip table. 
  * @param  d the specific index
  * @retval None.
  */
#if (!IS_USE_FIXED_IP)
static void mark_ip_in_table(uint8_t d)
{
#if (debug_dhcps)   
  	printf("\r\n mark ip %d\r\n",d);
#endif	
	xSemaphoreTake(dhcps_ip_table_semaphore, portMAX_DELAY);
	if (0 < d && d <= 32) {
		ip_table.ip_range[0] = MARK_RANGE1_IP_BIT(ip_table, d);	
#if (debug_dhcps)		
		printf("\r\n ip_table.ip_range[0] = 0x%x\r\n",ip_table.ip_range[0]);
#endif	
	} else if (32 < d && d <= 64) {
	  	ip_table.ip_range[1] = MARK_RANGE2_IP_BIT(ip_table, (d - 32));
#if (debug_dhcps)	
		printf("\r\n ip_table.ip_range[1] = 0x%x\r\n",ip_table.ip_range[1]);
#endif	
	} else if (64 < d && d <= 96) {
		ip_table.ip_range[2] = MARK_RANGE3_IP_BIT(ip_table, (d - 64));
#if (debug_dhcps)	
		printf("\r\n ip_table.ip_range[2] = 0x%x\r\n",ip_table.ip_range[2]);
#endif	
	} else if (96 < d && d <= 128) {
		ip_table.ip_range[3] = MARK_RANGE4_IP_BIT(ip_table, (d - 96));
#if (debug_dhcps)	
		printf("\r\n ip_table.ip_range[3] = 0x%x\r\n",ip_table.ip_range[3]);
#endif	
	} else {
#if (DHCPS_IP_RANGE_FROM_1_to_255)
	if (128 < d && d <= 160) {
		ip_table.ip_range[4] = MARK_RANGE5_IP_BIT(ip_table, d);	
#if (debug_dhcps)		
		printf("\r\n ip_table.ip_range[4] = 0x%x\r\n",ip_table.ip_range[4]);
#endif	
	} else if (160 < d && d <= 192) {
		ip_table.ip_range[5] = MARK_RANGE6_IP_BIT(ip_table, (d - 160));
#if (debug_dhcps)	
		printf("\r\n ip_table.ip_range[5] = 0x%x\r\n",ip_table.ip_range[5]);
#endif	
	} else if (192 < d && d <= 224) {
		ip_table.ip_range[6] = MARK_RANGE7_IP_BIT(ip_table, (d - 192));
#if (debug_dhcps)	
		printf("\r\n ip_table.ip_range[6] = 0x%x\r\n",ip_table.ip_range[6]);
#endif	
	} else if (224 < d) {
		ip_table.ip_range[7] = MARK_RANGE8_IP_BIT(ip_table, (d - 224));
#if (debug_dhcps)	
		printf("\r\n ip_table.ip_range[7] = 0x%x\r\n",ip_table.ip_range[7]);
#endif	
	}
#else 
		printf("\r\n Request ip over the range(1-128) \r\n");
#endif
	}
	xSemaphoreGive(dhcps_ip_table_semaphore);
	
}
#endif

/**
  * @brief  get one usable ip from the ip table of dhcp server. 
  * @param: None 
  * @retval the usable index which represent the ip4_addr(ip) of allocated ip addr.
  */
#if (!IS_USE_FIXED_IP)
static uint8_t search_next_ip(void)
{       
	uint8_t range_count, offset_count;
	uint8_t start, end;
	uint8_t max_count;
	if(dhcps_addr_pool_set){
		start = (uint8_t)ip4_addr4(&dhcps_addr_pool_start);
		end = (uint8_t)ip4_addr4(&dhcps_addr_pool_end);
	}else{
		start = 0;
		end = 255;
	}
	xSemaphoreTake(dhcps_ip_table_semaphore, portMAX_DELAY);
	for (range_count = 0; range_count < (max_count = (DHCPS_IP_RANGE_FROM_1_to_255 > 0 ? 8 : 4)); range_count++) {
		for (offset_count = 0;offset_count < 32; offset_count++) {
			if ((((ip_table.ip_range[range_count] >> offset_count) & 0x01) == 0) 
				&&(((range_count * 32) + (offset_count + 1)) >= start)
				&&(((range_count * 32) + (offset_count + 1)) <= end)) {
				xSemaphoreGive(dhcps_ip_table_semaphore); 
				return ((range_count * 32) + (offset_count + 1));
			}
		}
	}
	xSemaphoreGive(dhcps_ip_table_semaphore); 
	return 0;
}
#endif

/**
  * @brief  fill in the option field with message type of a dhcp message. 
  * @param  msg_option_base_addr: the addr be filled start.
  *	    message_type: the type code you want to fill in 
  * @retval the start addr of the next dhcp option.
  */
static uint8_t *add_msg_type(uint8_t *msg_option_base_addr, uint8_t message_type)
{
	uint8_t *option_start;
	msg_option_base_addr[0] = DHCP_OPTION_CODE_MSG_TYPE;
	msg_option_base_addr[1] = DHCP_OPTION_LENGTH_ONE;
	msg_option_base_addr[2] = message_type;
	option_start = msg_option_base_addr + 3;
	if (DHCP_MESSAGE_TYPE_NAK == message_type)
		*option_start++ = DHCP_OPTION_CODE_END;		
	return option_start;
}


static uint8_t *fill_one_option_content(uint8_t *option_base_addr,
	uint8_t option_code, uint8_t option_length, void *copy_info)
{
	uint8_t *option_data_base_address;
	uint8_t *next_option_start_address = NULL;
	option_base_addr[0] = option_code;
	option_base_addr[1] = option_length;
	option_data_base_address = option_base_addr + 2;
	switch (option_length) {
	case DHCP_OPTION_LENGTH_FOUR:
		memcpy(option_data_base_address, copy_info, DHCP_OPTION_LENGTH_FOUR);
		next_option_start_address = option_data_base_address + 4;
		break;
	case DHCP_OPTION_LENGTH_TWO:
		memcpy(option_data_base_address, copy_info, DHCP_OPTION_LENGTH_TWO);
		next_option_start_address = option_data_base_address + 2;
		break;
	case DHCP_OPTION_LENGTH_ONE:
		memcpy(option_data_base_address, copy_info, DHCP_OPTION_LENGTH_ONE);
		next_option_start_address = option_data_base_address + 1;
		break;
	}

	return next_option_start_address;
}

/**
  * @brief  fill in the needed content of the dhcp offer message. 
  * @param  optptr  the addr which the tail of dhcp magic field. 
  * @retval the addr represent to add the end of option.
  */
static void add_offer_options(uint8_t *option_start_address)
{
	uint8_t *temp_option_addr;
	/* add DHCP options 1. 
	The subnet mask option specifies the client's subnet mask */
	temp_option_addr = fill_one_option_content(option_start_address,
			DHCP_OPTION_CODE_SUBNET_MASK, DHCP_OPTION_LENGTH_FOUR,
					(void *)&dhcps_local_mask);
	
        /* add DHCP options 3 (i.e router(gateway)). The time server option 
        specifies a list of RFC 868 [6] time servers available to the client. */
	temp_option_addr = fill_one_option_content(temp_option_addr,
			DHCP_OPTION_CODE_ROUTER, DHCP_OPTION_LENGTH_FOUR,
					(void *)&dhcps_local_address);

	/* add DHCP options 6 (i.e DNS). 
        The option specifies a list of DNS servers available to the client. */
	temp_option_addr = fill_one_option_content(temp_option_addr,
			DHCP_OPTION_CODE_DNS_SERVER, DHCP_OPTION_LENGTH_FOUR,
					(void *)&dhcps_local_address);	
	/* add DHCP options 51.
	This option is used to request a lease time for the IP address. */
	temp_option_addr = fill_one_option_content(temp_option_addr,
			DHCP_OPTION_CODE_LEASE_TIME, DHCP_OPTION_LENGTH_FOUR,
					(void *)&dhcp_option_lease_time_one_day);
	/* add DHCP options 54. 
	The identifier is the IP address of the selected server. */
	temp_option_addr = fill_one_option_content(temp_option_addr,
			DHCP_OPTION_CODE_SERVER_ID, DHCP_OPTION_LENGTH_FOUR,
				(void *)&dhcps_local_address);
	/* add DHCP options 28. 
	This option specifies the broadcast address in use on client's subnet.*/
	temp_option_addr = fill_one_option_content(temp_option_addr,
		DHCP_OPTION_CODE_BROADCAST_ADDRESS, DHCP_OPTION_LENGTH_FOUR,
				(void *)&dhcps_subnet_broadcast);
	/* add DHCP options 26. 
	This option specifies the Maximum transmission unit to use */
	temp_option_addr = fill_one_option_content(temp_option_addr,
		DHCP_OPTION_CODE_INTERFACE_MTU, DHCP_OPTION_LENGTH_TWO,
					(void *) &dhcp_option_interface_mtu_576);
	/* add DHCP options 31.
	This option specifies whether or not the client should solicit routers */
	temp_option_addr = fill_one_option_content(temp_option_addr,
		DHCP_OPTION_CODE_PERFORM_ROUTER_DISCOVERY, DHCP_OPTION_LENGTH_ONE,
								NULL);
	*temp_option_addr++ = DHCP_OPTION_CODE_END;

}


/**
  * @brief  fill in common content of a dhcp message.  
  * @param  m the pointer which point to the dhcp message store in.
  * @retval None.
  */
static void dhcps_initialize_message(struct dhcp_msg *dhcp_message_repository, struct ip_addr yiaddr)
{
     
        dhcp_message_repository->op = DHCP_MESSAGE_OP_REPLY;
        dhcp_message_repository->htype = DHCP_MESSAGE_HTYPE;
        dhcp_message_repository->hlen = DHCP_MESSAGE_HLEN; 
        dhcp_message_repository->hops = 0;		
        memcpy((char *)dhcp_recorded_xid, (char *) dhcp_message_repository->xid,
					sizeof(dhcp_message_repository->xid));
        dhcp_message_repository->secs = 0;
        dhcp_message_repository->flags = htons(BOOTP_BROADCAST);         

	memcpy((char *)dhcp_message_repository->yiaddr,
			(char *)&yiaddr,
				sizeof(dhcp_message_repository->yiaddr));
        
	memset((char *)dhcp_message_repository->ciaddr, 0,
					sizeof(dhcp_message_repository->ciaddr));
        memset((char *)dhcp_message_repository->siaddr, 0,
					sizeof(dhcp_message_repository->siaddr));
        memset((char *)dhcp_message_repository->giaddr, 0,
					sizeof(dhcp_message_repository->giaddr));
        memcpy((char *)dhcp_message_repository->chaddr, &dhcp_client_ethernet_address,
					sizeof(dhcp_message_repository->chaddr));
        memset((char *)dhcp_message_repository->sname,  0,
					sizeof(dhcp_message_repository->sname));
        memset((char *)dhcp_message_repository->file,   0,
					sizeof(dhcp_message_repository->file));
        memset((char *)dhcp_message_repository->options, 0,
					dhcp_message_total_options_lenth);
        memcpy((char *)dhcp_message_repository->options, (char *)dhcp_magic_cookie,
					sizeof(dhcp_magic_cookie));
}

/**
  * @brief  init and fill in  the needed content of dhcp offer message.  
  * @param  packet_buffer packet buffer for UDP.
  * @retval None.
  */
static void dhcps_send_offer(struct pbuf *packet_buffer)
{
	uint8_t temp_ip = 0;
	dhcp_message_repository = (struct dhcp_msg *)packet_buffer->payload;	
#if (!IS_USE_FIXED_IP)
	if ((ip4_addr4(&dhcps_allocated_client_address) != 0) &&
		(memcmp((void *)&dhcps_allocated_client_address, (void *)&client_request_ip, 4) == 0) &&
		(memcmp((void *)&bound_client_ethernet_address, (void *)&dhcp_client_ethernet_address, 16) == 0)) {
		temp_ip = (uint8_t) ip4_addr4(&client_request_ip);
	} else if ((ip4_addr1(&client_request_ip) == ip4_addr1(&dhcps_network_id)) &&
		(ip4_addr2(&client_request_ip) == ip4_addr2(&dhcps_network_id)) &&
		(ip4_addr3(&client_request_ip) == ip4_addr3(&dhcps_network_id))) {
		uint8_t request_ip4 = (uint8_t) ip4_addr4(&client_request_ip);
		if ((request_ip4 != 0) && (((request_ip4 - 1) / 32) >= 0) && (((request_ip4 - 1) / 32) <= 3) &&
			(((ip_table.ip_range[(request_ip4 - 1) / 32] >> ((request_ip4 - 1) % 32)) & 0x01) == 0)) {
			temp_ip = request_ip4;
		}
	}

	/* create new client ip */
	if(temp_ip == 0) temp_ip = search_next_ip();
#if (debug_dhcps)	
	printf("\r\n temp_ip = %d",temp_ip);
#endif	
	if (temp_ip == 0) {
#if 0	
	  	memset(&ip_table, 0, sizeof(struct table));
		mark_ip_in_table((uint8_t)ip4_addr4(&dhcps_local_address));
		printf("\r\n reset ip table!!\r\n");	
#endif	
		printf("\r\n No useable ip!!!!\r\n");
	}
	
	IP4_ADDR(&dhcps_allocated_client_address, (ip4_addr1(&dhcps_network_id)),
		ip4_addr2(&dhcps_network_id), ip4_addr3(&dhcps_network_id), temp_ip);
	memcpy(bound_client_ethernet_address, dhcp_client_ethernet_address, sizeof(bound_client_ethernet_address));
#endif   
        dhcps_initialize_message(dhcp_message_repository, dhcps_allocated_client_address);
        add_offer_options(add_msg_type(&dhcp_message_repository->options[4],
						DHCP_MESSAGE_TYPE_OFFER));
        udp_sendto_if(dhcps_pcb, packet_buffer,
		   &dhcps_send_broadcast_address, DHCP_CLIENT_PORT, dhcps_netif);
}

/**
  * @brief  init and fill in  the needed content of dhcp nak message.  
  * @param  packet buffer packet buffer for UDP.
  * @retval None.
  */
static void dhcps_send_nak(struct pbuf *packet_buffer)
{
	struct ip_addr zero_address;
	IP4_ADDR(&zero_address, 0, 0, 0, 0);

	dhcp_message_repository = (struct dhcp_msg *)packet_buffer->payload;
        dhcps_initialize_message(dhcp_message_repository, zero_address);
        add_msg_type(&dhcp_message_repository->options[4], DHCP_MESSAGE_TYPE_NAK);
        udp_sendto_if(dhcps_pcb, packet_buffer,
		   &dhcps_send_broadcast_address, DHCP_CLIENT_PORT, dhcps_netif);
}

/**
  * @brief  init and fill in  the needed content of dhcp ack message.  
  * @param  packet buffer packet buffer for UDP.
  * @retval None.
  */
static void dhcps_send_ack(struct pbuf *packet_buffer)
{
        dhcp_message_repository = (struct dhcp_msg *)packet_buffer->payload;
        dhcps_initialize_message(dhcp_message_repository, dhcps_allocated_client_address);
        add_offer_options(add_msg_type(&dhcp_message_repository->options[4],
				      			DHCP_MESSAGE_TYPE_ACK));
        udp_sendto_if(dhcps_pcb, packet_buffer,
		   &dhcps_send_broadcast_address, DHCP_CLIENT_PORT, dhcps_netif);
}

/**
  * @brief  according by the input message type to reflect the correspond state.  
  * @param  option_message_type the input server state
  * @retval the server state which already transfer to.
  */
uint8_t dhcps_handle_state_machine_change(uint8_t option_message_type)
{
	switch (option_message_type) {
	case DHCP_MESSAGE_TYPE_DECLINE:
		dhcp_server_state_machine = DHCP_SERVER_STATE_IDLE;
		break;
	case DHCP_MESSAGE_TYPE_DISCOVER:
		if (dhcp_server_state_machine == DHCP_SERVER_STATE_IDLE) {
			dhcp_server_state_machine = DHCP_SERVER_STATE_OFFER;
		}
		break;
	case DHCP_MESSAGE_TYPE_REQUEST:

#if (!IS_USE_FIXED_IP) 	
		if (dhcp_server_state_machine == DHCP_SERVER_STATE_OFFER) {
			if (ip4_addr4(&dhcps_allocated_client_address) != 0) { 
				if (memcmp((void *)&dhcps_allocated_client_address, (void *)&client_request_ip, 4) == 0) {  	
					dhcp_server_state_machine = DHCP_SERVER_STATE_ACK;
			  	} else {
				  	dhcp_server_state_machine = DHCP_SERVER_STATE_NAK;
			  	}
			} else {
			  	dhcp_server_state_machine = DHCP_SERVER_STATE_NAK;
			}
		} else if (dhcp_server_state_machine == DHCP_SERVER_STATE_IDLE) {
			if ((ip4_addr4(&dhcps_allocated_client_address) != 0) &&
				(memcmp((void *)&dhcps_allocated_client_address, (void *)&client_request_ip, 4) == 0) &&
				(memcmp((void *)&bound_client_ethernet_address, (void *)&dhcp_client_ethernet_address, 16) == 0)) {
				dhcp_server_state_machine = DHCP_SERVER_STATE_ACK;
			} else if ((ip4_addr1(&client_request_ip) == ip4_addr1(&dhcps_network_id)) &&
				(ip4_addr2(&client_request_ip) == ip4_addr2(&dhcps_network_id)) &&
				(ip4_addr3(&client_request_ip) == ip4_addr3(&dhcps_network_id))) {
				uint8_t request_ip4 = (uint8_t) ip4_addr4(&client_request_ip);
				if ((request_ip4 != 0) && (((request_ip4 - 1) / 32) >= 0) && (((request_ip4 - 1) / 32) <= 3) &&
					(((ip_table.ip_range[(request_ip4 - 1) / 32] >> ((request_ip4 - 1) % 32)) & 0x01) == 0)) {
					IP4_ADDR(&dhcps_allocated_client_address, (ip4_addr1(&dhcps_network_id)),
						ip4_addr2(&dhcps_network_id), ip4_addr3(&dhcps_network_id), request_ip4);
					memcpy(bound_client_ethernet_address, dhcp_client_ethernet_address, sizeof(bound_client_ethernet_address));
					dhcp_server_state_machine = DHCP_SERVER_STATE_ACK;
				} else {
					dhcp_server_state_machine = DHCP_SERVER_STATE_NAK;
				}
			} else {
				dhcp_server_state_machine = DHCP_SERVER_STATE_NAK;
			}
		} else {
			dhcp_server_state_machine = DHCP_SERVER_STATE_NAK;
		}
#else		
		if (!(dhcp_server_state_machine == DHCP_SERVER_STATE_ACK ||
			dhcp_server_state_machine == DHCP_SERVER_STATE_NAK)) {
		        dhcp_server_state_machine = DHCP_SERVER_STATE_NAK;
		}
#endif
		break;
	case DHCP_MESSAGE_TYPE_RELEASE:
		dhcp_server_state_machine = DHCP_SERVER_STATE_IDLE;
		break;
	}

	return dhcp_server_state_machine;
}
/**
  * @brief  parse the dhcp message option part.
  * @param  optptr: the addr of the first option field. 
  *         len: the total length of all option fields.          
  * @retval dhcp server state.
  */
static uint8_t dhcps_handle_msg_options(uint8_t *option_start, int16_t total_option_length)
{
       
	int16_t option_message_type = 0;
        uint8_t *option_end = option_start + total_option_length;
        //dhcp_server_state_machine = DHCP_SERVER_STATE_IDLE;
	
        /* begin process the dhcp option info */
        while (option_start < option_end) {	
                switch ((uint8_t)*option_start) {
                case DHCP_OPTION_CODE_MSG_TYPE: 
                        option_message_type = *(option_start + 2); // 2 => code(1)+lenth(1)
                        break;
                case DHCP_OPTION_CODE_REQUEST_IP_ADDRESS : 
#if IS_USE_FIXED_IP
			if (memcmp((char *)&dhcps_allocated_client_address,
						(char *)option_start + 2, 4) == 0)
			  	dhcp_server_state_machine = DHCP_SERVER_STATE_ACK;
			else 
				dhcp_server_state_machine = DHCP_SERVER_STATE_NAK;
#else                   		
			memcpy((char *)&client_request_ip, (char *)option_start + 2, 4);	
#endif
                      	break;
                } 
                // calculate the options offset to get next option's base addr
                option_start += option_start[1] + 2; // optptr[1]: length value + (code(1)+ Len(1))
        }
	return dhcps_handle_state_machine_change(option_message_type);        
}

/**
  * @brief  get message from buffer then check whether it is dhcp related or not.
  *         if yes , parse it more to undersatnd the client's request.
  * @param  same as recv callback function definition
  * @retval if message is dhcp related then return dhcp server state,
  *	    otherwise return 0
  */
static uint8_t dhcps_check_msg_and_handle_options(struct pbuf *packet_buffer)
{
	int dhcp_message_option_offset;
	dhcp_message_repository = (struct dhcp_msg *)packet_buffer->payload;
	memcpy(dhcp_client_ethernet_address, dhcp_message_repository->chaddr, sizeof(dhcp_client_ethernet_address));
	dhcp_message_option_offset = ((int)dhcp_message_repository->options 
						- (int)packet_buffer->payload);
	dhcp_message_total_options_lenth = (packet_buffer->len 
						- dhcp_message_option_offset);
	/* check the magic number,if correct parse the content of options */
	if (memcmp((char *)dhcp_message_repository->options,
		(char *)dhcp_magic_cookie, sizeof(dhcp_magic_cookie)) == 0) {
            	return dhcps_handle_msg_options(&dhcp_message_repository->options[4],
            				(dhcp_message_total_options_lenth - 4));
	}
        
	return 0;
}


/**
  * @brief  handle imcoming dhcp message and response message to client 
  * @param  same as recv callback function definition
  * @retval None
  */
static void dhcps_receive_udp_packet_handler(void *arg, struct udp_pcb *udp_pcb,
struct pbuf *udp_packet_buffer, struct ip_addr *sender_addr, uint16_t sender_port)
{	
  	int16_t total_length_of_packet_buffer;
	struct pbuf *merged_packet_buffer = NULL;
	
	dhcp_message_repository = (struct dhcp_msg *)udp_packet_buffer->payload;
	if (udp_packet_buffer == NULL) {
		printf("\n\r Error!!!! System doesn't allocate any buffer \n\r");
		return;  
	}
	if (sender_port == DHCP_CLIENT_PORT) {
		total_length_of_packet_buffer = udp_packet_buffer->tot_len;
		if (udp_packet_buffer->next != NULL) {
			merged_packet_buffer = pbuf_coalesce(udp_packet_buffer,
								PBUF_TRANSPORT);
			if (merged_packet_buffer->tot_len !=
						total_length_of_packet_buffer) {
				pbuf_free(udp_packet_buffer);	
				return;
			}
		}
		switch (dhcps_check_msg_and_handle_options(udp_packet_buffer)) {
		case  DHCP_SERVER_STATE_OFFER:
			dhcps_send_offer(udp_packet_buffer);
			break;
		case DHCP_SERVER_STATE_ACK:
			dhcps_send_ack(udp_packet_buffer);
#if (!IS_USE_FIXED_IP)
			mark_ip_in_table((uint8_t)ip4_addr4(&dhcps_allocated_client_address)); 			
#endif
			dhcp_server_state_machine = DHCP_SERVER_STATE_IDLE;
			break;
		case DHCP_SERVER_STATE_NAK:
			dhcps_send_nak(udp_packet_buffer);
			dhcp_server_state_machine = DHCP_SERVER_STATE_IDLE;
			break;
		case DHCP_OPTION_CODE_END:
			break;
		}
        }
	
	/* free the UDP connection, so we can accept new clients */
        udp_disconnect(udp_pcb);
        
	/* Free the packet buffer */   
	if (merged_packet_buffer != NULL)
		pbuf_free(merged_packet_buffer);
	else 
		pbuf_free(udp_packet_buffer);
}

void dhcps_set_addr_pool(int addr_pool_set, struct ip_addr * addr_pool_start, struct ip_addr *addr_pool_end)
{
	if(addr_pool_set){
		dhcps_addr_pool_set = 1;

		memcpy(&dhcps_addr_pool_start, addr_pool_start,
							sizeof(struct ip_addr));

		memcpy(&dhcps_addr_pool_end, addr_pool_end,
							sizeof(struct ip_addr));
	}else{
		dhcps_addr_pool_set = 0;
	}
}
/** 
  * @brief  Initialize dhcp server.
  * @param  None.
  * @retval None.
  * Note, for now,we assume the server latch ip 192.168.1.1 and support dynamic 
  *       or fixed IP allocation. 
  */
void dhcps_init(struct netif * pnetif)
{	
//	printf("dhcps_init,wlan:%c\n\r",pnetif->name[1]);

	dhcps_netif = pnetif;

	if (dhcps_pcb != NULL) {
		udp_remove(dhcps_pcb);
		dhcps_pcb = NULL;	
	}

        dhcps_pcb = udp_new(); 
	if (dhcps_pcb == NULL) {
		printf("\n\r Error!!!upd_new error \n\r");
		return;
	}
        IP4_ADDR(&dhcps_send_broadcast_address, 255, 255, 255, 255);
	/* get net info from net interface */
	
        memcpy(&dhcps_local_address, &pnetif->ip_addr,
        						sizeof(struct ip_addr));
	memcpy(&dhcps_local_mask, &pnetif->netmask,
							sizeof(struct ip_addr));

	memcpy(&dhcps_local_gateway, &pnetif->gw,
							sizeof(struct ip_addr));

	/* calculate the usable network ip range */
	dhcps_network_id.addr = ((pnetif->ip_addr.addr) &
					(pnetif->netmask.addr));
	
	dhcps_subnet_broadcast.addr = ((dhcps_network_id.addr |
					~(pnetif->netmask.addr)));
#if 0	
	dhcps_owned_first_ip.addr = htonl((ntohl(dhcps_network_id.addr) + 1));
	dhcps_owned_last_ip.addr = htonl(ntohl(dhcps_subnet_broadcast.addr) - 1);
	dhcps_num_of_available_ips = ((ntohl(dhcps_owned_last_ip.addr) 
				- ntohl(dhcps_owned_first_ip.addr)) + 1); 
#endif

#if IS_USE_FIXED_IP
        IP4_ADDR(&dhcps_allocated_client_address, ip4_addr1(&dhcps_local_address)
		, ip4_addr2(&dhcps_local_address), ip4_addr3(&dhcps_local_address),
					(ip4_addr4(&dhcps_local_address)) + 1 );
#else
	if (dhcps_ip_table_semaphore != NULL) {	
		vSemaphoreDelete(dhcps_ip_table_semaphore);
		dhcps_ip_table_semaphore = NULL;
	}
	dhcps_ip_table_semaphore = xSemaphoreCreateMutex();

	//dhcps_ip_table = (struct ip_table *)(pvPortMalloc(sizeof(struct ip_table)));
	memset(&ip_table, 0, sizeof(struct table));
	memset(&dhcps_allocated_client_address, 0, sizeof(struct ip_addr));
	memset(bound_client_ethernet_address, 0, sizeof(bound_client_ethernet_address));
	mark_ip_in_table((uint8_t)ip4_addr4(&dhcps_local_address));
	mark_ip_in_table((uint8_t)ip4_addr4(&dhcps_local_gateway));
#if 0
	for (i = 1; i < ip4_addr4(&dhcps_local_address); i++) {
		mark_ip_in_table(i);
	}
#endif	
#endif
        udp_bind(dhcps_pcb, IP_ADDR_ANY, DHCP_SERVER_PORT);
        udp_recv(dhcps_pcb, dhcps_receive_udp_packet_handler, NULL);
}

void dhcps_deinit(void)
{
	if (dhcps_pcb != NULL) {
		udp_remove(dhcps_pcb);
		dhcps_pcb = NULL;	
	}
	if (dhcps_ip_table_semaphore != NULL) {	
		vSemaphoreDelete(dhcps_ip_table_semaphore);
		dhcps_ip_table_semaphore = NULL;
	}		
}
