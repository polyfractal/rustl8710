/*******************************************************************************
*
* Project: Realtek Ameba flash loader project
*
* Description: Memory-specific routines for Flash Loader.
*
* Copyright by Diolan Ltd. All rights reserved.
*
*******************************************************************************/
#include <string.h>
#include <stdlib.h>
#include "flash_loader.h"
#include "flash_loader_extra.h"

#include "rtl8195a.h"
//#include "rtl8195a/hal_misc.h"
//#include "rtl8195a/hal_spi_flash.h"
//#include "rtl8195a/core_cm3.h"

extern VOID
HalReInitPlatformLogUart(
    VOID
);

extern VOID 
SpicLoadInitParaFromClockRtl8195A
(
    IN  u8 CpuClkMode,
    IN  u8 BaudRate,
    IN  PSPIC_INIT_PARA pSpicInitPara
);

extern VOID 
SpicWaitBusyDoneRtl8195A();

extern VOID 
SpicWaitWipDoneRtl8195A
(
    IN  SPIC_INIT_PARA SpicInitPara
);

extern VOID 
SpicTxCmdRtl8195A
(
    IN  u8 cmd, 
    IN  SPIC_INIT_PARA SpicInitPara 
);

extern u8 
SpicGetFlashStatusRtl8195A
(
    IN  SPIC_INIT_PARA SpicInitPara    
);

__no_init unsigned int flash_loc;
__no_init unsigned int erase_loc;
__no_init unsigned int is_cascade;
__no_init unsigned int is_head;
__no_init unsigned int is_dbgmsg;
__no_init unsigned int is_erasecal;
__no_init unsigned int img2_addr;

int rest_count;
int first_write;
SPIC_INIT_PARA SpicInitPara;

#define PATTERN_1 0x96969999
#define PATTERN_2 0xFC66CC3F
#define PATTERN_3 0x03CC33C0
#define PATTERN_4 0x6231DCE5


#define DBGPRINT(fmt, arg...) do{ if( is_dbgmsg ) DiagPrintf(fmt, ##arg);}while(0)

//unsigned int fw_head[4] = {PATTERN_1, PATTERN_2, PATTERN_3, PATTERN_4};
unsigned int seg_head[4] = {0,0,0,0};

extern SPIC_INIT_PARA SpicInitCPUCLK[4];

void dump_flash_header(void)
{
  		uint32_t data;
        data = HAL_READ32(SPI_FLASH_BASE, 0);
		DBGPRINT("\n\r 0: %x", data);
		data = HAL_READ32(SPI_FLASH_BASE, 4);
		DBGPRINT("\n\r 4: %x", data);
		data = HAL_READ32(SPI_FLASH_BASE, 8);
		DBGPRINT("\n\r 8: %x", data);
		data = HAL_READ32(SPI_FLASH_BASE, 12);
		DBGPRINT("\n\r 12: %x", data);
}

const char* find_option(char* option, int withValue, int argc, char const* argv[])
{
    int i;
    for (i = 0; i < argc; i++) {
        if (strcmp(option, argv[i]) == 0){
            if (withValue) {
                if (i + 1 < argc) {
                    // The next argument is the value.
                    return argv[i + 1]; 
                }
                else {
                    // The option was found but there is no value to return.
                    return 0; 
                }
            }
            else
            {
                // Return the flag argument itself just to get a non-zero pointer.
                return argv[i]; 
            }
        }
    }
    return 0;
}

static VOID
FlashDownloadHalInitialROMCodeGlobalVar(VOID)
{
    // to initial ROM code using global variable
    ConfigDebugErr = _DBG_MISC_;
    ConfigDebugInfo= 0x0;
    ConfigDebugWarn= 0x0;
}

static VOID
FlashDownloadHalCleanROMCodeGlobalVar(VOID)
{
    ConfigDebugErr = 0x0;
    ConfigDebugInfo= 0x0;
    ConfigDebugWarn= 0x0;
}

// Please clean this Array
extern SPIC_INIT_PARA SpicInitParaAllClk[3][CPU_CLK_TYPE_NO];

u8 FlashType;
uint32_t FlashInit(void *base_of_flash, uint32_t image_size, uint32_t link_address, uint32_t flags, int argc, char const *argv[])
{
    u8 CpuClk;
    u8 Value8, InitBaudRate;
	
	char *addr;
		
    SPIC_INIT_PARA InitCPUCLK[4] = {
                                      {0x1,0x1,0x5E,0},  //default cpu 41, baud 1
                                      {0x1,0x1,0x0,0},   //cpu 20.8 , baud 1
                                      {0x1,0x2,0x23,0},   //cpu 83.3, baud 1
                                      {0x1,0x5,0x5,0},                                                        
                                };    
	memcpy(SpicInitCPUCLK, InitCPUCLK, sizeof(InitCPUCLK));
	memset(SpicInitParaAllClk, 0, sizeof(SPIC_INIT_PARA)*3*CPU_CLK_TYPE_NO);

    SpicInitPara.BaudRate = 0;
    SpicInitPara.DelayLine = 0;
    SpicInitPara.RdDummyCyle = 0;
    SpicInitPara.Rsvd = 0;   

	if(find_option( "--erase_cal", 0, argc, argv ))
		is_erasecal = 1;
	else
		is_erasecal = 0;
	
	if(find_option( "--cascade", 0, argc, argv ))
		is_cascade = 1;
	else
		is_cascade = 0;
	
	if(find_option( "--head", 0, argc, argv ))
		is_head = 1;
	else
		is_head = 0;	
	
	if(find_option( "--dbgmsg", 0, argc, argv ))
		is_dbgmsg = 1;
	else
		is_dbgmsg = 0;
	
	if( (addr = (char*)find_option( "--img2_addr", 1, argc, argv))){
		img2_addr = strtod(addr, NULL)/1024;
		DBG_8195A(" image2 start address = %s, offset = %x\n\r", addr, img2_addr);
	}else
		img2_addr = 0;

	memset((void *) 0x10000300, 0, 0xbc0-0x300);
		
    // Load Efuse Setting
    Value8 = ((HAL_READ32(SYSTEM_CTRL_BASE, REG_SYS_EFUSE_SYSCFG6) & 0xFF000000)
                >> 24);

    InitBaudRate = ((Value8 & 0xC)>>2);

    // Make sure InitBaudRate != 0
    if (!InitBaudRate) {
        InitBaudRate +=1;
    }
    
    CpuClk = ((HAL_READ32(SYSTEM_CTRL_BASE, REG_SYS_CLK_CTRL1) & (0x70)) >> 4);
    SpicLoadInitParaFromClockRtl8195A(CpuClk, InitBaudRate, &SpicInitPara);

	// Reset to low speed
	HAL_WRITE32(SYSTEM_CTRL_BASE, REG_SYS_CLK_CTRL1, 0x21);	
	
	FlashDownloadHalInitialROMCodeGlobalVar();
	
    //2 Need Modify
    VectorTableInitRtl8195A(0x1FFFFFFC);

    //3 Initial Log Uart    
    PatchHalInitPlatformLogUart();

    //3 Initial hardware timer
    PatchHalInitPlatformTimer();

    DBG_8195A("\r\n===> Flash Init \n\r");
    //4 Initialize the flash first
    if (HAL_READ32(REG_SOC_FUNC_EN,BIT_SOC_FLASH_EN) & BIT_SOC_FLASH_EN) {
        FLASH_FCTRL(OFF);
    }
	
    FLASH_FCTRL(ON);    
    ACTCK_FLASH_CCTRL(ON);
    SLPCK_FLASH_CCTRL(ON);
    PinCtrl(SPI_FLASH,S0,ON);

    PatchSpicInitRtl8195A(SpicInitPara.BaudRate, SpicOneBitMode);

	SpicFlashInitRtl8195A(SpicOneBitMode);
	
	FlashType = SpicInitParaAllClk[SpicOneBitMode][0].flashtype;
	
	char* vendor[] = {"Others", "MXIC", "Winbond", "Micron"};
	DBG_8195A("\r\n===> Flash Init Done, vendor: \x1b[32m%s\x1b[m \n\r", vendor[FlashType]);
	
    first_write = 1;
    rest_count = theFlashParams.block_size;
    seg_head[0] = theFlashParams.block_size;
    seg_head[1] = theFlashParams.offset_into_block;
	if(is_head){
		seg_head[2] = 0xFFFF0000|img2_addr;
		seg_head[3] = 0xFFFFFFFF;
	}else{
		if(is_cascade==0){
			// Image2 signature
			seg_head[2] = 0x35393138;	//8195
			seg_head[3] = 0x31313738;	//8711
		}else{
		seg_head[2] = 0xFFFFFFFF;	
	seg_head[3] = 0xFFFFFFFF;
		}
	}
	
	//DBG_8195A("link_address = %08x, flags = %08x ...\n\r", link_address, flags);
	
	if(is_cascade==0 && is_head==0){
		// mark partition 2 to old if existing
		unsigned int ota_addr = HAL_READ32(SPI_FLASH_BASE, 0x9000);
		
		//check OTA address valid
		if( ota_addr == 0xFFFFFFFF || ota_addr > 64*1024*1024 ){
			DBG_8195A("\r\n\x1b[31mOTA addr %8x is invalid\x1b[m\n\r", ota_addr );
			DBG_8195A("\x1b[31mOTA addr %8x is invalid\x1b[m\n\r", ota_addr );
			DBG_8195A("\x1b[31mOTA addr %8x is invalid\x1b[m\n\r", ota_addr );
			DBG_8195A("continue downloading...\n\r" );
			return RESULT_OK;
		}else{
			DBG_8195A("\x1b[36mOTA addr is %x \x1b[m\n\r", ota_addr );
		}
			
		int sig0 = HAL_READ32(SPI_FLASH_BASE, ota_addr+8);
		int sig1 = HAL_READ32(SPI_FLASH_BASE, ota_addr+12);
			
		if(sig0==0x35393138 && sig1==0x31313738){
			DBG_8195A("\r\n>>>> mark parition 2 as older \n\r" );
			HAL_WRITE32(SPI_FLASH_BASE, ota_addr+8, 0x35393130);	// mark to older version
			// wait spic busy done
			SpicWaitBusyDoneRtl8195A();
			// wait flash busy done (wip=0)
			if(FlashType == FLASH_MICRON)
                SpicWaitOperationDoneRtl8195A(SpicInitPara);
            else
                SpicWaitWipDoneRefinedRtl8195A(SpicInitPara);
		}	
	}
	dump_flash_header();
    //SpicEraseFlashRtl8195A();
    return RESULT_OK;
}

void write_spi_flash(uint32_t data)
{
        HAL_WRITE32(SPI_FLASH_BASE, flash_loc, data);
        // wait spic busy done
        SpicWaitBusyDoneRtl8195A();

        // wait flash busy done (wip=0)
        if(FlashType == FLASH_MICRON)
        	SpicWaitOperationDoneRtl8195A(SpicInitPara);
        else
        	SpicWaitWipDoneRefinedRtl8195A(SpicInitPara);
        flash_loc+=4;
}

uint32_t FlashWrite(void *block_start, uint32_t offset_into_block, uint32_t count, char const *buffer)
{
    int write_cnt=0;
    uint32_t* buffer32 = (uint32_t*)buffer;
    
    DBG_8195A("\r\n===> Flash Write, start %x, addr %x, offset %d, count %d, buf %x\n\r", block_start, flash_loc, offset_into_block, count, buffer);

    if(first_write){
		if(!is_cascade){
			flash_loc = (unsigned int)block_start;
		}
		if(is_head){
			unsigned int fw_head[4] = {PATTERN_1, PATTERN_2, PATTERN_3, PATTERN_4};
			DBGPRINT("Write FW header....");
			flash_loc=0;
			write_spi_flash(fw_head[0]);
			write_spi_flash(fw_head[1]);
			write_spi_flash(fw_head[2]);
			write_spi_flash(fw_head[3]);
			DBGPRINT("Write FW header.... %x %x %x %x --> Done\n\r", fw_head[0], fw_head[1], fw_head[2], fw_head[3]);
		}
        DBGPRINT("Write SEG header....");
        first_write = 0;
        write_spi_flash(seg_head[0]);
        write_spi_flash(seg_head[1]);
        write_spi_flash(seg_head[2]);
        write_spi_flash(seg_head[3]);
        DBGPRINT("Write SEG header.... %x %x %x %x --> Done\n\r", seg_head[0], seg_head[1], seg_head[2], seg_head[3]);
    }

    if(rest_count < count)
        count = rest_count;    
    
    // DO Write Here
    DBG_8195A("Write Binary....");
    while (write_cnt < count)
    {
        write_spi_flash(*buffer32);
        write_cnt += 4;
        buffer32++; 
    }
    DBG_8195A("Write Binary....Done\n\r");
    
    rest_count-=count;
    DBG_8195A("\r\n<=== Flash Write Done %x\n\r", flash_loc);
    DBGPRINT("first 4 bytes %2x %2x %2x %2x\n\r", buffer[0],buffer[1],buffer[2],buffer[3]);
    DBGPRINT("last 4 bytes %2x %2x %2x %2x\n\r", buffer[count-4],buffer[count-3],buffer[count-2],buffer[count-1]);   
    return RESULT_OK;
}

uint32_t FlashErase(void *block_start, uint32_t block_size)
{
	if(is_head == 1)
		erase_loc = 0;
	
	if(!is_cascade)
		erase_loc = (unsigned int)block_start;
	
	if(erase_loc != 0xa000){
		SpicSectorEraseFlashRtl8195A(erase_loc);
		DBGPRINT("@erase %x, size %d, fw offset %x\n\r", erase_loc, block_size, block_start);
	}else{
		if(is_erasecal){
			SpicSectorEraseFlashRtl8195A(erase_loc);
			DBGPRINT("@erase %x, size %d, fw offset %x\n\r", erase_loc, block_size, block_start);
		}
	}
	erase_loc += 4096;
	
    return 0;
}

