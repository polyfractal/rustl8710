;---------------------------------
;
; Functions accessed by the debugger to perform a flash download.
; All public symbols and the function FlashBreak() are looked up and called by the debugger.
;
; Copyright (c) 2008 IAR Systems
;
; $Revision: 38034 $
;
;---------------------------------

#define _CORTEX_  ((__CORE__ == __ARM6M__) || (__CORE__ == __ARM6SM__) || (__CORE__ == __ARM7M__) || (__CORE__ == __ARM7EM__))

        PUBLIC FlashInitEntry
        PUBLIC FlashWriteEntry
        PUBLIC FlashEraseWriteEntry
        PUBLIC FlashChecksumEntry
        PUBLIC FlashSignoffEntry
        PUBLIC FlashBufferStart
        PUBLIC FlashBufferEnd

        EXTERN FlashBreak
        EXTERN Fl2FlashInitEntry
        EXTERN Fl2FlashWriteEntry
        EXTERN Fl2FlashEraseWriteEntry
        EXTERN Fl2FlashChecksumEntry
        EXTERN Fl2FlashSignoffEntry

        SECTION CSTACK:DATA:NOROOT(3)


;---------------------------------
;
; FlashInitEntry()
; Debugger interface function
;
;---------------------------------
        SECTION .text:CODE:ROOT(2)
#if !_CORTEX_
        ARM
#else
        THUMB
#endif

FlashInitEntry:
#if !_CORTEX_
        ;; Set up the normal stack pointer.
        LDR     sp, =SFE(CSTACK)        ; End of CSTACK
#endif
        BL       Fl2FlashInitEntry
        B        FlashBreak


;---------------------------------
;
; FlashWriteEntry()
; Debugger interface function
;
;---------------------------------
        SECTION .text:CODE:ROOT(2)
#if !_CORTEX_
        ARM
#else
        THUMB
#endif

FlashWriteEntry:
        BL       Fl2FlashWriteEntry
        B        FlashBreak


;---------------------------------
;
; FlashEraseWriteEntry
; Debugger interface function
;
;---------------------------------
        SECTION .text:CODE:ROOT(2)
#if !_CORTEX_
        ARM
#else
        THUMB
#endif

FlashEraseWriteEntry:
        BL       Fl2FlashEraseWriteEntry
        B        FlashBreak


;---------------------------------
;
; FlashChecksumEntry
; Debugger interface function
;
;---------------------------------
        SECTION .text:CODE:NOROOT(2)
#if !_CORTEX_
        ARM
#else
        THUMB
#endif

FlashChecksumEntry:
        BL       Fl2FlashChecksumEntry
        B        FlashBreak


;---------------------------------
;
; FlashSignoffEntry
; Debugger interface function
;
;---------------------------------
        SECTION .text:CODE:NOROOT(2)
#if !_CORTEX_
        ARM
#else
        THUMB
#endif

FlashSignoffEntry:
        BL       Fl2FlashSignoffEntry
        B        FlashBreak


;---------------------------------
;
; Flash buffer and Cortex stack
;
;---------------------------------
        SECTION LOWEND:DATA(8)
        DATA
FlashBufferStart:

        SECTION HIGHSTART:DATA
        DATA
FlashBufferEnd:



#if _CORTEX_
        PUBLIC __vector_table

        SECTION .intvec:CODE:ROOT(2)
        DATA

__vector_table:
#if 0
        DC32    SFE(CSTACK)
        DC32    FlashInitEntry
#endif
#endif

;---------------------------------
; Entry: 0x200006b4 
;        ram start up, normal boot
;      : 0x200006c4 
;        ram wake up, use debugger, 
;        0x40000218 BIT(31) must 1
; Section: .start.ram.data, 
;           put to 0x200006b4
;        : .patch.start.ram.data, 
;           put to 0x200006bc
;---------------------------------

#if _CORTEX_
        PUBLIC __ram_start_table

        SECTION .start:CODE:ROOT(2)
        DATA

__ram_start_table:
        DC32    FlashInitEntry
        DC32    FlashInitEntry
		DC32    FlashInitEntry
        DC32    FlashInitEntry
		
#endif

#if _CORTEX_
        PUBLIC __patch_ram_start_table

        SECTION .patch:CODE:ROOT(2)
        DATA

__patch_ram_start_table:
        DC32    FlashInitEntry
        DC32    FlashInitEntry
		DC32    FlashInitEntry
        DC32    FlashInitEntry

#endif
        END
        
        
        
        
