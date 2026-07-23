#pragma once

#include <AP_HAL/HAL.h>
#include <AP_HAL/SPIDevice.h>

#include "infineon.h"
#include "AP_HAL_Infineon.h"

class Infineon::RCOutput : public AP_HAL::RCOutput {
public:
    void     init() override;
    void     set_freq(uint32_t chmask, uint16_t freq_hz) override;
    uint16_t get_freq(uint8_t ch) override;
    void     enable_ch(uint8_t ch) override;
    void     disable_ch(uint8_t ch) override;
    void     write(uint8_t ch, uint16_t period_us) override;
    uint16_t read(uint8_t ch) override;
    void     read(uint16_t* period_us, uint8_t len) override;
    void     cork(void) override;
    void     push(void) override;
private:
/*UART 驱动方案*/
    constexpr static uint8_t max_channels = 4;
    cy_stc_scb_uart_context_t uart_context;
    constexpr static uint32_t min_period = 1000;
    constexpr static uint32_t theory_max_duty = 10000;
    constexpr static uint32_t theory_max_clock = 1000000;
    struct channel_state {
        uint16_t set_duty;
        uint16_t max_duty;
    }channel_states[max_channels];

/*PWM驱动方案*/
    // constexpr static uint8_t max_channels = RCOUT_NUM;
    // constexpr static uint32_t max_freq = 1000,
    //     min_freq = 20;
    // constexpr static uint32_t pwm_clock = 1000000;
    // struct channel_state {
    //     uint16_t period;
    //     uint16_t counter;
    // }channel_states[max_channels];
    // bool flush = true;
};
