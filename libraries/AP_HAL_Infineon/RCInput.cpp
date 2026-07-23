
#include "RCInput.h"

using namespace Infineon;
extern const AP_HAL::HAL& hal;

RCInput::RCInput()
{
    memset(_rc_values, 0, sizeof(_rc_values));
}

void Infineon::RCInput::timer_tick()
{
    if(!_init) {
        return;
    }

    AP_RCProtocol &rc = AP::RC();

    if(rc.new_input()) {
        WITH_SEMAPHORE(_lock);
        _last_timestamp = AP_HAL::micros();
        _num_channels = rc.num_channels();

        rc.read(_rc_values, _num_channels);
    }
}

void RCInput::init()
{
    AP::RC().init();

    AP::RC().set_rc_protocols(1U << ((uint8_t)AP_RCProtocol::SBUS+1));

    _init = true;

    hal.scheduler->register_timer_process(FUNCTOR_BIND_MEMBER(&RCInput::timer_tick,void));
}

bool RCInput::new_input() {
    if(!_init) {
        return false;
    }
    bool valid = false;

    {
        WITH_SEMAPHORE(_lock);
        valid = (_last_timestamp != _last_read);
        _last_read = _last_timestamp;
    }

    return valid;
}

uint8_t RCInput::num_channels() {
    if(!_init) {
        return 0;
    }
    return _num_channels;
}

uint16_t RCInput::read(uint8_t chan) {
    if(!_init || chan >= _num_channels) {
        return 0;
    }
    uint16_t value = 0;
    {
        WITH_SEMAPHORE(_lock);
        value = _rc_values[chan];
    }
    return value;
}

uint8_t RCInput::read(uint16_t* periods, uint8_t len) {
    if(!_init || periods == nullptr ){
        return 0;
    }

    int i = 0;
    {
        WITH_SEMAPHORE(_lock);

        for(i = 0; i < len && i < _num_channels; i++) {
            periods[i] = _rc_values[i];
        }
    }
    return i;
}

