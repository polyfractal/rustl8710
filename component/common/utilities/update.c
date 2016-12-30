#include <stdlib.h>
#include <string.h>
#include <FreeRTOS.h>
#include <task.h>
#include <lwip/sockets.h>
#include <sys.h>

#if !defined(CONFIG_PLATFORM_8195A) && !defined(CONFIG_PLATFORM_8711B)
#include <flash/stm32_flash.h>
#if defined(STM32F2XX)
#include <stm32f2xx_flash.h>
#elif defined(STM32F4XX)	 
#include <stm32f4xx_flash.h>
#elif defined(STM32f1xx)	 
#include <stm32f10x_flash.h>
#endif
#include "cloud_updater.h"
#else
#include "flash_api.h"
#include <device_lock.h>
#endif
#include "update.h"
#if defined(CONFIG_PLATFORM_8195A) || defined(CONFIG_PLATFORM_8711B)
#define OFFSET_DATA		FLASH_SYSTEM_DATA_ADDR
#define IMAGE_2			0x0000B000
#define WRITE_OTA_ADDR		1
#define CONFIG_CUSTOM_SIGNATURE 1
#define SWAP_UPDATE 0

#if WRITE_OTA_ADDR
#define BACKUP_SECTOR	(FLASH_SYSTEM_DATA_ADDR - 0x1000)
#endif

#if CONFIG_CUSTOM_SIGNATURE
/* ---------------------------------------------------
  *  Customized Signature
  * ---------------------------------------------------*/
// This signature can be used to verify the correctness of the image
// It will be located in fixed location in application image
#include "section_config.h"
SECTION(".custom.validate.rodata")
const unsigned char cus_sig[32] = "Customer Signature-modelxxx";
#endif

#else
#define CONFIG_SECTOR			FLASH_Sector_1
#define APPLICATION_SECTOR		FLASH_Sector_2
#define UPDATE_SECTOR			FLASH_Sector_8
#endif
#define STACK_SIZE		1024
#define TASK_PRIORITY	tskIDLE_PRIORITY + 1
#define BUF_SIZE		512
#define ETH_ALEN	6

#define SERVER_LOCAL	1
#define SERVER_CLOUD	2
#define SERVER_TYPE		SERVER_LOCAL
#define UPDATE_DBG		1

#if (SERVER_TYPE == SERVER_LOCAL)
typedef struct
{
	uint32_t	ip_addr;
	uint16_t	port;
}update_cfg_local_t;
#endif

#if (SERVER_TYPE == SERVER_CLOUD)
#define REPOSITORY_LEN	16
#define FILE_PATH_LEN	64
typedef struct
{
	uint8_t	 	repository[REPOSITORY_LEN];
	uint8_t		file_path[FILE_PATH_LEN];
}update_cfg_cloud_t;
#endif

sys_thread_t TaskOTA = NULL;
//---------------------------------------------------------------------
static void* update_malloc(unsigned int size)
{
	return pvPortMalloc(size);
}

//---------------------------------------------------------------------
static void update_free(void *buf)
{
	vPortFree(buf);
}

//---------------------------------------------------------------------
#if (SERVER_TYPE == SERVER_LOCAL)
#if defined(STM32F2XX) ||(STM32F4XX)
static void update_ota_local_task(void *param)
{
	int server_socket = 0;
	struct sockaddr_in server_addr;
	char *buf;
	int read_bytes, size = 0, i;
	update_cfg_local_t *cfg = (update_cfg_local_t*)param;
	uint32_t address, checksum = 0;
#if CONFIG_WRITE_MAC_TO_FLASH
	char mac[ETH_ALEN];
#endif
	printf("\n\r[%s] Update task start", __FUNCTION__);
	buf = update_malloc(BUF_SIZE);
	if(!buf){
		printf("\n\r[%s] Alloc buffer failed", __FUNCTION__);
		goto update_ota_exit;
	}
	// Connect socket
	server_socket = socket(AF_INET, SOCK_STREAM, 0);
	if(server_socket < 0){
		printf("\n\r[%s] Create socket failed", __FUNCTION__);
		goto update_ota_exit;
	}
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = cfg->ip_addr;
	server_addr.sin_port = cfg->port;

	if(connect(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1){
		printf("\n\r[%s] socket connect failed", __FUNCTION__);
		goto update_ota_exit;
	}
	// Erase config sectors
	if(flash_EraseSector(CONFIG_SECTOR) < 0){
		printf("\n\r[%s] Erase sector failed", __FUNCTION__);
		goto update_ota_exit;
	}
#if CONFIG_WRITE_MAC_TO_FLASH
	// Read MAC address
 	if(flash_Read(FLASH_ADD_STORE_MAC, mac, ETH_ALEN) < 0){
		printf("\n\r[%s] Read MAC error", __FUNCTION__);
		goto update_ota_exit;
 	}	
#endif
	// Erase update sectors
	for(i = UPDATE_SECTOR; i <= FLASH_Sector_11; i += 8){
		if(flash_EraseSector(i) < 0){
			printf("\n\r[%s] Erase sector failed", __FUNCTION__);
			goto update_ota_exit;
		}
	}
	// Write update sectors
	address = flash_SectorAddress(UPDATE_SECTOR);
	printf("\n\r");
	while(1){
		read_bytes = read(server_socket, buf, BUF_SIZE);
		if(read_bytes == 0) break; // Read end
		if(read_bytes < 0){
			printf("\n\r[%s] Read socket failed", __FUNCTION__);
			goto update_ota_exit;
		}
		if(flash_Wrtie(address + size, buf, read_bytes) < 0){
			printf("\n\r[%s] Write sector failed", __FUNCTION__);
			goto update_ota_exit;
		}
		size += read_bytes;
		for(i = 0; i < read_bytes; i ++)
			checksum += buf[i];
		printf("\rUpdate file size = %d  ", size);
	}
#if CONFIG_WRITE_MAC_TO_FLASH
	//Write MAC address
	if(!(mac[0]==0xff&&mac[1]==0xff&&mac[2]==0xff&&mac[3]==0xff&&mac[4]==0xff&&mac[5]==0xff)){
		if(flash_Wrtie(FLASH_ADD_STORE_MAC, mac, ETH_ALEN) < 0){
			printf("\n\r[%s] Write MAC failed", __FUNCTION__);
			goto update_ota_exit;
		}	
	}
#endif
	// Write config sectors
	address = flash_SectorAddress(CONFIG_SECTOR);
	if( (flash_Wrtie(address, (char*)&size, 4) < 0) || 
		(flash_Wrtie(address+4, (char*)&checksum, 4) < 0) ){
		printf("\n\r[%s] Write sector failed", __FUNCTION__);
		goto update_ota_exit;
	}
	printf("\n\r[%s] Update OTA success!", __FUNCTION__);
update_ota_exit:
	if(buf)
		update_free(buf);
	if(server_socket >= 0)
		close(server_socket);
	if(param)
		update_free(param);
	TaskOTA = NULL;
	printf("\n\r[%s] Update task exit", __FUNCTION__);
	vTaskDelete(NULL);
	return;
}
#elif defined(STM32f1xx)
static void update_ota_local_task(void *param)
{
	int server_socket;
	struct sockaddr_in server_addr;
	char *buf, flag_a = 0;
	int read_bytes, size = 0, i;
	update_cfg_local_t *cfg = (update_cfg_local_t*)param;
	uint32_t address, checksum = 0;
	uint16_t a = 0;
#if CONFIG_WRITE_MAC_TO_FLASH
	char mac[ETH_ALEN];
#endif

	printf("\n\r[%s] Update task start", __FUNCTION__);
	buf = update_malloc(BUF_SIZE);
	if(!buf){
		printf("\n\r[%s] Alloc buffer failed", __FUNCTION__);
		goto update_ota_exit;
	}
	// Connect socket
	server_socket = socket(AF_INET, SOCK_STREAM, 0);
	if(server_socket < 0){
		printf("\n\r[%s] Create socket failed", __FUNCTION__);
		goto update_ota_exit;
	}
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = cfg->ip_addr;
	server_addr.sin_port = cfg->port;

	if(connect(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1){
		printf("\n\r[%s] socket connect failed", __FUNCTION__);
		goto update_ota_exit;
	}
	// Erase config sectors
	for(i = CONFIG_SECTOR; i < APPLICATION_SECTOR; i += FLASH_PAGE_SIZE){
		if(flash_EraseSector(i) < 0){
			printf("\n\r[%s] Erase sector failed", __FUNCTION__);
			goto update_ota_exit;
		}
	}
#if CONFIG_WRITE_MAC_TO_FLASH
	// Read MAC address
 	if(flash_Read(FLASH_ADD_STORE_MAC, mac, ETH_ALEN) < 0){
		printf("\n\r[%s] Read MAC error", __FUNCTION__);
		goto update_ota_exit;
 	}	
#endif

	// Erase update sectors
	for(i = UPDATE_SECTOR; i < FLASH_Sector_0 + FLASH_SIZE; i += FLASH_PAGE_SIZE){
		if(flash_EraseSector(i) < 0){
			printf("\n\r[%s] Erase sector failed", __FUNCTION__);
			goto update_ota_exit;
		}
	}
	// Write update sectors
	address = UPDATE_SECTOR;
	printf("\n\r");
	while(1){
		read_bytes = read(server_socket, buf, BUF_SIZE);
		if(read_bytes == 0) break; // Read end
		if(read_bytes < 0){
			printf("\n\r[%s] Read socket failed", __FUNCTION__);
			goto update_ota_exit;
		}
		if(flag_a == 0){
			if(read_bytes % 2 != 0){
				a = buf[read_bytes - 1];
				flag_a = 1;
				if(flash_Wrtie(address + size, buf, read_bytes - 1) < 0){
					printf("\n\r[%s] Write sector failed", __FUNCTION__);
					goto update_ota_exit;
				}
				size += read_bytes - 1;
			}
			else{
				if(flash_Wrtie(address + size, buf, read_bytes) < 0){
					printf("\n\r[%s] Write sector failed", __FUNCTION__);
					goto update_ota_exit;
				}
				size += read_bytes;
			}
		}
		else{
			a = buf[0] << 8 | a;
			if(flash_Wrtie(address + size, (char*)(&a), 2) < 0){
				printf("\n\r[%s] Write sector failed", __FUNCTION__);
				goto update_ota_exit;
			}
			size += 2;
			a = 0;
			flag_a = 0;
			if((read_bytes - 1) % 2 != 0){
				a = buf[read_bytes - 1];
				flag_a = 1;
				if(flash_Wrtie(address + size, buf + 1, read_bytes - 2) < 0){
					printf("\n\r[%s] Write sector failed", __FUNCTION__);
					goto update_ota_exit;
				}
				size += read_bytes - 2;
			}
			else{ 
				if(flash_Wrtie(address + size, buf + 1, read_bytes - 1) < 0){
					printf("\n\r[%s] Write sector failed", __FUNCTION__);
					goto update_ota_exit;
				}
				size += read_bytes - 1;
			}
		}	
		for(i = 0; i < read_bytes; i ++)
			checksum += buf[i];
		printf("\rUpdate file size = %d  ", size);
	}
	if(flag_a){
		if(flash_Wrtie(address + size, (char*)(&a), 2) < 0){
			printf("\n\r[%s] Write sector failed", __FUNCTION__);
			goto update_ota_exit;
		}
		size += 1;
	}
#if CONFIG_WRITE_MAC_TO_FLASH
	//Write MAC address
	if(!(mac[0]==0xff&&mac[1]==0xff&&mac[2]==0xff&&mac[3]==0xff&&mac[4]==0xff&&mac[5]==0xff)){
		if(flash_Wrtie(FLASH_ADD_STORE_MAC, mac, ETH_ALEN) < 0){
			printf("\n\r[%s] Write MAC failed", __FUNCTION__);
			goto update_ota_exit;
		}	
	}
#endif

	// Write config sectors
	address = CONFIG_SECTOR;
	if( (flash_Wrtie(address, (char*)&size, 4) < 0) || 
		(flash_Wrtie(address+4, (char*)&checksum, 4) < 0) ){
		printf("\n\r[%s] Write sector failed", __FUNCTION__);
		goto update_ota_exit;
	}
	printf("\n\r[%s] Update OTA success!", __FUNCTION__);
update_ota_exit:
	if(buf)
		update_free(buf);
	if(server_socket >= 0)
		close(server_socket);
	if(param)
		update_free(param);
	TaskOTA = NULL;
	printf("\n\r[%s] Update task exit", __FUNCTION__);
	vTaskDelete(NULL);
	return;
}

#elif defined(CONFIG_PLATFORM_8195A) || defined(CONFIG_PLATFORM_8711B)

void ota_platform_reset(void)
{
	//wifi_off();

	// Set processor clock to default before system reset
	HAL_WRITE32(SYSTEM_CTRL_BASE, 0x14, 0x00000021);
	osDelay(100);

	// Cortex-M3 SCB->AIRCR
	HAL_WRITE32(0xE000ED00, 0x0C, (0x5FA << 16) |                             // VECTKEY
	                              (HAL_READ32(0xE000ED00, 0x0C) & (7 << 8)) | // PRIGROUP
	                              (1 << 2));                                  // SYSRESETREQ
	while(1) osDelay(1000);
}
#if WRITE_OTA_ADDR
int write_ota_addr_to_system_data(flash_t *flash, uint32_t ota_addr)
{
	uint32_t data, i = 0;
	//Get upgraded image 2 addr from offset
	device_mutex_lock(RT_DEV_LOCK_FLASH);
	flash_read_word(flash, OFFSET_DATA, &data);
	printf("\n\r[%s] data 0x%x ota_addr 0x%x", __FUNCTION__, data, ota_addr);
	if(data == ~0x0){
		flash_write_word(flash, OFFSET_DATA, ota_addr);
	}else{
		//erase backup sector
		flash_erase_sector(flash, BACKUP_SECTOR);
		//backup system data to backup sector
		for(i = 0; i < 0x1000; i+= 4){
			flash_read_word(flash, OFFSET_DATA + i, &data);
			if(i == 0)
				data = ota_addr;
			flash_write_word(flash, BACKUP_SECTOR + i,data);
		}
		//erase system data
		flash_erase_sector(flash, OFFSET_DATA);
		//write data back to system data
		for(i = 0; i < 0x1000; i+= 4){
			flash_read_word(flash, BACKUP_SECTOR + i, &data);
			flash_write_word(flash, OFFSET_DATA + i,data);
		}
		//erase backup sector
		flash_erase_sector(flash, BACKUP_SECTOR);
	}
	device_mutex_unlock(RT_DEV_LOCK_FLASH);
	return 0;
}
#endif
static void update_ota_local_task(void *param)
{
	int server_socket;
	struct sockaddr_in server_addr;
	unsigned char *buf;
        union { uint32_t u; unsigned char c[4]; } file_checksum;
	int read_bytes = 0, size = 0, i = 0;
	update_cfg_local_t *cfg = (update_cfg_local_t *)param;
	uint32_t address, checksum = 0, flash_checksum=0;
	flash_t	flash;
	uint32_t NewImg2BlkSize = 0, NewImg2Len = 0, NewImg2Addr = 0, file_info[3];
	uint32_t Img2Len = 0;
	int ret = -1 ;
	//uint8_t signature[8] = {0x38,0x31,0x39,0x35,0x38,0x37,0x31,0x31};
	uint32_t IMAGE_x = 0, ImgxLen = 0, ImgxAddr = 0;
#if WRITE_OTA_ADDR
	uint32_t ota_addr = 0x80000;
#endif
#if CONFIG_CUSTOM_SIGNATURE
	char custom_sig[32] = "Customer Signature-modelxxx";
	uint32_t read_custom_sig[8];
#endif
	printf("\n\r[%s] Update task start", __FUNCTION__);
	buf = update_malloc(BUF_SIZE);
	if(!buf){
		printf("\n\r[%s] Alloc buffer failed", __FUNCTION__);
		goto update_ota_exit;
	}
	// Connect socket
	server_socket = socket(AF_INET, SOCK_STREAM, 0);
	if(server_socket < 0){
		printf("\n\r[%s] Create socket failed", __FUNCTION__);
		goto update_ota_exit;
	}
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = cfg->ip_addr;
	server_addr.sin_port = cfg->port;

	if(connect(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1){
		printf("\n\r[%s] socket connect failed", __FUNCTION__);
		goto update_ota_exit;
	}
	DBG_INFO_MSG_OFF(_DBG_SPI_FLASH_);

#if 1
	// The upgraded image2 pointer must 4K aligned and should not overlap with Default Image2
	device_mutex_lock(RT_DEV_LOCK_FLASH);
	flash_read_word(&flash, IMAGE_2, &Img2Len);
	IMAGE_x = IMAGE_2 + Img2Len + 0x10;
	flash_read_word(&flash, IMAGE_x, &ImgxLen);
	flash_read_word(&flash, IMAGE_x+4, &ImgxAddr);
	device_mutex_unlock(RT_DEV_LOCK_FLASH);
	if(ImgxAddr==0x30000000){
		printf("\n\r[%s] IMAGE_3 0x%x Img3Len 0x%x", __FUNCTION__, IMAGE_x, ImgxLen);
	}else{
		printf("\n\r[%s] no IMAGE_3", __FUNCTION__);
		// no image3
		IMAGE_x = IMAGE_2;
		ImgxLen = Img2Len;
	}
#if WRITE_OTA_ADDR
	if((ota_addr > IMAGE_x) && ((ota_addr < (IMAGE_x+ImgxLen))) ||
            (ota_addr < IMAGE_x) ||
            ((ota_addr & 0xfff) != 0)||
	      (ota_addr == ~0x0)){
		printf("\n\r[%s] illegal ota addr 0x%x", __FUNCTION__, ota_addr);
		goto update_ota_exit;
	}else
	    write_ota_addr_to_system_data( &flash, ota_addr);
#endif
	//Get upgraded image 2 addr from offset
	device_mutex_lock(RT_DEV_LOCK_FLASH);
	flash_read_word(&flash, OFFSET_DATA, &NewImg2Addr);
	device_mutex_unlock(RT_DEV_LOCK_FLASH);
	if((NewImg2Addr > IMAGE_x) && ((NewImg2Addr < (IMAGE_x+ImgxLen))) ||
            (NewImg2Addr < IMAGE_x) ||
            ((NewImg2Addr & 0xfff) != 0)||
	      (NewImg2Addr == ~0x0)){
		printf("\n\r[%s] Invalid OTA Address 0x%x", __FUNCTION__, NewImg2Addr);
		goto update_ota_exit;
	}
#else
	//For test, hard code addr
	NewImg2Addr = 0x80000;	
#endif
	
	//Clear file_info
	memset(file_info, 0, sizeof(file_info));
	
	if(file_info[0] == 0){
		printf("\n\r[%s] Read info first", __FUNCTION__);
		read_bytes = read(server_socket, file_info, sizeof(file_info));
		// !X!X!X!X!X!X!X!X!X!X!X!X!X!X!X!X!X!X!X!X
		// !W checksum !W padding 0 !W file size !W
		// !X!X!X!X!X!X!X!X!X!X!X!X!X!X!X!X!X!X!X!X
		printf("\n\r[%s] info %d bytes", __FUNCTION__, read_bytes);
		printf("\n\r[%s] tx chechsum 0x%x, file size 0x%x", __FUNCTION__, file_info[0],file_info[2]);
		if(file_info[2] == 0){
			printf("\n\r[%s] No checksum and file size", __FUNCTION__);
			goto update_ota_exit;
		}
	}
	
#if SWAP_UPDATE
	uint32_t SigImage0,SigImage1;
	uint32_t Part1Addr=0xFFFFFFFF, Part2Addr=0xFFFFFFFF, ATSCAddr=0xFFFFFFFF;
	uint32_t OldImg2Addr;
	device_mutex_lock(RT_DEV_LOCK_FLASH);
	flash_read_word(&flash, 0x18, &Part1Addr);
	Part1Addr = (Part1Addr&0xFFFF)*1024;	// first partition
	Part2Addr = NewImg2Addr;
	
	// read Part1/Part2 signature
	flash_read_word(&flash, Part1Addr+8, &SigImage0);
	flash_read_word(&flash, Part1Addr+12, &SigImage1);
	printf("\n\r[%s] Part1 Sig %x", __FUNCTION__, SigImage0);
	if(SigImage0==0x30303030 && SigImage1==0x30303030)
		ATSCAddr = Part1Addr;		// ATSC signature
	else if(SigImage0==0x35393138 && SigImage1==0x31313738)	
		OldImg2Addr = Part1Addr;	// newer version, change to older version
	else
		NewImg2Addr = Part1Addr;	// update to older version	
	
	flash_read_word(&flash, Part2Addr+8, &SigImage0);
	flash_read_word(&flash, Part2Addr+12, &SigImage1);
	device_mutex_unlock(RT_DEV_LOCK_FLASH);
	printf("\n\r[%s] Part2 Sig %x", __FUNCTION__, SigImage0);
	if(SigImage0==0x30303030 && SigImage1==0x30303030)
		ATSCAddr = Part2Addr;		// ATSC signature
	else if(SigImage0==0x35393138 && SigImage1==0x31313738)
		OldImg2Addr = Part2Addr;
	else
		NewImg2Addr = Part2Addr;
	
	// update ATSC clear partitin first
	if(ATSCAddr != ~0x0){
		OldImg2Addr = NewImg2Addr;
		NewImg2Addr = ATSCAddr;
	}
	
	printf("\n\r[%s] New %x, Old %x", __FUNCTION__, NewImg2Addr, OldImg2Addr);
	
	if( NewImg2Addr==Part1Addr ){
		if( file_info[2] > (Part2Addr-Part1Addr) ){	// firmware size too large
			printf("\n\r[%s] Part1 size < OTA size", __FUNCTION__);
			goto update_ota_exit;
			// or update to partition2
			// NewImg2Addr = Part2Addr;	
		}
	}
		
#endif

	//Erase upgraded image 2 region
	if(NewImg2Len == 0){
		NewImg2Len = file_info[2];
		printf("\n\r[%s] NewImg2Len %d  ", __FUNCTION__, NewImg2Len);
		if((int)NewImg2Len > 0){
			NewImg2BlkSize = ((NewImg2Len - 1)/4096) + 1;
			printf("\n\r[%s] NewImg2BlkSize %d  0x%8x", __FUNCTION__, NewImg2BlkSize, NewImg2BlkSize);
			device_mutex_lock(RT_DEV_LOCK_FLASH);
			for( i = 0; i < NewImg2BlkSize; i++)
				flash_erase_sector(&flash, NewImg2Addr + i * 4096);
			device_mutex_unlock(RT_DEV_LOCK_FLASH);
		}else{
			printf("\n\r[%s] Size INVALID", __FUNCTION__);
			goto update_ota_exit;
		}
	}	
	
	printf("\n\r[%s] NewImg2Addr 0x%x", __FUNCTION__, NewImg2Addr);
        
        // reset
        file_checksum.u = 0;
	// Write New Image 2 sector
	if(NewImg2Addr != ~0x0){
		address = NewImg2Addr;
		printf("\n\r");
		while(1){
			memset(buf, 0, BUF_SIZE);
			read_bytes = read(server_socket, buf, BUF_SIZE);
			if(read_bytes == 0) break; // Read end
			if(read_bytes < 0){
				printf("\n\r[%s] Read socket failed", __FUNCTION__);
				goto update_ota_exit;
			}
				checksum += file_checksum.c[0];              // not read end, this is not attached checksum
				checksum += file_checksum.c[1];
				checksum += file_checksum.c[2];
				checksum += file_checksum.c[3];
			//printf("\n\r[%s] read_bytes %d", __FUNCTION__, read_bytes);
			
			#if 1
			device_mutex_lock(RT_DEV_LOCK_FLASH);
			if(flash_stream_write(&flash, address + size, read_bytes, buf) < 0){
				printf("\n\r[%s] Write sector failed", __FUNCTION__);
				device_mutex_unlock(RT_DEV_LOCK_FLASH);
				goto update_ota_exit;
			}
			device_mutex_unlock(RT_DEV_LOCK_FLASH);
			size += read_bytes;
			for(i = 0; i < read_bytes-4; i ++)
				checksum += buf[i];
			file_checksum.c[0] = buf[read_bytes-4];      // checksum attached at file end
			file_checksum.c[1] = buf[read_bytes-3];
			file_checksum.c[2] = buf[read_bytes-2];
			file_checksum.c[3] = buf[read_bytes-1];
			#else
			size += read_bytes;
			for(i = 0; i < read_bytes-4; i ++){
				checksum += buf[i];				
			}	
			file_checksum.c[0] = buf[read_bytes-4];      // checksum attached at file end
			file_checksum.c[1] = buf[read_bytes-3];
			file_checksum.c[2] = buf[read_bytes-2];
			file_checksum.c[3] = buf[read_bytes-1];
			#endif	

			if(size == NewImg2Len)
				break;
		}
		printf("\n\r");

		// read flash data back and calculate checksum
		for(i=0;i<size-4;i+=BUF_SIZE){
			int k;
			int rlen = (size-4-i)>BUF_SIZE?BUF_SIZE:(size-4-i);
			flash_stream_read(&flash, NewImg2Addr+i, rlen, buf);
			for(k=0;k<rlen;k++)
				flash_checksum+=buf[k];
		}

		printf("\n\rUpdate file size = %d  checksum 0x%8x  flash checksum 0x%8x attached checksum 0x%8x", size, checksum, flash_checksum, file_checksum.u);
#if CONFIG_WRITE_MAC_TO_FLASH
		//Write MAC address
		if(!(mac[0]==0xff&&mac[1]==0xff&&mac[2]==0xff&&mac[3]==0xff&&mac[4]==0xff&&mac[5]==0xff)){
			device_mutex_lock(RT_DEV_LOCK_FLASH);
			if(flash_write_word(&flash, FLASH_ADD_STORE_MAC, mac, ETH_ALEN) < 0){
				printf("\n\r[%s] Write MAC failed", __FUNCTION__);
				device_mutex_unlock(RT_DEV_LOCK_FLASH);
				goto update_ota_exit;
			}	
			device_mutex_unlock(RT_DEV_LOCK_FLASH);
		}
#endif
		//printf("\n\r checksum 0x%x  file_info 0x%x  ", checksum, *(file_info));
#if CONFIG_CUSTOM_SIGNATURE
		device_mutex_lock(RT_DEV_LOCK_FLASH);
		for(i = 0; i < 8; i ++){
		    flash_read_word(&flash, NewImg2Addr + 0x28 + i *4, read_custom_sig + i);
		}
		device_mutex_unlock(RT_DEV_LOCK_FLASH);
		printf("\n\r[%s] read_custom_sig %s", __FUNCTION__ , (char*)read_custom_sig);
#endif
		// compare checksum with received checksum
		//if(!memcmp(&checksum,file_info,sizeof(checksum))
		if( ( (file_checksum.u == checksum) && (file_checksum.u == flash_checksum))
#if CONFIG_CUSTOM_SIGNATURE
			&& !strcmp((char*)read_custom_sig,custom_sig)
#endif
			){
			
			//Set signature in New Image 2 addr + 8 and + 12
			uint32_t sig_readback0,sig_readback1;
			device_mutex_lock(RT_DEV_LOCK_FLASH);
			flash_write_word(&flash,NewImg2Addr + 8, 0x35393138);
			flash_write_word(&flash,NewImg2Addr + 12, 0x31313738);
			flash_read_word(&flash, NewImg2Addr + 8, &sig_readback0);
			flash_read_word(&flash, NewImg2Addr + 12, &sig_readback1);
			device_mutex_unlock(RT_DEV_LOCK_FLASH);
			printf("\n\r[%s] signature %x,%x,  checksum 0x%x", __FUNCTION__ , sig_readback0, sig_readback1, checksum);
#if SWAP_UPDATE
			if(OldImg2Addr != ~0x0){
				device_mutex_lock(RT_DEV_LOCK_FLASH);
				flash_write_word(&flash,OldImg2Addr + 8, 0x35393130);
				flash_write_word(&flash,OldImg2Addr + 12, 0x31313738);
				flash_read_word(&flash, OldImg2Addr + 8, &sig_readback0);
				flash_read_word(&flash, OldImg2Addr + 12, &sig_readback1);
				device_mutex_unlock(RT_DEV_LOCK_FLASH);
				printf("\n\r[%s] old signature %x,%x", __FUNCTION__ , sig_readback0, sig_readback1);
			}
#endif			
			printf("\n\r[%s] Update OTA success!", __FUNCTION__);
			
			ret = 0;
		}
	}
update_ota_exit:
	if(buf)
		update_free(buf);
	if(server_socket >= 0)
		close(server_socket);
	if(param)
		update_free(param);
	TaskOTA = NULL;
	printf("\n\r[%s] Update task exit", __FUNCTION__);	
	if(!ret){
		printf("\n\r[%s] Ready to reboot", __FUNCTION__);	
		ota_platform_reset();
	}
	vTaskDelete(NULL);	
	return;

}
#endif

//---------------------------------------------------------------------
int update_ota_local(char *ip, int port)
{
	update_cfg_local_t *pUpdateCfg;
	
	if(TaskOTA){
		printf("\n\r[%s] Update task has created.", __FUNCTION__);
		return 0;
	}
	pUpdateCfg = update_malloc(sizeof(update_cfg_local_t));
	if(pUpdateCfg == NULL){
		printf("\n\r[%s] Alloc update cfg failed", __FUNCTION__);
		return -1;
	}
	pUpdateCfg->ip_addr = inet_addr(ip);
	pUpdateCfg->port = ntohs(port);
		
	if(xTaskCreate(update_ota_local_task, "OTA_server", STACK_SIZE, pUpdateCfg, TASK_PRIORITY, &TaskOTA) != pdPASS){
	  	update_free(pUpdateCfg);
		printf("\n\r[%s] Create update task failed", __FUNCTION__);
	}
	return 0;
}
#endif // #if (SERVER_TYPE == SERVER_LOCAL)

//---------------------------------------------------------------------
#if (SERVER_TYPE == SERVER_CLOUD)
#if defined(STM32F2XX) ||(STM32F4XX)
static void update_ota_cloud_task(void *param)
{
	struct updater_ctx ctx;
	char *buf;
	int read_bytes, size = 0, i;
	uint32_t address, checksum = 0;
	update_cfg_cloud_t *cfg = (update_cfg_cloud_t*)param;
#if CONFIG_WRITE_MAC_TO_FLASH
	char mac[ETH_ALEN];
#endif
	printf("\n\r[%s] Update task start", __FUNCTION__);
	buf = update_malloc(BUF_SIZE);
	if(!buf){
		printf("\n\r[%s] Alloc buffer failed", __FUNCTION__);
		goto update_ota_exit_1;
	}
	// Init ctx
	if(updater_init_ctx(&ctx, (char*)cfg->repository, (char*)cfg->file_path) != 0) {
		printf("\n\r[%s] Cloud ctx init failed", __FUNCTION__);
		goto update_ota_exit_1;
	}
	printf("\n\r[%s] Firmware link: %s, size = %d bytes, checksum = 0x%08x, version = %s\n", __FUNCTION__, 
		ctx.link, ctx.size, ctx.checksum, ctx.version);

	// Erase config sectors
	if(flash_EraseSector(CONFIG_SECTOR) < 0){
		printf("\n\r[%s] Erase sector failed", __FUNCTION__);
		goto update_ota_exit;
	}
#if CONFIG_WRITE_MAC_TO_FLASH
	// Read MAC address
 	if(flash_Read(FLASH_ADD_STORE_MAC, mac, ETH_ALEN) < 0){
		printf("\n\r[%s] Read MAC error", __FUNCTION__);
		goto update_ota_exit;
 	}	
#endif
	// Erase update sectors
	for(i = UPDATE_SECTOR; i <= FLASH_Sector_11; i += 8){
		if(flash_EraseSector(i) < 0){
			printf("\n\r[%s] Erase sector failed", __FUNCTION__);
			goto update_ota_exit;
		}
	}
	// Write update sectors
	address = flash_SectorAddress(UPDATE_SECTOR);
	printf("\n\r");
	while(ctx.bytes < ctx.size){
		read_bytes = updater_read_bytes(&ctx, (unsigned char*)buf, BUF_SIZE);
		if(read_bytes == 0) break; // Read end
		if(read_bytes < 0){
			printf("\n\r[%s] Read socket failed", __FUNCTION__);
			goto update_ota_exit;
		}
		if(flash_Wrtie(address + size, buf, read_bytes) < 0){
			printf("\n\r[%s] Write sector failed", __FUNCTION__);
			goto update_ota_exit;
		}
		size += read_bytes;
		for(i = 0; i < read_bytes; i ++)
			checksum += buf[i];
		printf("\rUpdate file size = %d/%d bytes   ", ctx.bytes, ctx.size);
	}
	printf("\n\r[%s] ctx checksum = %08x, computed checksum = %08x\n", __FUNCTION__, ctx.checksum, checksum);
	if(checksum != ctx.checksum){
		printf("\n\r[%s] Checksum error", __FUNCTION__);
		goto update_ota_exit;
	}
#if CONFIG_WRITE_MAC_TO_FLASH
	//Write MAC address
	if(!(mac[0]==0xff&&mac[1]==0xff&&mac[2]==0xff&&mac[3]==0xff&&mac[4]==0xff&&mac[5]==0xff)){
		if(flash_Wrtie(FLASH_ADD_STORE_MAC, mac, ETH_ALEN) < 0){
			printf("\n\r[%s] Write MAC failed", __FUNCTION__);
			goto update_ota_exit;
		}	
	}
#endif
	// Write config sectors
	address = flash_SectorAddress(CONFIG_SECTOR);
	if( (flash_Wrtie(address, (char*)&size, 4) < 0) || 
		(flash_Wrtie(address+4, (char*)&checksum, 4) < 0) ){
		printf("\n\r[%s] Write sector failed", __FUNCTION__);
		goto update_ota_exit;
	}
	printf("\n\r[%s] Update OTA success!", __FUNCTION__);
	
update_ota_exit:
	updater_free_ctx(&ctx);
update_ota_exit_1:
	if(buf)
		update_free(buf);
	if(param)
		update_free(param);
	TaskOTA = NULL;
	printf("\n\r[%s] Update task exit", __FUNCTION__);
	vTaskDelete(NULL);
	return;
}
#elif defined(STM32f1xx)
static void update_ota_cloud_task(void *param)
{
	struct updater_ctx ctx;
	char *buf, flag_a = 0;
	int read_bytes, size = 0, i;
	uint32_t address, checksum = 0;
	update_cfg_cloud_t *cfg = (update_cfg_cloud_t*)param;
	uint16_t a = 0;
#if CONFIG_WRITE_MAC_TO_FLASH
	char mac[ETH_ALEN];
#endif	
	printf("\n\r[%s] Update task start", __FUNCTION__);
	buf = update_malloc(BUF_SIZE);
	if(!buf){
		printf("\n\r[%s] Alloc buffer failed", __FUNCTION__);
		goto update_ota_exit_1;
	}
	// Init ctx
	if(updater_init_ctx(&ctx, (char*)cfg->repository, (char*)cfg->file_path) != 0) {
		printf("\n\r[%s] Cloud ctx init failed", __FUNCTION__);
		goto update_ota_exit_1;
	}
	printf("\n\r[%s] Firmware link: %s, size = %d bytes, checksum = 0x%08x, version = %s\n", __FUNCTION__, 
		ctx.link, ctx.size, ctx.checksum, ctx.version);

	// Erase config sectors
	for(i = CONFIG_SECTOR; i < APPLICATION_SECTOR; i += FLASH_PAGE_SIZE){
		if(flash_EraseSector(i) < 0){
			printf("\n\r[%s] Erase sector failed", __FUNCTION__);
			goto update_ota_exit;
		}
	}
#if CONFIG_WRITE_MAC_TO_FLASH
	// Read MAC address
 	if(flash_Read(FLASH_ADD_STORE_MAC, mac, ETH_ALEN) < 0){
		printf("\n\r[%s] Read MAC error", __FUNCTION__);
		goto update_ota_exit;
 	}	
#endif

	// Erase update sectors
	for(i = UPDATE_SECTOR; i < FLASH_Sector_0 + FLASH_SIZE; i += FLASH_PAGE_SIZE){
		if(flash_EraseSector(i) < 0){
			printf("\n\r[%s] Erase sector failed", __FUNCTION__);
			goto update_ota_exit;
		}
	}
	// Write update sectors
	address = UPDATE_SECTOR;
	printf("\n\r");
	while(ctx.bytes < ctx.size){
		read_bytes = updater_read_bytes(&ctx, (unsigned char*)buf, BUF_SIZE);
		if(read_bytes == 0) break; // Read end
		if(read_bytes < 0){
			printf("\n\r[%s] Read socket failed", __FUNCTION__);
			goto update_ota_exit;
		}
		if(flag_a == 0){
			if(read_bytes % 2 != 0){
				a = buf[read_bytes - 1];
				flag_a = 1;
				if(flash_Wrtie(address + size, buf, read_bytes - 1) < 0){
					printf("\n\r[%s] Write sector failed", __FUNCTION__);
					goto update_ota_exit;
				}
				size += read_bytes - 1;
			}
			else{
				if(flash_Wrtie(address + size, buf, read_bytes) < 0){
					printf("\n\r[%s] Write sector failed", __FUNCTION__);
					goto update_ota_exit;
				}
				size += read_bytes;
			}
		}
		else{
			a = buf[0]<< 8 |a;
			if(flash_Wrtie(address + size, (char*)(&a), 2) < 0){
				printf("\n\r[%s] Write sector failed", __FUNCTION__);
				goto update_ota_exit;
			}
			size += 2;
			a = 0;
			flag_a = 0;
			if((read_bytes - 1) % 2 != 0){
				a = buf[read_bytes - 1];
				flag_a = 1;
				if(flash_Wrtie(address + size, buf + 1, read_bytes - 2) < 0){
					printf("\n\r[%s] Write sector failed", __FUNCTION__);
					goto update_ota_exit;
				}
				size += read_bytes - 2;
			}
			else{ 
				if(flash_Wrtie(address + size, buf + 1, read_bytes - 1) < 0){
					printf("\n\r[%s] Write sector failed", __FUNCTION__);
					goto update_ota_exit;
				}
				size += read_bytes - 1;
			}
		}	
		for(i = 0; i < read_bytes; i ++)
			checksum += buf[i];
		printf("\rUpdate file size = %d/%d bytes   ", ctx.bytes, ctx.size);
	}
	if(flag_a){
		if(flash_Wrtie(address + size, (char*)(&a), 2) < 0){
			printf("\n\r[%s] Write sector failed", __FUNCTION__);
			goto update_ota_exit;
		}
		size += 1;
	}
	printf("\n\r[%s] ctx checksum = %08x, computed checksum = %08x\n", __FUNCTION__, ctx.checksum, checksum);
	if(checksum != ctx.checksum){
		printf("\n\r[%s] Checksum error", __FUNCTION__);
		goto update_ota_exit;
	}
#if CONFIG_WRITE_MAC_TO_FLASH
	//Write MAC address
	if(!(mac[0]==0xff&&mac[1]==0xff&&mac[2]==0xff&&mac[3]==0xff&&mac[4]==0xff&&mac[5]==0xff)){
		if(flash_Wrtie(FLASH_ADD_STORE_MAC, mac, ETH_ALEN) < 0){
			printf("\n\r[%s] Write MAC failed", __FUNCTION__);
			goto update_ota_exit;
		}	
	}
#endif

	// Write config sectors
	address = CONFIG_SECTOR;
	if( (flash_Wrtie(address, (char*)&size, 4) < 0) || 
		(flash_Wrtie(address+4, (char*)&checksum, 4) < 0) ){
		printf("\n\r[%s] Write sector failed", __FUNCTION__);
		goto update_ota_exit;
	}
	printf("\n\r[%s] Update OTA success!", __FUNCTION__);
	
update_ota_exit:
	updater_free_ctx(&ctx);
update_ota_exit_1:
	if(buf)
		update_free(buf);
	if(param)
		update_free(param);
	TaskOTA = NULL;
	printf("\n\r[%s] Update task exit", __FUNCTION__);
	vTaskDelete(NULL);
	return;
}

#endif

//---------------------------------------------------------------------
int update_ota_cloud(char *repository, char *file_path)
{
	update_cfg_cloud_t *pUpdateCfg;
	
	if(TaskOTA){
		printf("\n\r[%s] Update task has created.", __FUNCTION__);
		return 0;
	}
	pUpdateCfg = update_malloc(sizeof(update_cfg_cloud_t));
	if(pUpdateCfg == NULL){
		printf("\n\r[%s] Alloc update cfg failed.", __FUNCTION__);
		goto exit;
	}
	if(strlen(repository) > (REPOSITORY_LEN-1)){
		printf("\n\r[%s] Repository length is too long.", __FUNCTION__);
		goto exit;
	}
	if(strlen(file_path) > (FILE_PATH_LEN-1)){
		printf("\n\r[%s] File path length is too long.", __FUNCTION__);
		goto exit;
	}
	strcpy((char*)pUpdateCfg->repository, repository);
	strcpy((char*)pUpdateCfg->file_path, file_path);
	  	
	if(xTaskCreate(update_ota_cloud_task, "OTA_server", STACK_SIZE, pUpdateCfg, TASK_PRIORITY, &TaskOTA) != pdPASS){
		printf("\n\r[%s] Create update task failed", __FUNCTION__);
		goto exit;
	}

exit:
	update_free(pUpdateCfg);
	return 0;
}
#endif // #if (SERVER_TYPE == SERVER_CLOUD)

//---------------------------------------------------------------------
void cmd_update(int argc, char **argv)
{
//	printf("\n\r[%s] Firmware A", __FUNCTION__);
#if (SERVER_TYPE == SERVER_LOCAL)
	int port;
	if(argc != 3){
		printf("\n\r[%s] Usage: update IP PORT", __FUNCTION__);
		return;
	}
	port = atoi(argv[2]);
	update_ota_local(argv[1], port);
#endif
#if (SERVER_TYPE == SERVER_CLOUD)
	if(argc != 3){
		printf("\n\r[%s] Usage: update REPOSITORY FILE_PATH", __FUNCTION__);
		return;
	}
	update_ota_cloud(argv[1], argv[2]);
#endif
}

// chose to boot ota image or not
void cmd_ota_image(bool cmd){
	flash_t	flash;
	uint32_t Part1Addr = 0xFFFFFFFF,Part2Addr = 0xFFFFFFFF;
	uint8_t *pbuf = NULL;
	
	device_mutex_lock(RT_DEV_LOCK_FLASH);
	flash_read_word(&flash, 0x18, &Part1Addr);
	Part1Addr = (Part1Addr&0xFFFF)*1024;	// first partition
	flash_read_word(&flash, OFFSET_DATA, &Part2Addr);
	device_mutex_unlock(RT_DEV_LOCK_FLASH);

	if(Part2Addr == ~0x0)
		return;

	pbuf = update_malloc(FLASH_SECTOR_SIZE);
	if(!pbuf) return;
	
	device_mutex_lock(RT_DEV_LOCK_FLASH);	
	flash_stream_read(&flash, Part2Addr, FLASH_SECTOR_SIZE, pbuf);
	if (cmd == 1)
		memcpy((char*)pbuf+8, "81958711", 8);
	else
		memcpy((char*)pbuf+8, "01958711", 8);

	flash_erase_sector(&flash, Part2Addr);
	flash_stream_write(&flash, Part2Addr, FLASH_SECTOR_SIZE, pbuf);
	device_mutex_unlock(RT_DEV_LOCK_FLASH);
	
#if SWAP_UPDATE
	device_mutex_lock(RT_DEV_LOCK_FLASH);
	flash_stream_read(&flash, Part1Addr, FLASH_SECTOR_SIZE, pbuf);
	if (cmd == 1)
		memcpy((char*)pbuf+8, "01958711", 8);
	else
		memcpy((char*)pbuf+8, "81958711", 8);

	flash_erase_sector(&flash, Part1Addr);
	flash_stream_write(&flash, Part1Addr, FLASH_SECTOR_SIZE, pbuf);
	device_mutex_unlock(RT_DEV_LOCK_FLASH);
#endif
	update_free(pbuf);
}
//---------------------------------------------------------------------

