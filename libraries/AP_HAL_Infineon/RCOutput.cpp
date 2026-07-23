
#include "RCOutput.h"
#include "infineon.h"
#include <AP_Math/AP_Math.h>
#include <stdio.h>

using namespace Infineon;

RCOUT_DEFINITION;

void RCOutput::init() {
    constexpr uint32_t rxpin = MAKE_PIN(2,0);
    constexpr uint32_t txpin = MAKE_PIN(2,1);
    constexpr uint32_t b = 460800;
     Cy_GPIO_Pin_FastInit(MAKE_PORT(rxpin),
                        MAKE_NUM(rxpin),
                        CY_GPIO_DM_HIGHZ,
                        1u,
                        P2_0_SCB7_UART_RX);

    Cy_GPIO_Pin_FastInit(MAKE_PORT(txpin),
                        MAKE_NUM(txpin),
                        CY_GPIO_DM_STRONG_IN_OFF,
                        1u,
                        P2_1_SCB7_UART_TX);

    const cy_stc_scb_uart_config_t uart_config =
    {
        .uartMode                   = CY_SCB_UART_STANDARD,
        .oversample                 = 8UL,
        .dataWidth                  = 8UL,
        .enableMsbFirst             = false,
        .stopBits                   = CY_SCB_UART_STOP_BITS_1,
        .parity                     = CY_SCB_UART_PARITY_NONE,
        .enableInputFilter          = false,
        .dropOnParityError          = true,
        .dropOnFrameError           = true,
        .enableMutliProcessorMode   = false,
        .receiverAddress            = 0UL,
        .receiverAddressMask        = 0UL,
        .acceptAddrInFifo           = false,
        .irdaInvertRx               = false,
        .irdaEnableLowPowerReceiver = false,
        .smartCardRetryOnNack       = false,
        .enableCts                  = false,
        .ctsPolarity                = CY_SCB_UART_ACTIVE_LOW,
        .rtsRxFifoLevel             = 0UL,
        .rtsPolarity                = CY_SCB_UART_ACTIVE_LOW,
        .breakWidth                 = 11UL,
        .rxFifoTriggerLevel         = 0UL,
        .rxFifoIntEnableMask        = 0UL,
        .txFifoTriggerLevel         = 7UL,
        .txFifoIntEnableMask        = 0UL
    };
    
    //初始化uart
    cy_en_scb_uart_status_t status;
    status = Cy_SCB_UART_Init(SCB7, &uart_config, &uart_context);
    CY_ASSERT(status == CY_SCB_UART_SUCCESS);

    //初始化时钟
    uint32_t dummy = PERIPHERAL_CLOCK/b/8;
    infineon_init_clock(
        PCLK_SCB7_CLOCK,
        CY_SYSCLK_DIV_8_BIT,
        10,
    dummy);

    //启动
    Cy_SCB_UART_Enable(SCB7);

}

void RCOutput::set_freq(uint32_t chmask, uint16_t freq_hz) {
    // if(freq_hz > max_freq) {
    //     freq_hz = max_freq;
    // } else if (freq_hz < min_freq) {
    //     freq_hz = min_freq;
    // }

    // for(int a=0;a<max_channels;a++) {
    //     if(chmask & (1 << a)) {
    //         uint32_t period = pwm_clock / freq_hz;
    //         Cy_TCPWM_PWM_SetPeriod0(TCPWM0, rcout_list[a]->tcpwm_counter, period);
    //         channel_states[a].period = period;
    //     }
    // }

    for(int a=0;a<max_channels;a++) {
        if(chmask & (1 << a)) {
            uint32_t period = theory_max_clock / freq_hz;
            channel_states[a].max_duty = period;
        }
    }
}

uint16_t RCOutput::get_freq(uint8_t chan) {
    // if(chan >= max_channels) {
    //     return 0;
    // }
    // return pwm_clock / channel_states[chan].period;
    
    if(chan >= max_channels) {
        return 0;
    }
    return theory_max_clock / channel_states[chan].max_duty;
}

void RCOutput::enable_ch(uint8_t chan)
{
    if(chan >= max_channels) {
        return ;
    }
    // Cy_TCPWM_PWM_Enable(TCPWM0, rcout_list[chan]->tcpwm_counter);
    // Cy_TCPWM_TriggerStart_Single(TCPWM0, rcout_list[chan]->tcpwm_counter);
}

void RCOutput::disable_ch(uint8_t chan)
{
    if(chan >= max_channels) {
        return ;
    }
    // Cy_TCPWM_PWM_Disable(TCPWM0, rcout_list[chan]->tcpwm_counter);
}

void RCOutput::write(uint8_t chan, uint16_t period_us)
{
    if (chan >= max_channels) {
        return;
    }

    // if(period_us > channel_states[chan].period) {
    //     period_us = channel_states[chan].period - 1;
    // }

    // Cy_TCPWM_PWM_SetCompare0BufVal(TCPWM0, rcout_list[chan]->tcpwm_counter, period_us);
    // channel_states[chan].counter = period_us;
    // if(flush) {
    //     Cy_TCPWM_TriggerCaptureOrSwap_Single(TCPWM0, rcout_list[chan]->tcpwm_counter);
    // }


    uint32_t newduty = (period_us - min_period) * theory_max_duty / (channel_states[chan].max_duty - min_period);

    channel_states[chan].set_duty = newduty;

    char buffer[64] = "";

    static uint64_t last_time = 0;

    if(AP_HAL::micros64() - last_time < 2500) {
        return;
    }

    last_time = AP_HAL::micros64();

    uint16_t len = sprintf(buffer, "SET-DUTY,%d,%d,%d,%d\n",
         channel_states[0].set_duty,channel_states[1].set_duty,
        channel_states[2].set_duty,channel_states[3].set_duty);

    Cy_SCB_UART_PutArray(SCB7, (uint8_t*)buffer, len);
}

uint16_t RCOutput::read(uint8_t chan)
{
    // if (chan >= max_channels) {
    //     return 0;
    // }
    // return channel_states[chan].counter;

    if (chan >= max_channels) {
        return 0;
    }
    return channel_states[chan].set_duty*channel_states[chan].max_duty
        /theory_max_duty;
}

void RCOutput::read(uint16_t* period_us, uint8_t len)
{
    // len = len > max_channels ? max_channels : len;
    // for(int a=0;a<len;a++) {
    //     period_us[a] = channel_states[a].counter;
    // }

    len = len > max_channels ? max_channels : len;
    for(int a=0;a<len;a++) {
        period_us[a] = channel_states[a].set_duty*channel_states[a].max_duty
            /theory_max_duty;
    }
}

void RCOutput::cork(void){
    // flush = false;
}

void RCOutput::push(void){
    // for(int a=0;a<max_channels;a++) {
    //     Cy_TCPWM_TriggerCaptureOrSwap_Single(TCPWM0, rcout_list[a]->tcpwm_counter);
    // }
    // flush = true;
}

