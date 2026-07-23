#pragma once

#include "AP_HAL_Infineon.h"
#include <AP_RCProtocol/AP_RCProtocol.h>

#ifndef RC_INPUT_MAX_CHANNELS
#define RC_INPUT_MAX_CHANNELS 16
#endif

class Infineon::RCInput : public AP_HAL::RCInput {
public:
    RCInput();

    void init() override;
    bool  new_input() override;
    uint8_t num_channels() override;
    uint16_t read(uint8_t ch) override;
    uint8_t read(uint16_t* periods, uint8_t len) override;

    virtual const char *protocol() const override { return "SBUS"; }

    void timer_tick();
private:
    bool _init = false;

    uint16_t _rc_values[RC_INPUT_MAX_CHANNELS]{};
    uint8_t _num_channels = 0;

    uint32_t _last_timestamp = 0;
    uint32_t _last_read = 0;

    Infineon::Semaphore _lock;
};
