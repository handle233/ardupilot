
#include "RCOutput.h"
#include "infineon.h"
#include <AP_Math/AP_Math.h>

using namespace Infineon;

RCOUT_DEFINITION;

void RCOutput::init() {

    for(int a=0;a<max_channels;a++) {
        Cy_GPIO_Pin_FastInit(MAKE_PORT(rcout_list[a]->pin), 
        MAKE_NUM(rcout_list[a]->pin), CY_GPIO_DM_STRONG_IN_OFF, 0, (en_hsiom_sel_t)rcout_list[a]->pinhsiom);
        
        cy_stc_tcpwm_pwm_config_t pwmConfig = {
            .pwmMode = CY_TCPWM_PWM_MODE_PWM,
            .clockPrescaler = CY_TCPWM_PWM_PRESCALER_DIVBY_1,
            .pwmAlignment = CY_TCPWM_PWM_LEFT_ALIGN,
            .runMode = CY_TCPWM_PWM_CONTINUOUS,
            .period0 = 2500,
            .compare0 = 500,
            .enableCompareSwap = true,
            .interruptSources = 0,
            .killMode = CY_TCPWM_PWM_STOP_ON_KILL,
            .countInputMode = CY_TCPWM_INPUT_LEVEL,
            .countInput = CY_TCPWM_INPUT_1
        };

        Cy_TCPWM_PWM_Init(TCPWM0, rcout_list[a]->tcpwm_counter, &pwmConfig);
        Cy_SysClk_PeriPclkAssignDivider((en_clk_dst_t)rcout_list[a]->divider, CY_SYSCLK_DIV_8_BIT, 0);

        channel_states[a].period = 2500;
        channel_states[a].counter = 500;
    }

}

void RCOutput::set_freq(uint32_t chmask, uint16_t freq_hz) {
    if(freq_hz > max_freq) {
        freq_hz = max_freq;
    } else if (freq_hz < min_freq) {
        freq_hz = min_freq;
    }

    for(int a=0;a<max_channels;a++) {
        if(chmask & (1 << a)) {
            uint32_t period = pwm_clock / freq_hz;
            Cy_TCPWM_PWM_SetPeriod0(TCPWM0, rcout_list[a]->tcpwm_counter, period);
            channel_states[a].period = period;
        }
    }
}

uint16_t RCOutput::get_freq(uint8_t chan) {
    if(chan >= max_channels) {
        return 0;
    }
    return pwm_clock / channel_states[chan].period;
}

void RCOutput::enable_ch(uint8_t chan)
{
    if(chan >= max_channels) {
        return ;
    }
    Cy_TCPWM_PWM_Enable(TCPWM0, rcout_list[chan]->tcpwm_counter);
    Cy_TCPWM_TriggerStart_Single(TCPWM0, rcout_list[chan]->tcpwm_counter);
}

void RCOutput::disable_ch(uint8_t chan)
{
    if(chan >= max_channels) {
        return ;
    }
    Cy_TCPWM_PWM_Disable(TCPWM0, rcout_list[chan]->tcpwm_counter);
}

void RCOutput::write(uint8_t chan, uint16_t period_us)
{
    if (chan >= max_channels) {
        return;
    }

    if(period_us > channel_states[chan].period) {
        period_us = channel_states[chan].period - 1;
    }

    Cy_TCPWM_PWM_SetCompare0BufVal(TCPWM0, rcout_list[chan]->tcpwm_counter, period_us);
    channel_states[chan].counter = period_us;
    if(flush) {
        Cy_TCPWM_TriggerCaptureOrSwap_Single(TCPWM0, rcout_list[chan]->tcpwm_counter);
    }
}

uint16_t RCOutput::read(uint8_t chan)
{
    if (chan >= max_channels) {
        return 0;
    }
    return channel_states[chan].counter;
}

void RCOutput::read(uint16_t* period_us, uint8_t len)
{
    len = len > max_channels ? max_channels : len;
    for(int a=0;a<len;a++) {
        period_us[a] = channel_states[a].counter;
    }
}

void RCOutput::cork(void){
    flush = false;
}

void RCOutput::push(void){
    for(int a=0;a<max_channels;a++) {
        Cy_TCPWM_TriggerCaptureOrSwap_Single(TCPWM0, rcout_list[a]->tcpwm_counter);
    }
    flush = true;
}

