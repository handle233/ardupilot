#include <AP_HAL/AP_HAL.h>
#include <stdarg.h>
#include "infineon.h"
#include "AP_Math/div1000.h"

extern const AP_HAL::HAL& hal;

namespace AP_HAL{


    void init()
    {
        //no bsp functions should be called here.
    }
        

    void panic(const char *errormsg, ...){
        for(;;){
            va_list ap;
            va_start(ap, errormsg);
            hal.console->printf(errormsg,ap);
            va_end(ap);
            hal.console->printf("\n");
            hal.scheduler->delay(200);
        }
    }
    
    __FASTRAMFUNC__ uint64_t micros64()
    {
        uint32_t primask = __get_PRIMASK();
        __disable_irq();

        static uint64_t sumup = 0;

        uint32_t dt = DWT->CYCCNT - (uint32_t)sumup;

        sumup += dt;

        __set_PRIMASK(primask);
        return sumup/400;
    }

    __FASTRAMFUNC__ uint64_t millis64()
    {
        return uint64_div1000(micros64());
    }

    __FASTRAMFUNC__ uint32_t micros()
    {
        return micros64() & 0xFFFFFFFF;
    }

    __FASTRAMFUNC__ uint32_t millis()
    {
        return millis64() & 0xFFFFFFFF;
    }

}//namespace AP_HAL

extern "C" void runtime_stats_timer_init(void)
{
    // 如果你的 micros64/TCPWM 已经在系统初始化时启动了，这里可以为空
}

extern "C" uint32_t runtime_stats_get_counter(void)
{
    return (uint32_t)AP_HAL::micros();
}