#pragma once

#include "infineon.h"

class DelayTimer
{
public:
    DelayTimer();
    void init();
    void deinit();
    ~DelayTimer();

    void AcquireADelay(uint32_t us);
private:
    static void delay_callback();
    void handle_interrupt();
    void flush_delay();
    void remove_delay(TaskHandle_t task, uint64_t ring);

    constexpr static uint8_t delay_count = 8;
    constexpr static int8_t no_delay = -1;
    constexpr static uint32_t max_timer_period = 32768;
    constexpr static UBaseType_t notification_index = 2;
    constexpr static uint32_t notification_bit = 1UL << 31;
    static_assert(configTASK_NOTIFICATION_ARRAY_ENTRIES > notification_index,
                  "HighPrecisionDelay requires a dedicated task notification");

    struct delay_t {
        uint64_t ring;
        TaskHandle_t task;
    } delays[delay_count];

    static DelayTimer *_singleton;
    int8_t delay_index;
};
