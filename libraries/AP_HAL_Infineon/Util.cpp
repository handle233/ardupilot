#include "Util.h"

using namespace Infineon;
extern const AP_HAL::HAL& hal;


uint32_t Util::available_memory(void){
    return 16384; //TODO: implement this function
}
void *Util::malloc_type(size_t size, Memory_Type mem_type){ return calloc(1, size); }
void Util::free_type(void *ptr, size_t size, Memory_Type mem_type){ return free(ptr); }

Util::safety_state Util::safety_switch_state(){
    return SAFETY_NONE;
}
bool Util::was_watchdog_reset() const{
    return false;
}
bool Util::get_system_id(char buf[50]){
    snprintf(buf, 50, "Infineon-CYT4BB7");
    buf[49] = 0;
    return true;
}
bool Util::trap() const {
    return false;
}

void Util::uart_info(ExpandingString &str){
    const uint32_t now_ms = AP_HAL::millis();
    const uint32_t dt_ms = now_ms - sys_uart_stats.last_ms;
    sys_uart_stats.last_ms = now_ms;

    // Mission Planner / MAVFTP 
    // remains to be done, but this is a good start
    str.printf("UARTV1\n");

    for (uint8_t i = 0; i < HAL_UART_NUM_SERIAL_PORTS; i++) {
        AP_HAL::UARTDriver *uart = hal.serial(i);
        if (uart == nullptr) {
            continue;
        }

        str.printf("SERIAL%u ", unsigned(i));
        uart->uart_info(str, sys_uart_stats.serial[i], dt_ms);
    }
}