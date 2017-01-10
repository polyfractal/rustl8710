/********************************************************************************
 * Copyright (c) 2014, Realtek Semiconductor Corp.
 * All rights reserved.
 *
 * This module is a confidential and proprietary property of RealTek and
 * possession or use of this module requires written permission of RealTek.
 *******************************************************************************
 */

#include "xmport_uart.h"
#include "xmport_loguart.h"
#include "rtl8195a.h"
#include "xmodem.h"
#include "xmport_uart.h"
#include "hal_spi_flash.h"
#include "rtl8195a_spi_flash.h"
#include <platform/platform_stdlib.h>

#if /*CONFIG_PERI_UPDATE_IMG*/1

#define IMG1_SIGN_OFFSET        0x34

enum {
  XMODEM_UART_0     = 0,   
  XMODEM_UART_1     = 1,   
  XMODEM_UART_2     = 2,   
  XMODEM_LOG_UART   = 3   
};

FWU_DATA_SECTION char xMFrameBuf[XM_BUFFER_SIZE];
FWU_DATA_SECTION XMODEM_CTRL xMCtrl;

FWU_DATA_SECTION static u32 fw_img1_size;
FWU_DATA_SECTION static u32 fw_img2_size;
FWU_DATA_SECTION static u32 fw_img2_addr;
FWU_DATA_SECTION static u32 fw_img3_size;
FWU_DATA_SECTION static u32 fw_img3_addr;
FWU_DATA_SECTION static u32 flash_wr_offset;
FWU_DATA_SECTION static u32 flash_erased_addr;
FWU_DATA_SECTION static u8  start_with_img1;
FWU_DATA_SECTION static u32 flash_wr_err_cnt;

FWU_DATA_SECTION HAL_RUART_ADAPTER xmodem_uart_adp; // we can dynamic allocate memory for this object to save memory

FWU_RODATA_SECTION const char Img2Signature[8]="81958711";
extern u32 SpicCalibrationPattern[4];
extern const u8 ROM_IMG1_VALID_PATTEN[];
extern HAL_RUART_ADAPTER *pxmodem_uart_adp;

#ifdef CONFIG_GPIO_EN
extern HAL_GPIO_ADAPTER gBoot_Gpio_Adapter;
extern PHAL_GPIO_ADAPTER _pHAL_Gpio_Adapter;
#endif

extern BOOLEAN SpicFlashInitRtl8195A(u8 SpicBitMode);
_LONG_CALL_
extern VOID SpicWaitBusyDoneRtl8195A(VOID);
extern VOID SpicWaitWipDoneRefinedRtl8195A(SPIC_INIT_PARA SpicInitPara);

VOID WriteImg1Sign(u32 Image2Addr);

FWU_TEXT_SECTION void FWU_WriteWord(u32 Addr, u32 FData)
{
    SPIC_INIT_PARA SpicInitPara;
    
    HAL_WRITE32(SPI_FLASH_BASE, Addr, FData);
    // Wait spic busy done
    SpicWaitBusyDoneRtl8195A();
    // Wait flash busy done (wip=0)
    SpicWaitWipDoneRefinedRtl8195A(SpicInitPara);
}

FWU_TEXT_SECTION u32 xModem_MemCmp(const u32 *av, const u32 *bv, u32 len)
{
    const u32 *a = av;
    const u32 *b = (u32*)((u8*)bv+SPI_FLASH_BASE);
    u32 len4b = len >> 2;
    u32 i;
    
    for (i=0; i<len4b; i++) {
        if (a[i] != b[i]) {
            DBG_MISC_ERR("OTU: Flash write check error @ 0x%08x\r\n", (u32)(&b[i]));
            return ((u32)(&b[i])); 
        }
    }
    return 0;
}

FWU_TEXT_SECTION
u32 xModem_Frame_Dump(char *ptr,  unsigned int frame_num)
{
    u32 i;
    
    DiagPrintf("===== Frme %d ======\r\n", frame_num);

    for(i=0;i<128;i+=16) {
        DiagPrintf("%02x: ", i);
        DiagPrintf("%02x %02x %02x %02x %02x %02x %02x %02x  ",
            *(ptr+i),*(ptr+i+1),*(ptr+i+2),*(ptr+i+3),*(ptr+i+4),*(ptr+i+5),*(ptr+i+6),*(ptr+i+7));
        DiagPrintf("%02x %02x %02x %02x %02x %02x %02x %02x \r\n",
            *(ptr+i+8),*(ptr+i+9),*(ptr+i+10),*(ptr+i+11),*(ptr+i+12),*(ptr+i+13),*(ptr+i+14),*(ptr+i+15));
    }

    return 0;
}

FWU_TEXT_SECTION
u32 xModem_Frame_MemWrite(char *ptr,  unsigned int frame_num, unsigned int frame_size)
{
    u32 idx=0;
    u32 skip_sz=0;

    // "flash_wr_offset" here is used as the skip bytes from the head
    while ((flash_wr_offset > 0) && (idx < frame_size)) {
        flash_wr_offset--;
        idx++;
        skip_sz++;
    }

    // "fw_img2_size" here is used as the memory length to write
    // "fw_img2_addr" is used as the start memory address to write
    while (idx < frame_size) {
        if (fw_img2_size == 0) {
            return idx;
        }

        if (((fw_img2_addr & 0x03) == 0) && 
             (fw_img2_size > 3) && 
             ((frame_size - idx) > 3)) {
            // write address is 4-byte aligned
            *((u32*)fw_img2_addr) = (*((u32*)(ptr+idx)));
            fw_img2_addr += 4;
            fw_img2_size -= 4;
            idx += 4;
        } else if (fw_img2_size > 0){
            *((u8*)fw_img2_addr) = (*((u8*)(ptr+idx)));
            fw_img2_addr++;
            fw_img2_size--;
            idx++;        
        }
    }

    return (idx - skip_sz);
}

FWU_TEXT_SECTION
u32 xModem_Frame_FlashWrite(char *ptr,  unsigned int frame_num, unsigned int frame_size)
{
    u32 idx=0;
    u32 skip_sz=0;
    u32 temp;
    u32 i;

    // "flash_wr_offset" here is used as the skip bytes from the head
    while ((flash_wr_offset > 0) && (idx < frame_size)) {
        flash_wr_offset--;
        idx++;
        skip_sz++;
    }

    // "fw_img2_size" here is used as the memory length to write
    // "fw_img2_addr" is used as the start memory address to write    
    while (idx < frame_size) {
        if (fw_img2_size == 0) {
            return idx;
        }
        
        if ((fw_img2_size > 3) && ((frame_size - idx) > 3)) {
            FWU_WriteWord(fw_img2_addr, (*((u32*)(ptr+idx))));
            fw_img2_addr += 4;
            fw_img2_size -= 4;
            idx += 4;
        } else {
            temp = 0xFFFFFFFF;
            for (i=0;i<4;i++) {
                // Just for little endian
                *((((u8*)&temp) + i)) = (*((u8*)(ptr+idx)));
                idx++;        
                fw_img2_size--;
                if ((fw_img2_size == 0) || (idx >= frame_size)) {
                    break;
                }
            }
            FWU_WriteWord(fw_img2_addr, temp);
            fw_img2_addr += 4;
        }
    }

    return (idx - skip_sz);
}

FWU_TEXT_SECTION
u32 xModem_Frame_Img2(char *ptr,  unsigned int frame_num, unsigned int frame_size)    
{
    u32 address;
    u32 ImageIndex=0;
    u32 rx_len=0;
    u32 *chk_sr;
    u32 *chk_dr;
    u32 err_addr;
    
    if (frame_num == 1) {
        // Parse Image2 header
        flash_wr_offset = fw_img2_addr;
        fw_img2_size = rtk_le32_to_cpu(*((u32*)ptr)) + 0x10;
        if ((fw_img2_size & 0x03) != 0) {
            DBG_MISC_ERR("xModem_Frame_ImgAll Err#2: fw_img2_addr=0x%x fw_img2_size(%d) isn't 4-bytes aligned\r\n", fw_img2_addr, fw_img2_size);
            fw_img1_size = 0;
            fw_img2_size = 0;
            return rx_len;
        }
        
        if (fw_img2_size > (2*1024*1024)) {
            DBG_MISC_ERR("xModem_Frame_ImgAll Image2 to Big: fw_img2_addr=0x%x fw_img2_size(%d) \r\n", fw_img2_addr, fw_img2_size);
            fw_img1_size = 0;
            fw_img2_size = 0;
            return rx_len;
        }
        fw_img3_addr = fw_img2_addr + fw_img2_size;
        
        // erase Flash first
        address = fw_img2_addr & (~0xfff);     // 4k aligned, 4k is the page size of flash memory
        while ((address) < (fw_img2_addr+fw_img2_size)) {
            SpicSectorEraseFlashRtl8195A(SPI_FLASH_BASE + address);
            address += 0x1000;
        }
        flash_erased_addr = address;
    }

    if (fw_img2_size > 0) {
        // writing image2
        chk_sr = (u32*)((u8*)ptr+ImageIndex);
        chk_dr = (u32*)flash_wr_offset;
        while (ImageIndex < frame_size) {
            FWU_WriteWord(flash_wr_offset, (*((u32*)(ptr+ImageIndex))));
            ImageIndex += 4;
            flash_wr_offset += 4;
            rx_len += 4;
            fw_img2_size -= 4;
            if (fw_img2_size == 0) {
                // Image2 write done,
                break;
            }
        }

        err_addr = xModem_MemCmp(chk_sr, chk_dr, (flash_wr_offset - (u32)chk_dr));
        if (err_addr) {
            flash_wr_err_cnt++;
        }
    }

    if (ImageIndex >= frame_size) {
        return rx_len;
    }

    // Skip the gap between image2 and image3, 
    // there is no gap in current image format
    if (flash_wr_offset < fw_img3_addr) {
        if ((flash_wr_offset + (frame_size-ImageIndex)) <= fw_img3_addr) {
            flash_wr_offset += (frame_size-ImageIndex);
            return rx_len;
        } else {
            while (ImageIndex < frame_size) {
                if (flash_wr_offset == fw_img3_addr) {
                    break;
                }
                ImageIndex += 4;
                flash_wr_offset += 4;
            }
        }
    }

    if (fw_img3_addr == flash_wr_offset) {
        if (ImageIndex < frame_size) {
            fw_img3_size = rtk_le32_to_cpu(*((u32*)(ptr+ImageIndex)));
            if (fw_img3_size == 0x1A1A1A1A) {
                // all padding bytes, no image3
                fw_img3_size = 0;
                return rx_len;                        
            }
            if ((fw_img3_size & 0x03) != 0) {
                DBG_MISC_ERR("xModem_Frame_ImgAll Err#5: fw_img3_addr=0x%x fw_img3_size(%d) isn't 4-bytes aligned\r\n", fw_img3_addr, fw_img3_size);
                fw_img3_size = 0;
                return rx_len;
            }
    
            if (fw_img3_size > (2*1024*1024)) {
                DBG_MISC_ERR("xModem_Frame_ImgAll Image3 to Big: fw_img3_addr=0x%x fw_img2_size(%d) \r\n", fw_img3_addr, fw_img3_size);
                fw_img3_size = 0;
                return rx_len;
            }
        
            // Flash sector erase for image2 writing
            if (flash_erased_addr >= fw_img3_addr) {
                address = flash_erased_addr;
            } else {
                address = fw_img3_addr & (~0xfff);     // 4k aligned, 4k is the page size of flash memory
            }
            
            while ((address) < (fw_img3_addr+fw_img3_size)) {
                DBG_MISC_INFO("Flash Erase: 0x%x\n", address);
#if 0
                if ((address & 0xFFFF) == 0) {
                    SpicBlockEraseFlashRtl8195A(SPI_FLASH_BASE + address);
                    address += 0x10000;  // 1 block = 64k bytes
                } 
                else 
#endif                    
                {
                    SpicSectorEraseFlashRtl8195A(SPI_FLASH_BASE + address);
                    address += 0x1000;  // 1 sector = 4k bytes
                }
            }
            flash_erased_addr = address;
        }
    }

    if (fw_img3_size > 0) {
        // writing image3
        chk_sr = (u32*)((u8*)ptr+ImageIndex);
        chk_dr = (u32*)flash_wr_offset;
        while (ImageIndex < frame_size) {
            FWU_WriteWord(flash_wr_offset, (*((u32*)(ptr+ImageIndex))));
            ImageIndex += 4;
            flash_wr_offset += 4;
            rx_len += 4;
            fw_img3_size -= 4;
            if (fw_img3_size == 0) {
                // Image3 write done,
                break;
            }
        }

        err_addr = xModem_MemCmp(chk_sr, chk_dr, (flash_wr_offset - (u32)chk_dr));
        if (err_addr) {
            flash_wr_err_cnt++;
        }
    }

    return rx_len;
}

FWU_TEXT_SECTION
u32 xModem_Frame_ImgAll(char *ptr,  unsigned int frame_num, unsigned int frame_size)    
{
    int i;
    u32 address;
    u32 img_size;
    u32 img_addr;
    u32 ImgIdx=0;
    u32 Img1Sign;
    u32 rx_len=0;
    u32 *chk_sr;
    u32 *chk_dr;
    u32 err_addr;

    if (frame_num == 1) {
        // Check is it the start patten of image1
        start_with_img1 = 1;
        for(i=0; i<4; i++) {
            Img1Sign = rtk_le32_to_cpu(*((u32*)(ptr + i*4)));
            if(Img1Sign != SpicCalibrationPattern[i]) {
                start_with_img1 = 0;
                break;
            }
        }

        // Get the image size: the first 4 bytes
        if (start_with_img1) {
            // Image1 + Image2
            // Check the Image1 Signature
            i=0;
            while (ROM_IMG1_VALID_PATTEN[i] != 0xff) {
                if (ptr[i+IMG1_SIGN_OFFSET] != ROM_IMG1_VALID_PATTEN[i]) {
                    // image1 validation patten miss match
                    DBG_MISC_ERR("xModem_Frame_ImgAll Err: Image1 Signature Incorrect\r\n");
                    fw_img1_size = 0;
                    fw_img2_size = 0;
                    fw_img2_addr = 0;
                    fw_img3_size = 0;
                    fw_img3_addr = 0;
                    return 0;
                } else {
                    // make the signature all 0xff for now, write the signature when image1 download is done
                    ptr[i+IMG1_SIGN_OFFSET] = 0xff;
                }
                i++;
            }
            
            flash_wr_offset = 0;
            fw_img1_size = rtk_le32_to_cpu(*((u32*)(ptr + 0x10))) + 0x20;
            if ((fw_img1_size & 0x03) != 0) {
                DBG_MISC_WARN("xModem_Frame_ImgAll Err: fw_img1_size(0x%x) isn't 4-bytes aligned\r\n", fw_img1_size);
                fw_img1_size = 0;
                fw_img2_size = 0;
                fw_img2_addr = 0;
                fw_img3_size = 0;
                fw_img3_addr = 0;
                return 0;
            }
            address = 0;
            img_size = fw_img1_size;
            img_addr = 0;
            fw_img2_addr = rtk_le16_to_cpu(*((u16*)(ptr + 0x18))) * 1024;
            if (fw_img2_addr == 0) {
                // it's old format: image1 & image2 is cascaded directly
                fw_img2_addr = fw_img1_size;
            }
            fw_img2_size = 0;
            DBG_MISC_INFO("Update Image All: Image1 Size=%d, Image2 Addr=0x%x\r\n", fw_img1_size, fw_img2_addr);
        } else {
            // It's image2(+image3) only
            if (fw_img2_addr == 0) {
                DBG_MISC_WARN("The single-image format in flash now, it cannot just update the image2\r\n");
                fw_img1_size = 0;
                fw_img2_size = 0;
                return rx_len;
            }
            
            flash_wr_offset = fw_img2_addr;
            fw_img1_size = 0;
            fw_img2_size = rtk_le32_to_cpu(*((u32*)ptr)) + 0x10;
            fw_img3_addr = fw_img2_addr + fw_img2_size;
            if ((fw_img2_size & 0x03) != 0) {
                DBG_MISC_ERR("xModem_Frame_ImgAll Err: fw_img2_size(0x%x) isn't 4-bytes aligned\r\n", fw_img2_size);
                fw_img1_size = 0;
                fw_img2_size = 0;
                return rx_len;
            }
            address = fw_img2_addr & (~0xfff);     // 4k aligned, 4k is the page size of flash memory
            img_size = fw_img2_size;
            img_addr = fw_img2_addr;

            DBG_MISC_INFO("Update Image2: Addr=0x%x, Size=%d\r\n", fw_img2_addr, fw_img2_size);

        }

        // erase Flash sector first
        while ((address) < (img_addr+img_size)) {
//            DBG_MISC_INFO("Flash Erase: 0x%x\n", address);
            if ((address >= 0x10000 ) && ((address & 0xFFFF) == 0)) {
                SpicBlockEraseFlashRtl8195A(SPI_FLASH_BASE + address);
                address += 0x10000;  // 1 Block = 64k bytes
            } 
            else 
            {
                SpicSectorEraseFlashRtl8195A(SPI_FLASH_BASE + address);
                address += 0x1000;  // 1 sector = 4k bytes
            }
        }
        flash_erased_addr = address;
    }

    {
        if (!start_with_img1) {
            if (fw_img2_size > 0) {
                chk_sr = (u32*)((u8*)ptr+ImgIdx);
                chk_dr = (u32*)flash_wr_offset;
                while (ImgIdx < frame_size) {
                    FWU_WriteWord(flash_wr_offset, (*((u32*)(ptr+ImgIdx))));
                    ImgIdx += 4;
                    flash_wr_offset += 4;
                    rx_len += 4;
                    fw_img2_size -= 4;
                    if (fw_img2_size == 0) {
                        break;
                    }

                }
                err_addr = xModem_MemCmp(chk_sr, chk_dr, (flash_wr_offset - (u32)chk_dr));
                if (err_addr) {
                    flash_wr_err_cnt++;
                }
            }
        } else {
            ImgIdx = 0;
            if (fw_img1_size > 0) {
                // still writing image1
                chk_sr = (u32*)((u8*)ptr+ImgIdx);
                chk_dr = (u32*)flash_wr_offset;
                while (ImgIdx < frame_size) {
                    FWU_WriteWord(flash_wr_offset, (*((u32*)(ptr+ImgIdx))));
                    ImgIdx += 4;
                    flash_wr_offset += 4;
                    rx_len += 4;
                    fw_img1_size -= 4;
                    if (fw_img1_size == 0) {
                        // Image1 write done,
                        break;
                    }
                }

                err_addr = xModem_MemCmp(chk_sr, chk_dr, (flash_wr_offset - (u32)chk_dr));
                if (err_addr) {
                    flash_wr_err_cnt++;
                } else {
                    if (fw_img1_size == 0) {
                        // Write Image1 signature
                        WriteImg1Sign(IMG1_SIGN_OFFSET);
                    }
                }
            }

            if (ImgIdx >= frame_size) {
                return rx_len;
            }

            if (fw_img2_addr == 0) {
                return rx_len;
            }

            // Skip the section of system data
            if (flash_wr_offset < fw_img2_addr) {
                if ((flash_wr_offset + (frame_size-ImgIdx)) <= fw_img2_addr) {
                    flash_wr_offset += (frame_size-ImgIdx);
                    return rx_len;
                } else {
                    while (ImgIdx < frame_size) {
                        if (flash_wr_offset == fw_img2_addr) {
                            break;
                        }
                        ImgIdx += 4;
                        flash_wr_offset += 4;
                        rx_len += 4;
                    }
                }
            }

            if (fw_img2_addr == flash_wr_offset) {
                if (ImgIdx < frame_size) {
                    fw_img2_size = rtk_le32_to_cpu(*((u32*)(ptr+ImgIdx))) + 0x10;
                    fw_img3_addr = fw_img2_addr + fw_img2_size;
                    if ((fw_img2_size & 0x03) != 0) {
                        DBG_MISC_ERR("xModem_Frame_ImgAll Err#2: fw_img2_addr=0x%x fw_img2_size(%d) isn't 4-bytes aligned\r\n", fw_img2_addr, fw_img2_size);
                        fw_img1_size = 0;
                        fw_img2_size = 0;
                        return rx_len;
                    }

                    if (fw_img2_size > (2*1024*1024)) {
                        DBG_MISC_ERR("xModem_Frame_ImgAll Image2 to Big: fw_img2_addr=0x%x fw_img2_size(%d) \r\n", fw_img2_addr, fw_img2_size);
                        fw_img1_size = 0;
                        fw_img2_size = 0;
                        return rx_len;
                    }
                
                    // Flash sector erase for image2 writing
                    if (flash_erased_addr >= fw_img2_addr) {
                        address = flash_erased_addr;
                    } else {
                        address = fw_img2_addr & (~0xfff);     // 4k aligned, 4k is the page size of flash memory
                    }
                    
                    while ((address) < (fw_img2_addr+fw_img2_size)) {
                        DBG_MISC_INFO("Flash Erase: 0x%x\n", address);
                        if ((address & 0xFFFF) == 0) {
                            SpicBlockEraseFlashRtl8195A(SPI_FLASH_BASE + address);
                            address += 0x10000;  // 1 block = 64k bytes
                        } 
                        else 
                        {
                            SpicSectorEraseFlashRtl8195A(SPI_FLASH_BASE + address);
                            address += 0x1000;  // 1 sector = 4k bytes
                        }
                    }
                    flash_erased_addr = address;
                }
            }

            if (fw_img2_size > 0) {
                // writing image2
                chk_sr = (u32*)((u8*)ptr+ImgIdx);
                chk_dr = (u32*)flash_wr_offset;
                while (ImgIdx < frame_size) {
                    FWU_WriteWord(flash_wr_offset, (*((u32*)(ptr+ImgIdx))));
                    ImgIdx += 4;
                    flash_wr_offset += 4;
                    rx_len += 4;
                    fw_img2_size -= 4;
                    if (fw_img2_size == 0) {
                        // Image2 write done,
                        break;
                    }
                }
                
                err_addr = xModem_MemCmp(chk_sr, chk_dr, (flash_wr_offset - (u32)chk_dr));
                if (err_addr) {
                    flash_wr_err_cnt++;
                }
            }

            if (ImgIdx >= frame_size) {
                return rx_len;
            }

            if (fw_img3_addr == flash_wr_offset) {
                if (ImgIdx < frame_size) {
                    fw_img3_size = rtk_le32_to_cpu(*((u32*)(ptr+ImgIdx)));
                    if (fw_img3_size == 0x1A1A1A1A) {
                        // all padding bytes, no image3
                        fw_img3_size = 0;
//                        DBG_8195A("No Img3\r\n");
                        return rx_len;                        
                    }
                    if ((fw_img3_size & 0x03) != 0) {
                        DBG_MISC_ERR("xModem_Frame_ImgAll Err#5: fw_img3_addr=0x%x fw_img3_size(%d) isn't 4-bytes aligned\r\n", fw_img3_addr, fw_img3_size);
                        fw_img3_size = 0;
                        return rx_len;
                    }
            
                    if (fw_img3_size > (2*1024*1024)) {
                        DBG_MISC_ERR("xModem_Frame_ImgAll Image3 to Big: fw_img3_addr=0x%x fw_img2_size(%d) \r\n", fw_img3_addr, fw_img3_size);
                        fw_img3_size = 0;
                        return rx_len;
                    }
                
                    // Flash sector erase for image2 writing
                    if (flash_erased_addr >= fw_img3_addr) {
                        address = flash_erased_addr;
                    } else {
                        address = fw_img3_addr & (~0xfff);     // 4k aligned, 4k is the page size of flash memory
                    }
                    
                    while ((address) < (fw_img3_addr+fw_img3_size)) {
                        DBG_MISC_INFO("Flash Erase: 0x%x\n", address);
                        if ((address & 0xFFFF) == 0) {
                            SpicBlockEraseFlashRtl8195A(SPI_FLASH_BASE + address);
                            address += 0x10000;  // 1 block = 64k bytes
                        } 
                        else 
                        {
                            SpicSectorEraseFlashRtl8195A(SPI_FLASH_BASE + address);
                            address += 0x1000;  // 1 sector = 4k bytes
                        }
                    }
                    flash_erased_addr = address;
                }
            }

            if (fw_img3_size > 0) {
                // writing image3
                chk_sr = (u32*)((u8*)ptr+ImgIdx);
                chk_dr = (u32*)flash_wr_offset;
                while (ImgIdx < frame_size) {
                    FWU_WriteWord(flash_wr_offset, (*((u32*)(ptr+ImgIdx))));
                    ImgIdx += 4;
                    flash_wr_offset += 4;
                    rx_len += 4;
                    fw_img3_size -= 4;
                    if (fw_img3_size == 0) {
                        // Image3 write done,
                        break;
                    }
                }
                err_addr = xModem_MemCmp(chk_sr, chk_dr, (flash_wr_offset - (u32)chk_dr));
                if (err_addr) {
                    flash_wr_err_cnt++;
                }
            }

        }
    }

    return rx_len;
}

FWU_TEXT_SECTION
s32
xModem_Init_UART_Port(u8 uart_idx, u8 pin_mux, u32 baud_rate)
{
    if (uart_idx <= XMODEM_UART_2) {
        // update firmware via generic UART
        pxmodem_uart_adp = &xmodem_uart_adp;    // we can use dynamic allocate to save memory
//        pxmodem_uart_adp = RtlZmalloc(sizeof(HAL_RUART_ADAPTER));
        xmodem_uart_init(uart_idx, pin_mux, baud_rate);
        xmodem_uart_func_hook(&(xMCtrl.ComPort));
    } else if(uart_idx == XMODEM_LOG_UART) {
        // update firmware via Log UART
//        DiagPrintf("Open xModem Transfer on Log UART...\r\n");
//        xmodem_loguart_init();
        xmodem_loguart_init(baud_rate);
        xmodem_loguart_func_hook(&(xMCtrl.ComPort));    
//        DiagPrintf("Please Start the xModem Sender...\r\n");
    } else {
        // invalid UART port
		DBG_MISC_ERR("xModem_Init_UART_Port: Invaild UART port(%d)\n", uart_idx);
        return -1;
    }

    return 0;
}

FWU_TEXT_SECTION
VOID
xModem_DeInit_UART_Port(u8 uart_idx)
{
    if (uart_idx <= XMODEM_UART_2) {    
        xmodem_uart_deinit();
    } else if (uart_idx == XMODEM_LOG_UART) {
        xmodem_loguart_deinit();
    }
}

FWU_TEXT_SECTION
__weak s32
UpdatedImg2AddrValidate(
    u32 Image2Addr,
    u32 DefImage2Addr,
    u32 DefImage2Size
)
{
    if (Image2Addr == 0xffffffff) {
        // Upgraded Image2 isn't exist
        return 0;   // invalid address
    }

    if ((Image2Addr & 0xfff) != 0) {
        // Not 4K aligned
        return 0;   // invalid address
    }

    if (Image2Addr <= DefImage2Addr) {
        // Updated image2 address must bigger than the addrss of default image2
        return 0;   // invalid address
    }

    if (Image2Addr < (DefImage2Addr+DefImage2Size)) {
        // Updated image2 overlap with the default image2
        return 0;   // invalid address
    }

    return 1;   // this address is valid    
}

FWU_TEXT_SECTION
__weak s32
Img2SignValidate(
    u32 Image2Addr
)
{
    u32 img2_sig[3];
    s32 sign_valid=0;
    
    // Image2 header: Size(4B) + Addr(4B) + Signature(8B)
    img2_sig[0] = HAL_READ32(SPI_FLASH_BASE, Image2Addr + 8);
    img2_sig[1] = HAL_READ32(SPI_FLASH_BASE, Image2Addr + 12);
    img2_sig[2] = 0; // end of string

    if (_memcmp((void*)img2_sig, (void*)Img2Signature, 8)) {
        DBG_MISC_INFO("Invalid Image2 Signature:%s\n", img2_sig);
    } else {
        sign_valid = 1;
    }

    return sign_valid;

}


FWU_TEXT_SECTION
VOID
MarkImg2SignOld(
    u32 Image2Addr
)
{
    u32 img2_sig;

    _memcpy((void*)&img2_sig, (void*)Img2Signature, 4);
    *((char*)(&img2_sig)) = '0';  // '8' -> the latest image; '0' -> the older image
    FWU_WriteWord((Image2Addr + 8), img2_sig);
}

FWU_TEXT_SECTION
VOID
WriteImg1Sign(
    u32 Image2Addr
)
{
    u32 img1_sig;

    _memcpy((void*)&img1_sig, (void*)ROM_IMG1_VALID_PATTEN, 4);
    FWU_WriteWord(IMG1_SIGN_OFFSET, img1_sig);
}

FWU_TEXT_SECTION
VOID
WriteImg2Sign(
    u32 Image2Addr
)
{
    u32 img2_sig[2];

    _memcpy((void*)img2_sig, (void*)Img2Signature, 8);
    FWU_WriteWord((Image2Addr + 8), img2_sig[0]);
    FWU_WriteWord((Image2Addr + 12), img2_sig[1]);
}

FWU_TEXT_SECTION
u32
SelectImg2ToUpdate(
    u32 *OldImg2Addr
)
{
    u32 DefImage2Addr=0xFFFFFFFF;  // the default Image2 addr.
    u32 SecImage2Addr=0xFFFFFFFF;  // the 2nd image2 addr.
    u32 ATSCAddr=0xFFFFFFFF; 
    u32 UpdImage2Addr;  // the addr of the image2 to be updated
    u32 DefImage2Len;
#ifdef CONFIG_UPDATE_TOGGLE_IMG2
    u32 SigImage0,SigImage1;
#endif

    *OldImg2Addr = 0;
    DefImage2Addr = (HAL_READ32(SPI_FLASH_BASE, 0x18)&0xFFFF) * 1024;
    if ((DefImage2Addr != 0) && ((DefImage2Addr < (16*1024*1024)))) {
        // Valid Default Image2 Addr: != 0 & located in 16M
        DefImage2Len = HAL_READ32(SPI_FLASH_BASE, DefImage2Addr);

        // Get the pointer of the upgraded Image2
        SecImage2Addr = HAL_READ32(SPI_FLASH_BASE, FLASH_SYSTEM_DATA_ADDR);

        if (UpdatedImg2AddrValidate(SecImage2Addr, DefImage2Addr, DefImage2Len)) {
            UpdImage2Addr = SecImage2Addr; // Update the 2nd image2
#ifdef CONFIG_UPDATE_TOGGLE_IMG2
            // read Part1/Part2 signature
            SigImage0 = HAL_READ32(SPI_FLASH_BASE, DefImage2Addr + 8);
            SigImage1 = HAL_READ32(SPI_FLASH_BASE, DefImage2Addr + 12);
            
            DBG_8195A("\n\rPart1 Sig %x", SigImage0);
            if(SigImage0==0x30303030 && SigImage1==0x30303030)
                ATSCAddr = DefImage2Addr;		// ATSC signature
            else if(SigImage0==0x35393138 && SigImage1==0x31313738)	
                *OldImg2Addr = DefImage2Addr;	// newer version, change to older version
            else
                UpdImage2Addr = DefImage2Addr;	// update to older version	
            
            SigImage0 = HAL_READ32(SPI_FLASH_BASE, SecImage2Addr + 8);
            SigImage1 = HAL_READ32(SPI_FLASH_BASE, SecImage2Addr + 12);
            DBG_8195A("\n\rPart2 Sig %x\n\r", SigImage0);
            if(SigImage0==0x30303030 && SigImage1==0x30303030)
                ATSCAddr = SecImage2Addr;		// ATSC signature
            else if(SigImage0==0x35393138 && SigImage1==0x31313738)
                *OldImg2Addr = SecImage2Addr;
            else
                UpdImage2Addr = SecImage2Addr;
            
            // update ATSC clear partitin first
            if(ATSCAddr != ~0x0){
                *OldImg2Addr = UpdImage2Addr;
                UpdImage2Addr = ATSCAddr;
            }
#endif // end of SWAP_UPDATE, wf, 1006
        } else {
            // The upgraded image2 isn't exist or invalid so we can just update the default image2
            UpdImage2Addr = DefImage2Addr; // Update the default image2
 #ifdef CONFIG_UPDATE_TOGGLE_IMG2
            *OldImg2Addr = DefImage2Addr;
#endif
       }
    } else {
        UpdImage2Addr = 0;
    }

    return UpdImage2Addr;
}


FWU_TEXT_SECTION
void OTU_FW_Update(u8 uart_idx, u8 pin_mux, u32 baud_rate)
{
    u32 wr_len;
    u32 OldImage2Addr=0;  // the addr of the image2 will become old one
    SPIC_INIT_PARA SpicInitPara;

    fw_img1_size = 0;
    fw_img2_size = 0;
    fw_img2_addr = 0;
    fw_img3_size = 0;
    fw_img3_addr = 0;
    flash_wr_offset = 0;
    flash_erased_addr = 0;
    start_with_img1 = 0;;
    flash_wr_err_cnt = 0;

    // Get the address of the image2 to be updated
	SPI_FLASH_PIN_FCTRL(ON);
	if (!SpicFlashInitRtl8195A(SpicOneBitMode)){
        SPI_FLASH_PIN_FCTRL(OFF);    
		DBG_MISC_ERR("OTU_FW_Update: SPI Init Fail!!!!!!\n");
        return;
	}
	SpicWaitWipDoneRefinedRtl8195A(SpicInitPara);

    printf("FW Update Over UART%d, PinMux=%d, Baud=%d\r\n", uart_idx, pin_mux, baud_rate);
    fw_img2_addr = SelectImg2ToUpdate(&OldImage2Addr);

    // Start to update the Image2 through xModem on peripheral device
    printf("FW Update Image2 @ 0x%x\r\n", fw_img2_addr);
    // We update the image via xModem on UART now, if we want to uase other peripheral device
    // to update the image then we need to redefine the API
    if (xModem_Init_UART_Port(uart_idx, pin_mux, baud_rate) < 0) {
        return;
    }

//    xModemStart(&xMCtrl, xMFrameBuf, xModem_Frame_ImgAll);    // Support Image format: Image1+Image2 or Image2 only
    xModemStart(&xMCtrl, xMFrameBuf, xModem_Frame_Img2);    // Support Image format: Image2 only
//    xModemStart(&xMCtrl, xMFrameBuf, xModem_Frame_Dump);    // for debugging
    wr_len = xModemRxBuffer(&xMCtrl, (2*1024*1024));
    xModemEnd(&xMCtrl);

    xModem_DeInit_UART_Port(uart_idx);

    if ((wr_len > 0) && (flash_wr_err_cnt == 0)) {
        // Firmware update OK, now write the signature to active this image
        WriteImg2Sign(fw_img2_addr);
#ifdef CONFIG_UPDATE_TOGGLE_IMG2
        // Mark the other image2 as old one by modify its signature
        if (OldImage2Addr != 0) {
            printf("Mark Image2 @ 0x%x as Old\r\n", OldImage2Addr);
            MarkImg2SignOld(OldImage2Addr);
        }
#endif        
    }
    printf("OTU_FW_Update Done, Write Len=%d\n", wr_len);
    SPI_FLASH_PIN_FCTRL(OFF);    
}

FWU_TEXT_SECTION
u8 OTU_check_gpio(void)
{
#ifdef CONFIG_GPIO_EN
    HAL_GPIO_PIN  GPIO_Pin;
    u8 enter_update;

    GPIO_Pin.pin_name = HAL_GPIO_GetIPPinName_8195a(0x21);; //pin PC_1
    GPIO_Pin.pin_mode = DIN_PULL_HIGH;

    _pHAL_Gpio_Adapter = &gBoot_Gpio_Adapter;
   
    HAL_GPIO_Init_8195a(&GPIO_Pin);
    if (HAL_GPIO_ReadPin_8195a(&GPIO_Pin) == GPIO_PIN_LOW) {
        enter_update = 1;
    }
    else {
        enter_update = 0;
    }
    HAL_GPIO_DeInit_8195a(&GPIO_Pin);

    _pHAL_Gpio_Adapter = NULL;
    return enter_update;
#else
    return 0;
#endif
}

FWU_TEXT_SECTION
u8 OTU_check_uart(u32 UpdateImgCfg){

    if(((UpdateImgCfg>>4)&0x03) == 2){
	    ACTCK_SDIOD_CCTRL(OFF);

	    /* SDIO Function Disable */
	    SDIOD_ON_FCTRL(OFF);
	    SDIOD_OFF_FCTRL(OFF);

	    // SDIO Pin Mux off
	    SDIOD_PIN_FCTRL(OFF);
    }
	
    if (xModem_Init_UART_Port(((UpdateImgCfg>>4)&0x03), (UpdateImgCfg&0x03), 115200) < 0) {
        return 0;
    }


    char ch;
    u8 x_count = 0;
    int timeout1 = 500;
    while (timeout1 != 0) {
       if (xMCtrl.ComPort.poll()) {
           ch = xMCtrl.ComPort.get();
           if(ch != 0x78){
        	   xModem_DeInit_UART_Port(((UpdateImgCfg>>4)&0x03));			   
		   return 0;	
           }
           x_count ++;
           if(x_count == 5){
	        xModem_DeInit_UART_Port(((UpdateImgCfg>>4)&0x03));		   	
               return 1;
           }
       }
       HalDelayUs(200);
       timeout1--;
    }

    if(!x_count){
        xModem_DeInit_UART_Port(((UpdateImgCfg>>4)&0x03));			   
        return 0;	
    }
	
    int timeout2 = 4500;
    while (timeout2 != 0) {
       if (xMCtrl.ComPort.poll()) {
           ch = xMCtrl.ComPort.get();
           if(ch != 0x78){
        	   xModem_DeInit_UART_Port(((UpdateImgCfg>>4)&0x03));			   
		   return 0;	
           	}
           x_count ++;
           if(x_count == 5)
           {
               xModem_DeInit_UART_Port(((UpdateImgCfg>>4)&0x03));	
               return 1;
           }
       }
       HalDelayUs(200);
       timeout2--;
    }	
	
    xModem_DeInit_UART_Port(((UpdateImgCfg>>4)&0x03));			   
    return 0;
}

FWU_TEXT_SECTION
void OTU_Img_Download(u8 uart_idx, u8 pin_mux, u32 baud_rate,
    u32 start_offset, u32 start_addr, u32 max_size)
{
    SPIC_INIT_PARA SpicInitPara;
    u32 wr_len;
    u8 is_flash=0;

    if (xModem_Init_UART_Port(uart_idx, pin_mux, baud_rate) < 0) {
        return;
    }

    DBG_MISC_INFO("Image Download: StartOffset=%d StartAddr=0x%x MaxSize=%d\r\n", start_offset, start_addr, max_size);

    fw_img2_addr = start_addr;
    flash_wr_offset = start_offset;
    fw_img2_size = max_size;

    if ((start_addr & 0xFF000000) == SPI_FLASH_BASE) {
        // it's going to write the Flash memory
        if (((start_addr & 0x03) != 0) || ((start_offset&0x03) != 0)) {
            DiagPrintf("StartAddr(0x%x), StartOffset(0x%x) Must 4-bytes Aligned\r\n", start_addr, start_offset);
            return;
        }
        SPI_FLASH_PIN_FCTRL(ON);
        if (!SpicFlashInitRtl8195A(SpicOneBitMode)){
            DBG_MISC_ERR("OTU_FW_Update: SPI Init Fail!!!!!!\n");
            SPI_FLASH_PIN_FCTRL(OFF);    
            return;
        }
        is_flash = 1;
        SpicWaitWipDoneRefinedRtl8195A(SpicInitPara);
        fw_img2_addr = start_addr & 0x00FFFFFF;
        xModemStart(&xMCtrl, xMFrameBuf, xModem_Frame_FlashWrite);
    } else {
        xModemStart(&xMCtrl, xMFrameBuf, xModem_Frame_MemWrite);        
    }
    wr_len = xModemRxBuffer(&xMCtrl, ((((max_size+flash_wr_offset-1)>>7)+1) << 7));
    xModemEnd(&xMCtrl);

    xModem_DeInit_UART_Port(uart_idx);

    DBG_MISC_INFO("OTU_Img_Download Done, Write Len=%d\n", wr_len);

    if (is_flash) {
        SPI_FLASH_PIN_FCTRL(OFF);    
    }
}

#endif  //#if CONFIG_PERI_UPDATE_IMG

