#pragma once

#include "AP_HAL_Infineon.h"
#include "Scheduler.h"

class Infineon::AnalogSource : public AP_HAL::AnalogSource {
public:
    AnalogSource(int _channel);
    float read_average() override;
    float read_latest() override;
    bool set_pin(uint8_t p) override;
    float voltage_average() override;
    float voltage_latest() override;
    float voltage_average_ratiometric() override { return voltage_average(); }
private:
    static constexpr float base_volt = 3.29f;
    int channel;
};

class Infineon::AnalogIn : public AP_HAL::AnalogIn {
public:
    friend AnalogSource;
    AnalogIn();
    void init() override;
    AP_HAL::AnalogSource* channel(int16_t n) override;
    float board_voltage(void) override;

    static void periodic_process(void*);

    struct PinMap{
        int id;
        int channel;
        uint64_t average;
        uint32_t average_count;
        uint16_t recent;
    };
private:
    static PinMap pins[ANALOG_PIN_NUM];

    cy_stc_sar2_channel_config_t channels[ANALOG_PIN_NUM];
    cy_stc_sar2_config_t sar_0_config;
    static int bat_channel;

    StaticTask_t _thread;
    TaskHandle_t task_handle;
    static StackType_t Stack[INFENION_ANALOG_PROCESS_STACK_SIZE];
};
