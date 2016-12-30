#include "rtl8195a.h"
#include "build_info.h"
#ifdef PLATFORM_FREERTOS
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#endif
#include "osdep_service.h"
#include "lwip_netconf.h"
#include "ethernet_api.h"
#include "lwip_intf.h"
#include "ethernet_mii.h"
#include "platform_opts.h"
#include "ethernet_ex_api.h"

static _sema mii_rx_sema;
static _mutex mii_tx_mutex;

extern struct netif  xnetif[NET_IF_NUM];

static u8 TX_BUFFER[1518];
static u8 RX_BUFFER[1518];

static u8 *pTmpTxDesc = NULL;
static u8 *pTmpRxDesc = NULL;
static u8 *pTmpTxPktBuf = NULL;
static u8 *pTmpRxPktBuf = NULL;	


int dhcp_ethernet_mii = 1;
int ethernet_if_default = 0;

extern int lwip_init_done;

static _sema mii_linkup_sema;

void mii_rx_thread(void* param){
	u32 len = 0;
	u8* pbuf = RX_BUFFER;
	while(1){
		if (rtw_down_sema(&mii_rx_sema) == _FAIL){
			DBG_8195A("%s, Take Semaphore Fail\n", __FUNCTION__);
			goto exit;
		}
		// continues read the rx ring until its empty
		while(1){
			len = ethernet_receive();
			if(len){
				//DBG_8195A("mii_recv len = %d\n\r", len);
				ethernet_read(pbuf, len);
// calculate the time duration
				ethernetif_mii_recv(&xnetif[NET_IF_NUM - 1], len);		
				//__rtl_memDump_v1_00(pbuf, len, "ethernet_receive Data:");
				//rtw_memset(pbuf, 0, len);
			}else if(len == 0){
				break;
			}
		}
	}
exit:
	rtw_free_sema(&mii_rx_sema);
	vTaskDelete(NULL);
}

void dhcp_start_mii(void* param)
{
	while(1)
	{
		if (rtw_down_sema(&mii_linkup_sema) == _FAIL){
			DBG_8195A("%s, Take Semaphore Fail\n", __FUNCTION__);
			break;
		}
    LwIP_DHCP(NET_IF_NUM - 1, DHCP_START);
	}
	rtw_free_sema(&mii_linkup_sema);
	vTaskDelete(NULL);
}


void mii_intr_handler(u32 Event, u32 Data)
{
	switch(Event)
	{
		case ETH_TXDONE:
			//DBG_8195A("TX Data = %d\n", Data);
			break;
		case ETH_RXDONE:
			//DBG_8195A("\r\nRX Data = %d\n", Data);
			// wake up rx thread to receive data
			rtw_up_sema_from_isr(&mii_rx_sema);
			break;
		case ETH_LINKUP:
			DBG_8195A("Link Up\n");
			
      if(dhcp_ethernet_mii == 1)
			  rtw_up_sema_from_isr(&mii_linkup_sema);
			  
			break;
		case ETH_LINKDOWN:
			DBG_8195A("Link Down\n");
			  
			break;
		default:
			DBG_8195A("Unknown event !!\n");
			break;
	}
}

void ethernet_demo(void* param){
	u8 mac[6];
	/* Initilaize the LwIP stack */
	// can not init twice
	if(!lwip_init_done)
	  LwIP_Init();
	DBG_8195A("LWIP Init done\n");

	ethernet_irq_hook(mii_intr_handler);

    if(pTmpTxDesc)
    {
        free(pTmpTxDesc);
        pTmpTxDesc = NULL;
    }
    if(pTmpRxDesc)
    {
        free(pTmpRxDesc);
        pTmpRxDesc = NULL;
    }
    if(pTmpTxPktBuf)
    {
        free(pTmpTxPktBuf);
        pTmpTxPktBuf = NULL;
    }
    if(pTmpRxPktBuf)
    {
        free(pTmpRxPktBuf);
        pTmpRxPktBuf = NULL;
    }
    
    pTmpTxDesc = (u8 *)malloc(/*MII_TX_DESC_CNT*/MII_TX_DESC_NO * ETH_TX_DESC_SIZE);
    pTmpRxDesc = (u8 *)malloc(/*MII_RX_DESC_CNT*/MII_RX_DESC_NO * ETH_RX_DESC_SIZE);
    pTmpTxPktBuf = (u8 *)malloc(/*MII_TX_DESC_CNT*/MII_TX_DESC_NO * ETH_PKT_BUF_SIZE);
    pTmpRxPktBuf = (u8 *)malloc(/*MII_RX_DESC_CNT*/MII_RX_DESC_NO * ETH_PKT_BUF_SIZE);
    if(pTmpTxDesc == NULL || pTmpRxDesc == NULL || pTmpTxPktBuf == NULL || pTmpRxPktBuf == NULL)
    {
        printf("TX/RX descriptor malloc fail\n");
        return;
    }
    memset(pTmpTxDesc, 0, MII_TX_DESC_NO * ETH_TX_DESC_SIZE);
    memset(pTmpRxDesc, 0, MII_RX_DESC_NO * ETH_RX_DESC_SIZE);
    memset(pTmpTxPktBuf, 0, MII_TX_DESC_NO * ETH_PKT_BUF_SIZE);
    memset(pTmpRxPktBuf, 0, MII_RX_DESC_NO * ETH_PKT_BUF_SIZE);
    //size 160        128     12288   12288            
    
    ethernet_set_descnum(MII_TX_DESC_NO, MII_RX_DESC_NO);
    printf("TRX descriptor number setting done\n");
    ethernet_trx_pre_setting(pTmpTxDesc, pTmpRxDesc, pTmpTxPktBuf, pTmpRxPktBuf);
    printf("TRX pre setting done\n");

	ethernet_init();

	DBG_INFO_MSG_OFF(_DBG_MII_);  
	DBG_WARN_MSG_OFF(_DBG_MII_);
	DBG_ERR_MSG_ON(_DBG_MII_);
	
	/*get mac*/
	ethernet_address(mac);
	memcpy((void*)xnetif[NET_IF_NUM - 1].hwaddr,(void*)mac, 6);

	rtw_init_sema(&mii_rx_sema,0);
	rtw_mutex_init(&mii_tx_mutex);

	if(xTaskCreate(mii_rx_thread, ((const char*)"mii_rx_thread"), 1024, NULL, tskIDLE_PRIORITY+5, NULL) != pdPASS)
		DBG_8195A("\n\r%s xTaskCreate(mii_rx_thread) failed", __FUNCTION__);
	
	DBG_8195A("\nEthernet_mii Init done, interface %d",NET_IF_NUM - 1);
	
	if(dhcp_ethernet_mii == 1)
	  LwIP_DHCP(NET_IF_NUM - 1, DHCP_START);
	
	vTaskDelete(NULL);
}

void ethernet_mii_init()
{
	printf("\ninitializing Ethernet_mii......\n");
  
  // set the ethernet interface as default
  ethernet_if_default = 1;
  rtw_init_sema(&mii_linkup_sema,0);
  
	if( xTaskCreate((TaskFunction_t)dhcp_start_mii, "DHCP_START_MII", 1024, NULL, 2, NULL) != pdPASS) {
		DBG_8195A("Cannot create demo task\n\r");
	}	

	if( xTaskCreate((TaskFunction_t)ethernet_demo, "ETHERNET DEMO", 1024, NULL, 2, NULL) != pdPASS) {
		DBG_8195A("Cannot create demo task\n\r");
	}
	
}


void rltk_mii_recv(struct eth_drv_sg *sg_list, int sg_len){
	struct eth_drv_sg *last_sg;
	u8* pbuf = RX_BUFFER;

	for (last_sg = &sg_list[sg_len]; sg_list < last_sg; ++sg_list) {
		if (sg_list->buf != 0) {
			rtw_memcpy((void *)(sg_list->buf), pbuf, sg_list->len);
			pbuf+=sg_list->len;
		}			 
	}
}


s8 rltk_mii_send(struct eth_drv_sg *sg_list, int sg_len, int total_len){
	int ret =0;
	struct eth_drv_sg *last_sg;
	u8* pdata = TX_BUFFER;
	u8	retry_cnt = 0;
	u32 size = 0;
	for (last_sg = &sg_list[sg_len]; sg_list < last_sg; ++sg_list) {
		rtw_memcpy(pdata, (void *)(sg_list->buf), sg_list->len);
		pdata += sg_list->len;
		size += sg_list->len;		
	}
	pdata = TX_BUFFER;
	//DBG_8195A("mii_send len= %d\n\r", size);
	rtw_mutex_get(&mii_tx_mutex);
	while(1){
		ret = ethernet_write(pdata, size);
		if(ret > 0){
			ethernet_send();
			ret = 0;
			break;
		}
		if(++retry_cnt > 3){
			DBG_8195A("TX drop\n\r");
			ret = -1;
		}
		else
			rtw_udelay_os(1);
	}
	rtw_mutex_put(&mii_tx_mutex);

	return ret; 	
}