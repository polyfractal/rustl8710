#include "FreeRTOS.h"

#include "freertos_pmu.h"

#include <platform_opts.h>

#include "platform_autoconf.h"
#include "sys_api.h"
#include "sleep_ex_api.h"
#include "gpio_api.h"

#ifndef portNVIC_SYSTICK_CURRENT_VALUE_REG
#define portNVIC_SYSTICK_CURRENT_VALUE_REG	( * ( ( volatile uint32_t * ) 0xe000e018 ) )
#endif

uint32_t missing_tick = 0;

#define FREERTOS_PMU_DISABLE_LOGUART_IN_TICKLESS (0)

static uint32_t wakelock     = DEFAULT_WAKELOCK;
static uint32_t wakeup_event = DEFAULT_WAKEUP_EVENT;

freertos_sleep_callback pre_sleep_callback[32] = {NULL};
freertos_sleep_callback post_sleep_callback[32] = {NULL};

#if (configGENERATE_RUN_TIME_STATS == 1)
static u8 last_wakelock_state[32] = {
    DEFAULT_WAKELOCK & 0x01, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};
static u32 last_acquire_wakelock_time[32] = {0};
static u32 hold_wakelock_time[32] = {0};
static u32 base_sys_time = 0;
static u32 sys_sleep_time = 0;
#endif

#if defined(FREERTOS_PMU_TICKLESS_PLL_RESERVED) && (FREERTOS_PMU_TICKLESS_PLL_RESERVED==1)
unsigned char reserve_pll = 1;
#else
unsigned char reserve_pll = 0;
#endif


/* ++++++++ FreeRTOS macro implementation ++++++++ */

/*
 *  It is called in idle task.
 *
 *  @return  true  : System is ready to check conditions that if it can enter sleep.
 *           false : System keep awake.
 **/
int freertos_ready_to_sleep() {
    return wakelock == 0;
}

/*
 *  It is called when freertos is going to sleep.
 *  At this moment, all sleep conditons are satisfied. All freertos' sleep pre-processing are done.
 *
 *  @param  expected_idle_time : The time that FreeRTOS expect to sleep.
 *                               If we set this value to 0 then FreeRTOS will do nothing in its sleep function.
 **/
void freertos_pre_sleep_processing(unsigned int *expected_idle_time) {

#ifdef CONFIG_SOC_PS_MODULE

    uint32_t i;
    uint32_t stime;
    uint32_t tick_before_sleep;
    uint32_t tick_after_sleep;
    uint32_t tick_passed;
    uint32_t backup_systick_reg;
    unsigned char IsDramOn = 1;
    unsigned char suspend_sdram = 1;

#if (configGENERATE_RUN_TIME_STATS == 1)
	uint32_t kernel_tick_before_sleep;
	uint32_t kernel_tick_after_sleep;
#endif

    /* To disable freertos sleep function and use our sleep function, 
     * we can set original expected idle time to 0. */
    stime = *expected_idle_time;
    *expected_idle_time = 0;

    for (i=0; i<32; i++) {
        if ( pre_sleep_callback[i] != NULL) {
            pre_sleep_callback[i]( stime );
        }
    }

#if (configGENERATE_RUN_TIME_STATS == 1)
	kernel_tick_before_sleep = osKernelSysTick();
#endif

    // Store gtimer timestamp before sleep
    tick_before_sleep = us_ticker_read();

    if ( sys_is_sdram_power_on() == 0 ) {
        IsDramOn = 0;
    }

    if (IsDramOn) {
#if defined(FREERTOS_PMU_TICKLESS_SUSPEND_SDRAM) && (FREERTOS_PMU_TICKLESS_SUSPEND_SDRAM==0)
        // sdram is turned on, and we don't want suspend sdram
        suspend_sdram = 0;
#endif
    } else {
        // sdram didn't turned on, we should not suspend it
        suspend_sdram = 0;
    }

#if (FREERTOS_PMU_DISABLE_LOGUART_IN_TICKLESS)
    // config gpio on log uart tx for pull ctrl
    HAL_GPIO_PIN gpio_log_uart_tx;
    gpio_log_uart_tx.pin_name = gpio_set(PB_0);
    gpio_log_uart_tx.pin_mode = DOUT_PUSH_PULL;
    HAL_GPIO_Init(&gpio_log_uart_tx);
    GpioFunctionChk(PB_0, ENABLE);

    sys_log_uart_off();
    HAL_GPIO_WritePin(&gpio_log_uart_tx, 1); // pull up log uart tx to avoid power lekage
#endif

    backup_systick_reg = portNVIC_SYSTICK_CURRENT_VALUE_REG;

    // sleep
    sleep_ex_selective(wakeup_event, stime, reserve_pll, suspend_sdram);

    portNVIC_SYSTICK_CURRENT_VALUE_REG = backup_systick_reg;

#if (FREERTOS_PMU_DISABLE_LOGUART_IN_TICKLESS)
    sys_log_uart_off();
    sys_log_uart_on();
#endif

    // update kernel tick by calculating passed tick from gtimer
    {
        // get current gtimer timestamp
        tick_after_sleep = us_ticker_read();

        // calculated passed time
        if (tick_after_sleep > tick_before_sleep) {
            tick_passed = tick_after_sleep - tick_before_sleep;
        } else {
            // overflow
            tick_passed = (0xffffffff - tick_before_sleep) + tick_after_sleep;
        }

        /* If there is a rapid interrupt (<1ms), it makes tick_passed less than 1ms.
         * The tick_passed would be rounded and make OS can't step tick.
         * We collect the rounded tick_passed into missing_tick and step tick properly.
         * */
        tick_passed += missing_tick;
        if (tick_passed > stime * 1000) {
            missing_tick = tick_passed - stime * 1000;
            tick_passed = stime * 1000;
        } else {
            missing_tick = tick_passed % 1000;
        }

        // update kernel tick
        vTaskStepTick( tick_passed/1000 );
    }

#if (configGENERATE_RUN_TIME_STATS == 1)
	kernel_tick_after_sleep = osKernelSysTick();
	sys_sleep_time += (kernel_tick_after_sleep - kernel_tick_before_sleep);
#endif

    for (i=0; i<32; i++) {
        if ( post_sleep_callback[i] != NULL) {
            post_sleep_callback[i]( stime );
        }
    }

#else
    // If PS is not enabled, then use freertos sleep function
#endif
}

void freertos_post_sleep_processing(unsigned int *expected_idle_time) {
#ifndef configSYSTICK_CLOCK_HZ
	*expected_idle_time = 1 + ( portNVIC_SYSTICK_CURRENT_VALUE_REG / ( configCPU_CLOCK_HZ / configTICK_RATE_HZ ) );
#else
	*expected_idle_time = 1 + ( portNVIC_SYSTICK_CURRENT_VALUE_REG / ( configSYSTICK_CLOCK_HZ / configTICK_RATE_HZ ) );
#endif
}
/* -------- FreeRTOS macro implementation -------- */

void acquire_wakelock(uint32_t lock_id) {

    wakelock |= lock_id;

#if (configGENERATE_RUN_TIME_STATS == 1)
    u32 i;
    u32 current_timestamp = osKernelSysTick();
    for (i=0; i<32; i++) {
        if ( (1<<i & lock_id) && (last_wakelock_state[i] == 0) ) {
            last_acquire_wakelock_time[i] = current_timestamp;
            last_wakelock_state[i] = 1;            
        }
    }
#endif

}

void release_wakelock(uint32_t lock_id) {
    wakelock &= ~lock_id;

#if (configGENERATE_RUN_TIME_STATS == 1)
    u32 i;
    u32 current_timestamp = osKernelSysTick();
    for (i=0; i<32; i++) {
        if ( (1<<i & lock_id) && (last_wakelock_state[i] == 1) ) {
            hold_wakelock_time[i] += current_timestamp - last_acquire_wakelock_time[i];
            last_wakelock_state[i] = 0;
        }
    }
#endif

}

uint32_t get_wakelock_status() {
    return wakelock;
}

#if (configGENERATE_RUN_TIME_STATS == 1)
void get_wakelock_hold_stats( char *pcWriteBuffer ) {
    u32 i;
    u32 current_timestamp = osKernelSysTick();

    *pcWriteBuffer = 0x00;

    // print header
    sprintf(pcWriteBuffer, "wakelock_id\tholdtime\r\n");
    pcWriteBuffer += strlen( pcWriteBuffer );

    for (i=0; i<32; i++) {
        if (last_wakelock_state[i] == 1) {
            sprintf(pcWriteBuffer, "%x\t\t%d\r\n", i, hold_wakelock_time[i] + (current_timestamp - last_acquire_wakelock_time[i]));
        } else {
            if (hold_wakelock_time[i] > 0) {
                sprintf(pcWriteBuffer, "%x\t\t%d\r\n", i, hold_wakelock_time[i]);
            }
        }
        pcWriteBuffer += strlen( pcWriteBuffer );
    }
    sprintf(pcWriteBuffer, "time passed: %d ms, system sleep %d ms\r\n", current_timestamp - base_sys_time, sys_sleep_time);
}

void clean_wakelock_stat() {
    u32 i;
    base_sys_time = osKernelSysTick();
    for (i=0; i<32; i++) {
        hold_wakelock_time[i] = 0;
        if (last_wakelock_state[i] == 1) {
            last_acquire_wakelock_time[i] = base_sys_time;
        }
    }
	sys_sleep_time = 0;
}
#endif

void add_wakeup_event(uint32_t event) {
    wakeup_event |= event;
}

void del_wakeup_event(uint32_t event) {
    wakeup_event &= ~event;
    // To fulfill tickless design, system timer is required to be wakeup event
    wakeup_event |= SLEEP_WAKEUP_BY_STIMER;
}

void register_sleep_callback_by_module( unsigned char is_pre_sleep, freertos_sleep_callback sleep_cb, uint32_t module ) {
    u32 i;
    for (i=0; i<32; i++) {
        if ( module & BIT(i) ) {
            if (is_pre_sleep) {
                pre_sleep_callback[i] = sleep_cb;
            } else {
                post_sleep_callback[i] = sleep_cb;
            }
        }
    }
}

void register_pre_sleep_callback( freertos_sleep_callback pre_sleep_cb ) {
    register_sleep_callback_by_module(1, pre_sleep_cb, 0x00008000);
}

void register_post_sleep_callback( freertos_sleep_callback post_sleep_cb ) {
    register_sleep_callback_by_module(0, post_sleep_cb, 0x00008000);
}

void set_pll_reserved(unsigned char reserve) {
    reserve_pll = reserve;
}