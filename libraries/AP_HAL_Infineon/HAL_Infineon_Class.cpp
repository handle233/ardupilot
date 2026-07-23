
#include <AP_HAL/AP_HAL.h>
#if CONFIG_HAL_BOARD == HAL_BOARD_INFINEON

#include <assert.h>

#include "infineon.h"

#include "HAL_Infineon_Class.h"
#include "AP_HAL_Infineon_Private.h"

using namespace Infineon;


static GPIO gpioDriver;
static I2CDeviceManager i2cDeviceManager;
static SPIDeviceManager spiDeviceManager;
static Scheduler scheduler_;
static Storage flashStorage;
static Util utility;
static AnalogIn analoginput;
static RCInput rcinput_;
static RCOutput rcoutput_;

namespace {

    constexpr uint16_t HAL_INFENION_MAIN_TASK_STACK_SIZE = configMINIMAL_STACK_SIZE * 8;
    StaticTask_t hal_infineon_main_task_buffer;
    StackType_t hal_infineon_main_task_stack[HAL_INFENION_MAIN_TASK_STACK_SIZE];

    void hal_infineon_main_task(void *arg)
    {
        auto *callbacks = static_cast<AP_HAL::HAL::Callbacks *>(arg);

        callbacks->setup();

        scheduler_.set_system_initialized();
        //gpioDriver.toggle(MAKE_PIN(19,0));

        for (;;) {
            callbacks->loop();
        }
    }

}

SCB0_DEFINITION;
SCB1_DEFINITION;
SCB2_DEFINITION;
SCB3_DEFINITION;
SCB4_DEFINITION;
SCB5_DEFINITION;
SCB6_DEFINITION;
SCB7_DEFINITION;
SCB8_DEFINITION;
SCB9_DEFINITION;

HAL_Infineon::HAL_Infineon() :
    AP_HAL::HAL(
        //serial
        HAL_SERIAL_0,            /* no SERIAL0 */
        HAL_SERIAL_1,            /* no SERIAL1 */
        HAL_SERIAL_2,            /* no SERIAL2 */
        HAL_SERIAL_3,            /* no SERIAL3 */
        HAL_SERIAL_4,            /* no SERIAL4 */
        HAL_SERIAL_5,            /* no SERIAL5 */
        HAL_SERIAL_6,            /* no SERIAL6 */
        HAL_SERIAL_7,            /* no SERIAL7 */
        HAL_SERIAL_8,            /* no SERIAL8 */
        HAL_SERIAL_9,            /* no SERIAL9 */

        &i2cDeviceManager,  //iicmanager
        &spiDeviceManager,  //spimanager
        nullptr,            //wspi
        &analoginput,            //analog
        &flashStorage,            //storage
        HAL_SERIAL_USB,            //serial USB
        &gpioDriver,        //gpiodriver
        &rcinput_,            //rcin
        &rcoutput_,            //rcout
        &scheduler_,         //scheduler
        &utility,            //utility
        nullptr,            //optical
        nullptr,            //flash
        nullptr,            //dsp
        nullptr             //canI face
    )
{}

void HAL_Infineon::run(int argc, char* const argv[], Callbacks* callbacks) const
{
    //here initialize the bsp.
    cy_rslt_t result;
    
    result = cybsp_init();
    CY_ASSERT(result == CY_RSLT_SUCCESS);
    //gpioDriver.pinMode(MAKE_PIN(19,0),HAL_GPIO_OUTPUT);

    __enable_irq();

    /*
    * initial all hal component，call callbacks->setup()，then start scheduler
    */
    console->begin(DEFAULT_SERIAL0_BAUD);

    i2cDeviceManager.initialize();
    spiDeviceManager.initialize();
    flashStorage.init();
    analoginput.init();
    rcinput_.init();
    rcoutput_.init();

    scheduler->init();

    scheduler_._main = 
    xTaskCreateStatic(hal_infineon_main_task,
        "APM_MAIN",
        HAL_INFENION_MAIN_TASK_STACK_SIZE,
        callbacks,
        RTOS_PRIORITY_MAIN,
        hal_infineon_main_task_stack,
        &hal_infineon_main_task_buffer
    );
    CY_ASSERT(scheduler_._main != nullptr);

    vTaskStartScheduler();

    CY_ASSERT(0);
    for(;;);
}

static HAL_Infineon hal_Infineon;

const AP_HAL::HAL& AP_HAL::get_HAL() {
    return hal_Infineon;
}

AP_HAL::HAL& AP_HAL::get_HAL_mutable() {
    return hal_Infineon;
}

#endif
