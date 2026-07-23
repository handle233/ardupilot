#pragma once

#include <AP_HAL/AP_HAL.h>
#include "AP_HAL_Infineon_Namespace.h"
#include <AP_Common/ExpandingString.h>

class Infineon::Util : public AP_HAL::Util {
public:
    void set_hw_rtc(uint64_t time_utc_usec)override {
        rtc_time_base = time_utc_usec;
    }
    
    uint64_t get_hw_rtc() const override {
        if(rtc_time_base){
            return rtc_time_base + AP_HAL::micros64();
        }
        return 0;
    }

    uint32_t available_memory(void)override;
    void *malloc_type(size_t size, Memory_Type mem_type)override;
    void free_type(void *ptr, size_t size, Memory_Type mem_type)override;

    safety_state safety_switch_state()override;
    bool was_watchdog_reset() const override;
    bool get_system_id(char buf[50]) override;
    bool trap() const override;

#if HAL_UART_STATS_ENABLED
    void uart_info(ExpandingString &str) override;
#endif

private:
    uint64_t rtc_time_base = 0;

    struct {
        uint32_t last_ms;
        AP_HAL::UARTDriver::StatsTracker serial[HAL_UART_NUM_SERIAL_PORTS];
    } sys_uart_stats;

};
