/******************************* joinlink **************************/
//includes
#include "joinlink.h"

// macro
#define NUM_MCAST                 53  // the max len of pkt in mcast, original: 13
#define NUM_BCAST                 36  // the max number of index of bcast
#define HEAD_LEN                  9   // sum(1 byte) + pwd_len(1 byte) + port(2 byte) + ip(4 byte) + ssid_len(1 byte) 
#define NUM_IDX                   10  // number of index in bcast
#define NUM_PKT			              4   // number of packets for every index
#define SEQ_INCREMENT_ONE_BCAST   1   // only the increment of 1 in seq of pkt is accepted

static char smac[6];
static u8 decoded_state = 0;
static int joinlink_state_mcast = 0;
static int joinlink_state_bcast = 0;
static u8 sync_label_mcast = 0;
static u8 version_mcast = 0;
// every pkt has two byte
static u8 *raw_data_mcast = NULL;
static u8 *decrypted_data_mcast = NULL;
static u8 count_mcast = 0;
static u8 sum_mcast = 0; // the total len of ssid and pwd
static char pass_len = -1;
static u8 ssid_len = 0;
static u8 ssid_offset = 0;
static u8 odd_check = 0;
static u8 total_len_mcast = 0;
static u8 *recved_flag_mcast = NULL;
static u8 range_mcast[NUM_MCAST >> 3]; // the range for aes decryption
static u8 decryp_flag_mcast[NUM_MCAST >> 3];

static u8 sync_label_bcast = 0;
static u8 version_bcast_ready = 0;
static u8 version_bcast = 0;
static u8 count_in_idx_bcast = 0;
static u8 count_decoded_bcast = 0;
static unsigned short seq_now_bcast = 0;
static u8 locked_bssid_bcast[6];
static u8 ssid_offset_bcast = 0;

// for data phase in bcast
// 0: wating index pkt, 1: waiting info pkt
// TODO: need to fix bssid/ssid for bcast to filter unnecessary pkt
static u8 data_phase_state_bcast = 0;
static u8 *raw_data_bcast = NULL;
static u8 *decrypted_data_bcast = NULL;

static u8 version_CRC = 0;
static u8 *decoded_flag_bcast = NULL;
static u8 current_idx_bcast = 0;

static u8 sum_bcast = 0; 
static char pass_len_bcast = -1;
static u8 ssid_len_bcast = 0;
static u8 fc_version_bcast = 0;
static u8 fc_data_bcast = 0;
static u8 idx_CRC = 0;
static u8 idx_data = 0;

//store decode result of AP profile
joinlink_result_t *AP_profile = NULL;

// AES decryption related
static u8 aes_iv[16];
static u8 aes_key[16];
static u8 ssid_range_mcast = 255;
static u8 decryp_data_buf[16];
/*
ret: 0, success, -1, failure
*/
int joinlink_init()
{
	decoded_state = 0;
	joinlink_state_mcast = 0;
	joinlink_state_bcast = 0;
	sync_label_mcast = 0;
	version_mcast = 0;
	
  raw_data_mcast = (u8 *)malloc(NUM_MCAST*2);
  decrypted_data_mcast = (u8 *)malloc(NUM_MCAST*2);
  recved_flag_mcast = (u8 *)malloc(NUM_MCAST);
  raw_data_bcast = (u8 *)malloc(NUM_BCAST*NUM_PKT);
  decrypted_data_bcast = (u8 *)malloc(NUM_BCAST*NUM_PKT);
  decoded_flag_bcast = (u8 *)malloc(NUM_BCAST);
  AP_profile = (joinlink_result_t *)malloc(sizeof(joinlink_result_t));
  
  if(!raw_data_mcast || !decrypted_data_mcast || !recved_flag_mcast||
  	 !raw_data_bcast || !decrypted_data_bcast || !decoded_flag_bcast||
  	 !AP_profile)
  {
  	printf("join_link: malloc memory fail\n");
  	return -1;
  }
  
	memset(raw_data_mcast, 0, NUM_MCAST*2);
  count_mcast = 0;
  sum_mcast = 0;
  pass_len = -1;
  ssid_len = 0;
  ssid_offset = 0;
  odd_check = 0;
  total_len_mcast = 0;
  memset(recved_flag_mcast, 0, NUM_MCAST);
  
  sync_label_bcast = 0;
  version_bcast_ready = 0;
  version_bcast = 0;
  seq_now_bcast = 0;
  memset(locked_bssid_bcast, 0, sizeof(locked_bssid_bcast));
  
  data_phase_state_bcast = 0;
  memset(raw_data_bcast, 0, NUM_BCAST*NUM_PKT);
  version_CRC = 0;
  count_in_idx_bcast = 0;
  count_decoded_bcast = 0;
  memset(decoded_flag_bcast, 0, NUM_BCAST);
  current_idx_bcast = 0;
  sum_bcast = 0; 
  pass_len_bcast = -1;
  ssid_len_bcast = 0;
  fc_version_bcast = 0;
  fc_data_bcast = 0;
  idx_CRC = 0;
  idx_data = 0;
  
  memset(AP_profile, 0, sizeof(joinlink_result_t));
  memset(smac, 0, sizeof(smac));
  
  memset(aes_iv, 0, sizeof(aes_iv));
  memset(aes_key, 0, sizeof(aes_key));
  memset(range_mcast, 0, sizeof(range_mcast));
  memset(decryp_flag_mcast, 0, sizeof(decryp_flag_mcast));
  ssid_range_mcast = 255;
  memset(decrypted_data_mcast, 0, NUM_MCAST*2);
  memset(decryp_data_buf, 0, sizeof(decryp_data_buf));
  memset(decrypted_data_bcast, 0, NUM_BCAST*NUM_PKT);
  ssid_offset_bcast = 0;
  
	return 0;
}

// set the aes_key, the max len should be 16
int set_aes_key(char *key, int len)
{
	if (len <= 0 || len > 16)
		return 0;
		
	memcpy(aes_key, key, len);
	if (rtl_crypto_aes_cbc_init(aes_key, sizeof(aes_key)) != 0) 
  {
		printf("AES CBC init failed\n");
		return 0;
	}
	printf("the AES key is set to %s\n", aes_key);
	return 1;
}
// free memory
void joinlink_deinit()
{
	free(raw_data_mcast);
	free(decrypted_data_mcast);
	free(recved_flag_mcast);
	free(raw_data_bcast);
	free(decrypted_data_bcast);
	free(decoded_flag_bcast);
	free(AP_profile);
	
	raw_data_mcast = NULL;
  decrypted_data_mcast = NULL;
  recved_flag_mcast = NULL;
  raw_data_bcast = NULL;
  decrypted_data_bcast = NULL;
  decoded_flag_bcast = NULL;
  AP_profile = NULL;
  
  return;
}
// restart joinlink when error
static void joinlink_restart()
{
	joinlink_deinit();
	joinlink_init();
	return;
}
/*
ret: 0, failure; 1 true.
*/
static int check_sync_mcast(u8 *da)
{
	if((da[3] == 0) && (da[4] == 1) && (da[5] >= 1) && (da[5] <= 3))
	{
		sync_label_mcast |= 0x01 << (da[5] - 1);
		if(sync_label_mcast == 0x07)
			return 1;
		else
			return 0;
	}
	else
		return 0;	
}
// ret: 0, failure; 1 true
static int check_version_mcast(u8 *da)
{
	// 239.0.{Version}.4
	if((da[3] == 0) && (da[5] == 4))
	{
		version_mcast = da[4];
		return 1;
	}
	else
		return 0;
}

static u8 getCrc(u8 *ptr, u8 len)
{
	u8 crc;
	u8 i;
	crc = 0;
	while (len--)
	{
		crc ^= *ptr++;
		for (i = 0; i < 8; i++)
		{
			if (crc & 0x01)
			{
				crc = (crc >> 1) ^ 0x8C;
			}
			else
				crc >>= 1;
		}
	}
	return crc;
}
// check whether received enough pkt to decrypt
static u8 decryp_ready(u8 range)
{
	int first = (range << 3) + 1;
	u8 count = 0;
	while(count < 8)
	{
		if(!recved_flag_mcast[first + count])
			return 0;
		++count;
	}
	return 1;
}
// ret: 0 suc, ret: -1 failure
static int decryp_data(u8 decryp_range)
{
	// before decryption dump
	memset(decryp_data_buf, 0, sizeof(decryp_data_buf));
	
	// this decrpytion API only accept 16 byte size
	if (rtl_crypto_aes_cbc_decrypt(raw_data_mcast + (decryp_range << 4), 16, aes_iv, sizeof(aes_iv), decryp_data_buf) != 0 ) 
	{
		printf("AES CBC decrypt failed\n");
		return -1;
	}
	memcpy(decrypted_data_mcast + (decryp_range << 4), decryp_data_buf, 16);
	// dump encrypted and decrypted data
#if 0	
	printf("range %d encryp data:", decryp_range);
	for(int i = 0; i < 16; i++)
	  printf("0x%02x ", raw_data_mcast[(decryp_range << 4) + i]);
	printf("\n");  
	
	printf("range %d decrypted to:", decryp_range);
	for(int i = 0; i < 16; i++)
	  printf("0x%02x ", decrypted_data_mcast[(decryp_range << 4) + i]);
	printf("\n");
#endif	
	return 0;
}

// for aes_cbc, need to remove the chain using xor
static void dechain_aes_mcast(u8 range)
{
	if(range != 0)
	{
	  for(int i = 0; i < 16; i++)
	    decrypted_data_mcast[(range << 4) + i] ^= raw_data_mcast[(range - 1 << 4) + i];		
	}
	// dump data
#if 0	
	printf("range %d dechained to: ", range);
	for(int i = 0; i < 16; i++)
	  printf("0x%02x ", decrypted_data_mcast[(range << 4) + i]);
	printf("\n");
#endif
  printf("mcast: block %d is dechained\n", range);	
	decryp_flag_mcast[range] = 2;
	count_mcast += 8;
	return;
}
/*
ret: 1, enough data; 0 error or not enough
239.{index}.{byte[i]}{byte[i+1]}
{index} = (CRCLSB*3bit) + (Index*5bit)
*/
static int check_data_mcast(u8 *da)
{
	u8 raw_index = da[3];
	u8 CRC_index = (raw_index & 0x40) >> 6;
	u8 idx = raw_index & 0x3f;
	u8 first, second;
	u8 range = 0;
	int first_in_range = 0;
	// check CRC

  // idx is invalid, start with 1
	if((idx > NUM_MCAST) || (idx < 1))
		return 0;
  
  // CRC check pass
	if(((da[4] ^ da[5]) & 0x01) == CRC_index)
	{
		// already received
		if(recved_flag_mcast[idx] == 1)
			return 0;
		
		// new pkt	
		recved_flag_mcast[idx] = 1;
		first = (idx -1) * 2;
		second = first + 1;
		
		raw_data_mcast[first] = da[4];
		raw_data_mcast[second] = da[5];
		printf("mcast: new pkt, idx is %d\n", idx);
		// range begins with 0, every 8 pkts belongs to 1 range,e.g idx: {1~8} -> range:0
    range = (idx - 1) >> 3;
    // not enough pkt for decryption
    if(!decryp_ready(range))
    	return 0;
    // start to decrypt
    first_in_range = range << 4;
    if(decryp_data(range) == -1)
    {
    	// clear the received flag for this range
    	for (int i = 1; i < 9; i++)
    		recved_flag_mcast[first_in_range + i] = 0;
    	printf("decryped error in range %d\n",range);
    	return 0;
    }
    // decryption success here;
    decryp_flag_mcast[range] = 1;
    printf("mcast: block %d is decrypted\n", range);
    		
		// this is the sum and pass_len
		//if((idx == 1) && (!sum_mcast))
		if(range == 0)
		{
			dechain_aes_mcast(range);
			
			sum_mcast = decrypted_data_mcast[0];
    	pass_len = decrypted_data_mcast[1];
    	printf("mcast: sum_mcast 0x%02x pass_len %d \n",sum_mcast, pass_len);
			
			// check whether the pass_len is valid
			if(pass_len < 0 || pass_len > 64)
			{
				printf("mcast: pass_len is wrong, clear\n");
	      decryp_flag_mcast[range] = 0;
	      count_mcast -= 8;
    	  for (int i = 1; i < 9; i++)
    		  recved_flag_mcast[first_in_range + i] = 0;
				return 0;
			}
			
			// printf("[DEBUG]_mcast: the 2nd flag is %d\n", decryp_flag_mcast[range + 1]);
			// check whether 2nd block is ready
			if(decryp_flag_mcast[range + 1] == 1)
			{
				printf("here\n");
			  dechain_aes_mcast(range + 1);
			}			
			
			if((pass_len & 0x01)  == 0)
			  odd_check = 2; // even
			else
				odd_check = 1; // odd	
			// get the idx of pkt which contains the ssid_len info	
			ssid_offset = 1 + (u8)((8 + pass_len)/2);	
			ssid_range_mcast = (ssid_offset - 1) >> 3;			
			printf("ssid_offset %d ssid_range_mcast %d\n",ssid_offset, ssid_range_mcast);
#if 1	
      // already dechained		
			if(decryp_flag_mcast[ssid_range_mcast] == 2)
			{
				if(total_len_mcast == 0)
				{
					if(ssid_len == 0)
					{
						if(odd_check == 2)
							ssid_len = decrypted_data_mcast[2 * (ssid_offset - 1)];
						if(odd_check == 1)
							ssid_len = decrypted_data_mcast[2 * (ssid_offset - 1) + 1];
						printf("ssid_len is %d\n",ssid_len);
					}
					total_len_mcast = (u8)((pass_len + ssid_len + HEAD_LEN + 1)/2);
			    printf("total_len_mcast is recalculated as %d\n",total_len_mcast);
				}
			}
#endif
		}
		// need to dechain for the 2nd and following block
		else
		{
			if(decryp_flag_mcast[range - 1] > 0)
				dechain_aes_mcast(range);
				
			if(decryp_flag_mcast[range + 1] == 1)
				dechain_aes_mcast(range + 1);
				
			if(!decryp_flag_mcast[range - 1] && !decryp_flag_mcast[range + 1])
				return 0;
		}
		// 8 new pkts has been de chained for AES
		
		// set the ssid_len
		if(ssid_range_mcast != 255 && decryp_flag_mcast[ssid_range_mcast] == 2)
		{
		   if(ssid_len == 0)
		   {
		   	if(odd_check == 2)
		   		ssid_len = decrypted_data_mcast[2 * (ssid_offset - 1)];
		   	if(odd_check == 1)
		   		ssid_len = decrypted_data_mcast[2 * (ssid_offset - 1) + 1];
		   	printf("ssid_len is %d\n",ssid_len);
		   }
		}

		// set the total_len 
		if((pass_len != -1) && (ssid_len != 0))
		{
			if(total_len_mcast == 0)
			{
			  total_len_mcast = (u8)((pass_len + ssid_len + HEAD_LEN + 1)/2);
			  printf("total_len_mcast is calculated as %d\n",total_len_mcast);
			}
		}
		
		printf("total_len needed is %d already decrypted %d\n", total_len_mcast, count_mcast);
		
		if(!total_len_mcast)
		{
		  if(count_mcast >= NUM_MCAST)
				return 1;
			else
				return 0;
		}
		else
		{
			//printf("count_mcast is %d total_len_mcast is %d\n");
		  if(count_mcast >= total_len_mcast)
		  {
		  		// check CRC
		  	u8 crc_ret = 0;
		  	printf("enough decrypted pkt, start to check sum\n");
		  	if((pass_len + ssid_len) & 0x01)
		  		crc_ret = getCrc(decrypted_data_mcast + 1, total_len_mcast * 2 - 1);
		  	else
		  		crc_ret = getCrc(decrypted_data_mcast + 1, total_len_mcast * 2 - 2);
				
				if(crc_ret == sum_mcast)			  					  		
          {printf("sum check pass\n"); return 1;}
        else
      	{
      		printf("sum crc check failure, restart\n");
      		joinlink_restart(); // fine tune: only restart the mcast part
      		return 0;
      	}
		  }
			else
				return 0;		
		}
	}
	// check CRC failure
	else
	{
		//printf("CRC failure in mcast, getCrc is 0x%02x, CRC is 0x%02x\n",(da[4] ^ da[5]), CRC_index);
		return 0;
	}

}
/*
ret: 0, failure; 1 true.
*/
static int check_sync_bcast(int len)
{
	// make sure the bits larger than 9 is 0
	if(len >= 256) 
		return 0;
	// only the least 9 bits are useful	
	len &= 0x01ff;
	if((len >=1) && (len <=4))
	{
		sync_label_bcast |= 0x01 << (len - 1);
		if(sync_label_bcast == 0x0f)
			return 1;
		else
			return 0;
	}
	else
		return 0;	
}

/* 
{0b10000*5bit}+{CRCLSB*4bit}
{0*1bit}{Version}
 ret: 0, failure; 1 true
 */
static int check_version_bcast(int len, u8 i_fc)
{
  version_bcast = 0;
  
	if(!version_bcast_ready)
	{
		u8 version_pre_CRC = len & 0x0007;
		u8 version_pre_data = (len & 0x01f8) >> 3;
		
		if(len >= 512)
			return 0;
		
		if(version_pre_data == 0x20)
		{
			version_bcast_ready = 1; 
			version_CRC = version_pre_CRC;
			// fix the direction(fromDS/toDS) to receive version info
  		fc_version_bcast = i_fc;
				
//			printf("get the CRC of version, change to wait version state\n");
		}	
			
		return 0;
	}
	else
	{
		if(i_fc != fc_version_bcast)
			return 0;
			
		if((len & 0xff00) != 0) 
			return 0;
		
		version_bcast = len & 0x00ff;
		if((getCrc(&version_bcast,1) & 0x07) == version_CRC)			
		{
			printf("version CRC pass\n");
			return 1;
		}
		else
		{
			//printf("version CRC failure,reset this state, version is 0x%02x calculated CRC is 0x%02x, CRC is 0x%02x\n", 
			//      version_bcast, getCrc(&version_bcast,1), version_CRC);
			version_bcast_ready = 0;
		}

	}
	return 0;
}

/*ret 1: valid seq, ret 0: invalid seq*/
static u8 check_and_update_seq(unsigned short frame_seq)
{
	int seq_delta = frame_seq - seq_now_bcast;
	
#if SEQ_INCREMENT_ONE_BCAST
	if((seq_delta == 1) || (seq_now_bcast == 4095) && (frame_seq == 0))
	{
		seq_now_bcast = frame_seq;
		return 1;
	}
	else
	{
		seq_now_bcast = frame_seq;
		return 0;
	}
#else	
	if(((seq_delta <= 10) && (seq_delta >= 0)) || 
		((seq_now_bcast > 4085) && (seq_delta + 4096 <= 10) && (seq_delta + 4096 >= 0)))
	{
		seq_now_bcast = frame_seq;
		//printf("valid seq, seq_delta %d seq_now is updated to %d\n", seq_delta, seq_now_bcast);
		return 1;
	}
	else
	{
		seq_now_bcast = frame_seq;
		//printf("invalid seq, seq_delta %d seq_now is updated to %d\n", seq_delta, seq_now_bcast);
		return 0;
	}
#endif
}
// idx starts with 1, every 4 is one decryption block
static int decryp_ready_bcast(u8 first_idx)
{	
	for(int i = 0; i < 4; i++)
	{
		if(decoded_flag_bcast[first_idx + i] == 0)
			return 0;
	}
	return 1;
}
// decryption for bcast
static int decryp_data_bcast(u8 idx)
{
	memset(decryp_data_buf, 0, sizeof(decryp_data_buf));
// dump the encryption info	
#if 0	
	printf("before decryption of idx %d:", idx);
	for(int i = 0; i < 16; i++)
	  printf("0x%02x ", raw_data_bcast[((idx >> 2) << 4) + i]);
	printf("\n");
#endif

	// this decrpytion API only accept 16 byte size
	if (rtl_crypto_aes_cbc_decrypt(raw_data_bcast + ((idx >> 2) << 4), 16, aes_iv, sizeof(aes_iv), decryp_data_buf) != 0 ) 
	{
		printf("AES CBC decrypt failed\n");
		return -1;
	}
	memcpy(decrypted_data_bcast + ((idx >> 2) << 4), decryp_data_buf, 16);
	printf("bcast: blcok %d is decrypted\n", idx >> 2);
// dump the info after decryption	
#if 0	
	printf("after decryption of idx %d:", idx);
	for(int i = 0; i < 16; i++)
	  printf("0x%02x ", decrypted_data_bcast[((idx >> 2) << 4) + i]);
	printf("\n");	
#endif
	
	return 0;
}
// remove chain for aes cbc for bcast mode
static void dechain_aes_bcast(u8 idx)
{
	u8 first_idx = (idx >> 2) << 4;
	if(idx != 1)
	{
		for(int i = 0; i < 16; i++)
			decrypted_data_bcast[first_idx + i] ^= raw_data_bcast[first_idx - 16 + i];
	}
	count_decoded_bcast += 4;
	// set the dechained flag
  for(int i = 0; i < 4; i++)
	  decoded_flag_bcast[idx + i] = 3;
	// dump the info after dechain
#if 0
	printf("idx %d is de chained to: ", idx);
	for(int i = 0; i < 16; i++)
	  printf("0x%02x ", decrypted_data_bcast[first_idx + i]);
	printf("\n");
#endif
  printf("bcast: block %d is dechained\n", idx >> 2);
	return;
}

/*
{EncodeIndex*5bit}+{CRCLSB*4bit}
{0*1bit}{Byte(i+0) *8bit}
{0*1bit}{Byte(i+1) *8bit}
{0*1bit}{Byte(i+2) *8bit}
{0*1bit}{Byte(i+3) *8bit}
*/
static int check_data_bcast(int len, u8 i_fc, unsigned short frame_seq, unsigned const char *temp_bssid)
{
	// waiting index pkt
	if(!data_phase_state_bcast)
	{
		
		// make sure the 9th bit is 1 and bit larger than 9 is 0
		if(((len & 0x0fff) >= 512) || ((len & 0x0fff) <= 256))
			return 0;
		// idx_data is increase from 1, not 0	
		idx_data = (len & 0x00f8) >> 3;
		if(idx_data == 0)
			return 0;
		
		if(idx_data > NUM_BCAST)
		{
			printf("index is too large\n");
			return 0;
		}
		
		// already decoded this idx
		if(decoded_flag_bcast[idx_data] >= 1)
			return 0;
		else
		{
			current_idx_bcast = idx_data;
			count_in_idx_bcast = 0;
			data_phase_state_bcast = 1;
			idx_CRC = len & 0x0007;
			seq_now_bcast = frame_seq;
			//printf("idx_CRC is 0x%02x len is 0x%02x\n",idx_CRC, len);
			fc_data_bcast = i_fc;
			//printf("waiting data pkt of idx %d, locked in i_fc %d, seq_now %d\n",
			//				current_idx_bcast,fc_data_bcast,seq_now_bcast);
			return 0;
		}
			
	}
	// waiting info pkt
	else
	{
		u8 array_idx = 0;
		// check whether the data is valid, the 9th bit should be 0
		if(len >= 256)
		{
			//printf("not info pkt\n");
			return 0;
		}
		
		// only receive the data from the previous idx direction
		if(i_fc != fc_data_bcast)
			return 0;
		
		array_idx = 4 * (current_idx_bcast - 1);

		//printf("from bssid 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
		//						 temp_bssid[0],temp_bssid[1],temp_bssid[2],temp_bssid[3],temp_bssid[4],temp_bssid[5]);		
		// check whether seq is valid
	  if(check_and_update_seq(frame_seq) == 0)
	  {
			//memset(raw_data_bcast + array_idx, 0, 4);
			data_phase_state_bcast = 0;
			return 0;  
	  }
		
		raw_data_bcast[array_idx + count_in_idx_bcast] = len & 0x00ff;
		count_in_idx_bcast++;
		
		//printf("len 0x%02x, info 0x%02x, i_fc %d i_seq %d\n",len, len & 0x00ff, i_fc, frame_seq);
		if(count_in_idx_bcast != NUM_PKT)
			return 0;
		else
		{
			u8 temp_ret = 0;
			u8 first_idx = 0;
      
#if 0			
			printf("enough data pkt for idx, check CRC\n",current_idx_bcast);
			printf("data to be decoded in idx %d: 0x%02x 0x%02x 0x%02x 0x%02x\n",current_idx_bcast,
			      *(raw_data_bcast + array_idx),*(raw_data_bcast + array_idx + 1),
			      *(raw_data_bcast + array_idx + 2),*(raw_data_bcast + array_idx + 3));
#endif			      
			
			// assume first encryption and then CRC, so CRC check first and then decryption for receiver.      
			temp_ret = getCrc(raw_data_bcast + array_idx, 4) & 0x07;
			//printf("calculated CRC is 0x%02x true CRC is 0x%02x\n",temp_ret, idx_CRC);
			// CRC pass
			if(temp_ret == idx_CRC)
			{
				printf("bcast: idx %d is decoded\n",current_idx_bcast);
				// the first idx in every decryption block
				first_idx = 1 + ((current_idx_bcast - 1 >> 2) << 2);
				// set the flag of this idx to 1, indicate pass CRC but not yet to decrypt
				decoded_flag_bcast[current_idx_bcast] = 1;
				// not enough neighbor idx for decryption
				if(decryp_ready_bcast(first_idx) == 0)
					return 0;
				
				if(decryp_data_bcast(first_idx) == -1)
				{
					for(int i = 0; i < 4; i++)
					  decoded_flag_bcast[first_idx + i] = 0;
					// clear the 4 idx data in this block;  
					memset(raw_data_bcast + ((current_idx_bcast - 1 >> 2) << 4), 0, 16);
					data_phase_state_bcast = 0;
					return 0;
				}
				// decryption PASS
				// set the decryption flag
				for(int i = 0; i < 4; i++)
				  decoded_flag_bcast[first_idx + i] = 2;
				
	      // if 1st block, get the pass_len
        if(current_idx_bcast <= 4)
        {
        	dechain_aes_bcast(first_idx);
        	sum_bcast = *decrypted_data_bcast; 
        	pass_len_bcast = *(decrypted_data_bcast + 1);
        	// make sure the pass len is in the right range {0,63}
        	if((pass_len_bcast > 63) || (pass_len_bcast < 0))
        	{
        		printf("pass_len is wrong, clear\n");
					  for(int i = 0; i < 4; i++)
					    decoded_flag_bcast[first_idx + i] = 0;
					  count_decoded_bcast -= 4;
					  // clear the 4 idx data in this block;  
					  memset(raw_data_bcast + ((current_idx_bcast - 1 >> 2) << 4), 0, 16);
					  data_phase_state_bcast = 0;
						return 0;        		
        	}
        	printf("sum_bcast is %d pass_len_bcast is %d\n",sum_bcast, pass_len_bcast);
        	// recalculate ssid_len if the idx containning ssid_len is already decoded
        	
        	// to de chain the neighbor block
        	if(decryp_ready_bcast(first_idx + 4))
        		dechain_aes_bcast(first_idx + 4);
        	
        	ssid_offset_bcast = (8 + pass_len_bcast)/4 + 1; 
        	if(decoded_flag_bcast[ssid_offset_bcast] == 3)
        	{
        	  ssid_len_bcast = *(decrypted_data_bcast + 8 + pass_len_bcast);
        	  // check whether ssid len is in the right range {0,32}
	        	if(ssid_len_bcast > 32)
	        	{
							//memset(raw_data_bcast + 4 * (2 + pass_len_bcast/4), 0, 4);
							data_phase_state_bcast = 0;
							for(int i = 0; i < 4; i++)
						  	decoded_flag_bcast[(ssid_len_bcast - 1 >> 2) + i] = 0;
						  count_decoded_bcast -= 4;
							printf("ssid_len_bcast is wrong, clear idx %d\n", ((8 + pass_len_bcast)/4 + 1));
							return 0;         		
	        	}
        	  printf("recalculated ssid_len_bcast is %d\n",ssid_len_bcast);
        	}
        }
        // for the 2nd and following block, need the preceeding block to de chain
        else
        {
        	if(decryp_ready_bcast(first_idx - 4))
        	  dechain_aes_bcast(first_idx);
        	  
        	if(decryp_ready_bcast(first_idx + 4))
        		dechain_aes_bcast(first_idx + 4);
        	
        	if(!decryp_ready_bcast(first_idx - 4) && !decryp_ready_bcast(first_idx + 4))	
        		return 0;
        }
        // check whether ssid_len idx has been dechained
        for(int i = 0; i < 4; i++)
        {
          if((pass_len_bcast != -1) && (decoded_flag_bcast[ssid_offset_bcast] == 3))
          {
          	ssid_len_bcast = *(decrypted_data_bcast + 8 + pass_len_bcast);
          	// make sure ssid_len is valid in the range {0, 32}
          	if(ssid_len_bcast > 32)
          	{
							//memset(raw_data_bcast + 4 * (2 + pass_len_bcast/4), 0, 4);
							data_phase_state_bcast = 0;
							for(int i = 0; i < 4; i++)
						  	decoded_flag_bcast[(ssid_offset_bcast >> 2) << 2 + i + 1] = 0;
						  count_decoded_bcast -= 4;
							printf("ssid_len_bcast is wrong\n");
							return 0;
          	}
          	printf("ssid_len_bcast is %d\n",ssid_len_bcast);
          	break;
          }        	
        }
        // check whether enough
        if((ssid_len_bcast != 0) && (pass_len_bcast != -1))
        {
        	u8 total_len_bcast = HEAD_LEN + ssid_len_bcast + pass_len_bcast;
        	printf("needed %d pkt, decoded %d\n",(u8)((total_len_bcast + 3)/4),count_decoded_bcast);
        	if(count_decoded_bcast >= (u8)((total_len_bcast + 3)/4))
        	{
        		printf("enough decoded packets, start to check sum\n");
        		if(getCrc(decrypted_data_bcast + 1,total_len_bcast - 1) == sum_bcast)
        		{
        			printf("sum check pass in bcast\n");
        			return 1;
        		}
        		else
        		{
        			//printf("bcast sum check failure, restart\n");
        			joinlink_restart(); // fine tune: only restart the bcast part
        			return 0;
        		}
        	}
        }
				
				data_phase_state_bcast = 0;
			}
			else
			{
				memset(raw_data_bcast + 4 * (current_idx_bcast - 1), 0, 4);
				data_phase_state_bcast = 0;
				printf("CRC failure of idx %d\n",current_idx_bcast);
				return 0;
			}
		}
		
	}
	
	return 0;
}
/*
ret: 0, failure, 1 true
if success, assign the AP profile to a structure.
*/ 
static int decode_AP_profile(u8 *raw_data)
{
	int pos = 0;
		
	AP_profile->sum = raw_data[pos];
	pos++;
	printf("AP_profile: sum %d\n",AP_profile->sum);
	
	AP_profile->pwd_length = raw_data[pos];
	pos++;
	printf("AP_profile: pwd_len %d\n",AP_profile->pwd_length);
	
	if(AP_profile->pwd_length > 64)
	{
		printf("the pwd len: %d, larger than 64\n",AP_profile->pwd_length);
		return 0;
	}
	
	memcpy(AP_profile->pwd, (raw_data + pos), AP_profile->pwd_length);
	pos += AP_profile->pwd_length;
	printf("AP_profile: pwd %s\n",AP_profile->pwd);
	
	AP_profile->source_ip[0] = *(raw_data + pos);
	AP_profile->source_ip[1] = *(raw_data + pos + 1);
	AP_profile->source_ip[2] = *(raw_data + pos + 2);
	AP_profile->source_ip[3] = *(raw_data + pos + 3);
	
	pos += 4;
	printf("AP_profile: sip %d %d %d %d\n",AP_profile->source_ip[0],
																				 AP_profile->source_ip[1],
																				 AP_profile->source_ip[2],
																				 AP_profile->source_ip[3]);
	// assume the high byte is the most significant
	#if 1
	AP_profile->source_port = ((unsigned int)(*(raw_data + pos + 1)) << 8) | (*(raw_data + pos));
	//printf("port high_part, low_part: %d %d\n", *(raw_data + pos + 1), *(raw_data + pos));
	#endif
	
	pos += 2;
	printf("AP_profile: port %d\n",AP_profile->source_port);
	
	AP_profile->ssid_length = *(raw_data + pos);
	pos++;
	printf("AP_profile: ssid_len %d\n",AP_profile->ssid_length);
	
	if(AP_profile->ssid_length > 64)
	{
		printf("the ssid len: %d, larger than 64\n",AP_profile->ssid_length);
		return 0;
	}
	memcpy(AP_profile->ssid, (raw_data + pos), AP_profile->ssid_length);
	printf("AP_profile: ssid %s\n",AP_profile->ssid);
	
	return 1;
}

joinlink_status_t joinlink_recv(u8 *da, u8 *sa, int len, void *user_data)
{
	joinlink_status_t ret;
	const ieee80211_frame_info_t *promisc_info = user_data;
	// 1 from ds, 2 to ds
	u8 i_fc = ((promisc_info->i_fc & 0x0100) == 0x0100)? 2: 1;
	unsigned short frame_seq = promisc_info->i_seq;
	unsigned const char *temp_bssid = promisc_info->bssid;
		
	// for mcast
	if(!((da[0] == 0xff) && (da[1] == 0xff) && (da[2] == 0xff) &&
			(da[3] == 0xff) && (da[4] == 0xff) && (da[5] == 0xff)))
	{
		if(joinlink_state_mcast == 0)
		{
			if(!check_sync_mcast(da))
				return JOINLINK_STATUS_CONTINUE;
			else
			{
				// TODO: consider to fix source mac here
				joinlink_state_mcast = 1;
				memcpy(smac, sa, 6);
				printf("turn to wait version state\n");
				return JOINLINK_STATUS_CONTINUE;
			}
				
		}
		else if(joinlink_state_mcast == 1)
		{
			// only accept the pkt from fixed source mac
			if(memcmp(smac, sa, 6))
				return JOINLINK_STATUS_CONTINUE;
				
			if(!check_version_mcast(da))
				return JOINLINK_STATUS_CONTINUE;
			else
			{
				joinlink_state_mcast = 2;
				printf("mcast version is %d\n",version_mcast);
				printf("turn to wait data state\n");
				return JOINLINK_STATUS_CHANNEL_LOCKED;
			}
		}
		else if(joinlink_state_mcast == 2)
		{
			if(memcmp(smac, sa, 6))
				return JOINLINK_STATUS_CONTINUE;
				
			if(!check_data_mcast(da))
				return JOINLINK_STATUS_CONTINUE;
			else
			{
				printf("enough packets, start to decode AP profile\n");
				// AP profile has been gotten
				if(!decode_AP_profile(decrypted_data_mcast))
				{
					printf("decode failure, restart joinlink\n");
					joinlink_restart();
					//TODO: intialize the data structure to restart receive data
					return JOINLINK_STATUS_CONTINUE;
				}
				else
				{
					decoded_state = 1;
					return JOINLINK_STATUS_COMPLETE;
				}
			}
		}
	}
	// for bcast
	else
	{
		len -= 42; // remove the unnecessary part
				
		if(joinlink_state_bcast == 0)
		{
			if(!check_sync_bcast(len))
				return JOINLINK_STATUS_CONTINUE;
			else
			{
				// fix the smac and bssid
				memcpy(smac, sa, 6);
				memcpy(locked_bssid_bcast, temp_bssid, 6);
				joinlink_state_bcast = 1;
				printf("change to bcast_state_1, lock channel\n");
				return /*JOINLINK_STATUS_CONTINUE*/JOINLINK_STATUS_CHANNEL_LOCKED;
			}
				
		}
		else if(joinlink_state_bcast == 1)
		{
			if(memcmp(smac, sa, 6) || memcmp(temp_bssid, locked_bssid_bcast, 6))
				return JOINLINK_STATUS_CONTINUE;			
			
			if(!check_version_bcast(len, i_fc))
				return JOINLINK_STATUS_CONTINUE;
			else
			{
				joinlink_state_bcast = 2;
				printf("change to bcast_state_2\n");
				return /*JOINLINK_STATUS_CHANNEL_LOCKED*/JOINLINK_STATUS_CONTINUE;
			}
		}
		else if(joinlink_state_bcast == 2)
		{
			if(memcmp(smac, sa, 6) || memcmp(temp_bssid, locked_bssid_bcast, 6))
				return JOINLINK_STATUS_CONTINUE;
							
			if(!check_data_bcast(len, i_fc, frame_seq, temp_bssid))
				return JOINLINK_STATUS_CONTINUE;
			else
			{
				// AP profile has been gotten
				if(!decode_AP_profile(decrypted_data_bcast))
				{
					printf("decode failure, restart joinlink\n");
					//TODO: intialize the data structure to restart receive data
					return JOINLINK_STATUS_CONTINUE;
				}
				else
				{
					decoded_state = 1;
					return JOINLINK_STATUS_COMPLETE;
				}
			}
		}
	}
	ret = JOINLINK_STATUS_CONTINUE;
	return ret;
}

/*
 * copy the decode AP info to user space
* store the AP profile to result;
* ret: 0, success
* ret: -1, failure
 */
int joinlink_get_result(joinlink_result_t *result)
{
	if(decoded_state == 0)
		return -1;
	else
	{
    memcpy(result, AP_profile, sizeof(joinlink_result_t));
	  return 0;		
	}
}



/*********************************************************/
