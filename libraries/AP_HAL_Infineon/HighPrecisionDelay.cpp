#include "HighPrecisionDelay.h"
#include "Scheduler.h"

DelayTimer *DelayTimer::_singleton;

namespace
{

bool time_reached(uint64_t now, uint64_t target)
{
    return int64_t(now - target) >= 0;
}

}

DelayTimer::DelayTimer() :
    delays{},
    delay_index(no_delay)
{
    CY_ASSERT(_singleton == nullptr);
    _singleton = this;
}

void DelayTimer::init()
{
    //turn on a TCPWM counter as a timer
    Cy_SysClk_PeriPclkAssignDivider(
        PCLK_TCPWM0_CLOCKS1,
        CY_SYSCLK_DIV_8_BIT,
        0);
    const cy_stc_tcpwm_counter_config_t counter1 = {
        .period = max_timer_period,
        .clockPrescaler = CY_TCPWM_COUNTER_PRESCALER_DIVBY_1,
        .runMode = CY_TCPWM_COUNTER_ONESHOT,
        .countDirection = CY_TCPWM_COUNTER_COUNT_UP,
        .compareOrCapture = CY_TCPWM_COUNTER_MODE_COMPARE,
        .interruptSources = CY_TCPWM_INT_ON_TC,
        .countInputMode = CY_TCPWM_INPUT_LEVEL,
        .countInput = CY_TCPWM_INPUT_1,
        .trigger0Event = CY_TCPWM_CNT_TRIGGER_ON_DISABLED,
        .trigger1Event = CY_TCPWM_CNT_TRIGGER_ON_DISABLED
    };
    Cy_TCPWM_Counter_Init(TCPWM0, 1, &counter1);
    Cy_TCPWM_Counter_Enable(TCPWM0, 1);

    //turn on interrupt
    cy_stc_sysint_t irqCfg2 = {
        .intrSrc = (NVIC_MUX_DELAYTIMER << CY_SYSINT_INTRSRC_MUXIRQ_SHIFT)|tcpwm_0_interrupts_1_IRQn,
        .intrPriority = BSP_PRIORITY_DELAYTIMER
    };

    Cy_SysInt_Init(&irqCfg2, delay_callback);
    NVIC_ClearPendingIRQ(NVIC_MUX_DELAYTIMER);
    NVIC_EnableIRQ(NVIC_MUX_DELAYTIMER);

    Cy_TCPWM_Counter_SetCounter(TCPWM0, 1U, 0U);

}

void DelayTimer::deinit()
{
    NVIC_DisableIRQ(NVIC_MUX_DELAYTIMER);
    Cy_TCPWM_TriggerStopOrKill_Single(TCPWM0, 1);
    delay_index = no_delay;
}

DelayTimer::~DelayTimer()
{
    if (_singleton == this) {
        _singleton = nullptr;
    }
}

void DelayTimer::AcquireADelay(uint32_t us)
{
    if (us == 0) {
        return;
    }

    const TaskHandle_t task = xTaskGetCurrentTaskHandle();
    const uint64_t target = AP_HAL::micros64() + us;

    bool queued = false;

    (void)xTaskNotifyStateClearIndexed(nullptr, notification_index);
    (void)ulTaskNotifyValueClearIndexed(nullptr, notification_index, notification_bit);

    taskENTER_CRITICAL();
    for (uint8_t i = 0; i < delay_count; i++) {
        if (delays[i].task == nullptr) {
            delays[i].task = task;
            delays[i].ring = target;
            queued = true;
            break;
        }
    }
    if (queued) {
        flush_delay();
    }
    taskEXIT_CRITICAL();

    while (true) {
        const uint64_t now = AP_HAL::micros64();
        if (time_reached(now, target)) {
            break;
        }

        const uint64_t remaining_us = target - now;
        TickType_t wait_ticks = pdMS_TO_TICKS((remaining_us + 999U) / 1000U);
        uint32_t notification = 0;

        if (!queued) {
            if (remaining_us > 1000U) {
                vTaskDelay(pdMS_TO_TICKS(remaining_us / 1000U));
            } else {
                __NOP();
            }
            continue;
        }

        if (wait_ticks == 0) {
            __NOP();
            continue;
        }

        (void)xTaskNotifyWaitIndexed(notification_index,
                                     notification_bit,
                                     notification_bit,
                                     &notification,
                                     wait_ticks);
    }

    if (queued) {
        taskENTER_CRITICAL();
        remove_delay(task, target);
        flush_delay();
        taskEXIT_CRITICAL();
    }
}

void DelayTimer::delay_callback()
{
    if (_singleton != nullptr) {
        _singleton->handle_interrupt();
    }
}

void DelayTimer::handle_interrupt()
{
    Cy_TCPWM_ClearInterrupt(TCPWM0, 1, CY_TCPWM_INT_ON_TC);

    BaseType_t higher_priority_task_woken = pdFALSE;
    const UBaseType_t saved_mask = taskENTER_CRITICAL_FROM_ISR();
    const uint64_t now = AP_HAL::micros64();

    for (uint8_t i = 0; i < delay_count; i++) {
        if (delays[i].task != nullptr && time_reached(now, delays[i].ring)) {
            TaskHandle_t task = delays[i].task;
            delays[i].task = nullptr;
            delays[i].ring = 0;
            (void)xTaskNotifyIndexedFromISR(task,
                                            notification_index,
                                            notification_bit,
                                            eSetBits,
                                            &higher_priority_task_woken);
        }
    }

    delay_index = no_delay;
    flush_delay();
    taskEXIT_CRITICAL_FROM_ISR(saved_mask);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

void DelayTimer::flush_delay()
{
    int8_t minimum_index = no_delay;

    for (uint8_t i = 0; i < delay_count; i++) {
        if (delays[i].task == nullptr) {
            continue;
        }
        if (minimum_index == no_delay ||
            int64_t(delays[i].ring - delays[minimum_index].ring) < 0) {
            minimum_index = i;
        }
    }

    if (minimum_index == no_delay) {
        Cy_TCPWM_TriggerStopOrKill_Single(TCPWM0, 1);
        delay_index = no_delay;
        return;
    }

    if (delay_index == minimum_index) {
        return;
    }

    Cy_TCPWM_TriggerStopOrKill_Single(TCPWM0, 1);

    int64_t period = delays[minimum_index].ring - AP_HAL::micros64();
    if (period <= 0) {
        period = 1;
    }

    const uint32_t timer_period = (period > max_timer_period) ? max_timer_period : uint32_t(period);

    delay_index = minimum_index;
    Cy_TCPWM_Counter_SetPeriod(TCPWM0, 1, timer_period);
    Cy_TCPWM_Counter_SetCounter(TCPWM0, 1U, 0U);
    Cy_TCPWM_TriggerStart_Single(TCPWM0, 1);
}

void DelayTimer::remove_delay(TaskHandle_t task, uint64_t ring)
{
    for (uint8_t i = 0; i < delay_count; i++) {
        if (delays[i].task == task && delays[i].ring == ring) {
            delays[i].task = nullptr;
            delays[i].ring = 0;
            if (delay_index == i) {
                delay_index = no_delay;
            }
            return;
        }
    }
}
