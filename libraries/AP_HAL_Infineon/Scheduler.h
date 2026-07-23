#pragma once
#include "AP_HAL/AP_HAL.h"
#include "AP_HAL_Infineon.h"
#include "infineon.h"

//bsp 中断优先级定义，数值越小优先级越高 不能超过7
#define BSP_PRIORITY_BASE 3         //小于这个的优先级不受RTOS调度影响，适合时钟中断等需要高精度的中断
#define BSP_PRIORITY_DMA            BSP_PRIORITY_BASE - 1
#define BSP_PRIORITY_TICKTIMER      BSP_PRIORITY_BASE - 2
#define BSP_PRIORITY_SPI1           BSP_PRIORITY_BASE + 0
#define BSP_PRIORITY_DELAYTIMER     BSP_PRIORITY_BASE + 0
#define BSP_PRIORITY_PWM_TIMER      BSP_PRIORITY_BASE + 0
#define BSP_PRIORITY_SPI_INTERRUPT  BSP_PRIORITY_BASE + 0
#define BSP_PRIORITY_I2C1           BSP_PRIORITY_BASE + 1
#define BSP_PRIORITY_UART           BSP_PRIORITY_BASE + 0

//rtos线程优先级定义，数值越大优先级越高
#define RTOS_PRIORITY_STORAGE_PROCESS tskIDLE_PRIORITY+5
#define RTOS_PRIORITY_ANALOG_PROCESS tskIDLE_PRIORITY+6
#define RTOS_PRIORITY_I2C_PROCESS tskIDLE_PRIORITY+10
#define RTOS_PRIORITY_IO_PROCESS tskIDLE_PRIORITY+9
#define RTOS_PRIORITY_MAIN tskIDLE_PRIORITY+12
#define RTOS_PRIORITY_TIMER_PROCESS tskIDLE_PRIORITY+13
#define RTOS_PRIORITY_SPI_PROCESS tskIDLE_PRIORITY+14

#define RTOS_PRIORITY_STORAGE_BOOST tskIDLE_PRIORITY+11
#define RTOS_PRIORITY_MAIN_BOOST tskIDLE_PRIORITY+15

//NvicMux定义,决定中断向量的分配
#define NVIC_MUX_UART_DMA NvicMux2_IRQn
#define NVIC_MUX_TICKTIMER NvicMux5_IRQn
#define NVIC_MUX_DELAYTIMER NvicMux6_IRQn
#define NVIC_MUX_I2C_INT NvicMux4_IRQn
#define NVIC_MUX_SPI_INT NvicMux4_IRQn

/* 注意检查，此处优先级定义不一定正确，参考具体实现 */

#define MAX_PROCESS_NUM 8
#define INFENION_STORAGE_PROCESS_STACK_SIZE (configMINIMAL_STACK_SIZE * 8)*2
#define INFENION_TIMER_PROCESS_STACK_SIZE (configMINIMAL_STACK_SIZE * 8)*2
#define INFENION_IO_PROCESS_STACK_SIZE    (configMINIMAL_STACK_SIZE * 8)*2
#define INFENION_SPI_PROCESS_STACK_SIZE (configMINIMAL_STACK_SIZE * 8)*2
#define INFENION_I2C_PROCESS_STACK_SIZE (configMINIMAL_STACK_SIZE * 8)*2
#define INFENION_ANALOG_PROCESS_STACK_SIZE (configMINIMAL_STACK_SIZE * 8)

class Infineon::Scheduler : public AP_HAL::Scheduler {
public:
    Scheduler() {
        memset(slot,0,sizeof(slot));
    }
    void     init() override;
    void     delay(uint16_t ms) override;
    void     delay_microseconds(uint16_t us) override;
    void     delay_microseconds_boost(uint16_t us) override;
    void     register_timer_process(AP_HAL::MemberProc) override;
    void     register_io_process(AP_HAL::MemberProc) override;

    void     boost_end(void) override;

    void     register_timer_failsafe(AP_HAL::Proc, uint32_t period_us) override;

    void     set_system_initialized() override {_initialized = true; }
    bool     is_system_initialized() override { return _initialized; }

    void     reboot(bool hold_in_bootloader) override;

    bool     in_main_thread() const override {return xTaskGetCurrentTaskHandle() == _main;}

    virtual bool thread_create(AP_HAL::MemberProc proc, const char *name,
                               uint32_t stack_size, priority_base base, int8_t priority)override;

    TaskHandle_t _main;
private:
    constexpr static const uint8_t max_boost = 8;
    struct BoostThread{
        TaskHandle_t task;
        UBaseType_t basePriority;
    }slot[max_boost];


    volatile bool _initialized = false;
    StaticTask_t _timer_task_buffer;
    StackType_t _timer_task_stack[INFENION_TIMER_PROCESS_STACK_SIZE];
    StaticTask_t _io_task_buffer;
    StackType_t _io_task_stack[INFENION_IO_PROCESS_STACK_SIZE];

    AP_HAL::MemberProc _timer_proc[MAX_PROCESS_NUM];
    AP_HAL::Proc _timer_failsafe_proc;
    uint8_t _timer_proc_count;
    volatile bool _in_timer_Proc;
    /*timer线程入口函数声明*/
    static void timer_process(void *param);
    
    AP_HAL::MemberProc _io_proc[MAX_PROCESS_NUM];
    uint8_t _io_proc_count;
    volatile bool _in_io_Proc;
    /*io线程入口函数声明*/
    static void io_process(void *param);

    /*thread create 通用入口*/
    static void thread_entry(void *param);
};
