#include "AP_HAL_Infineon.h"
#include "Scheduler.h"

#include "I2CDevice.h"
#include "SPIDevice.h"
#include "HighPrecisionDelay.h"

#include <stdlib.h>

using namespace Infineon;
extern const AP_HAL::HAL& hal;

void tick_isr(void){
    AP_HAL::micros64();
    Cy_TCPWM_ClearInterrupt(TCPWM0, 512, CY_TCPWM_INT_ON_TC);
}

DelayTimer tim;

void Scheduler::init(){
    //初始化tick时钟
    infineon_init_clock(PCLK_TCPWM0_CLOCKS512, CY_SYSCLK_DIV_8_BIT, 0, 99);

    cy_stc_tcpwm_counter_config_t counter0 =
    {
        .period            = (2000000UL-1UL),
        .clockPrescaler    = CY_TCPWM_COUNTER_PRESCALER_DIVBY_1,
        .runMode           = CY_TCPWM_COUNTER_CONTINUOUS,
        .countDirection    = CY_TCPWM_COUNTER_COUNT_UP,
        .compareOrCapture  = CY_TCPWM_COUNTER_MODE_COMPARE,
        .interruptSources  = CY_TCPWM_INT_ON_TC,
        .countInputMode    = CY_TCPWM_INPUT_LEVEL,
        .countInput        = CY_TCPWM_INPUT_1,
        .trigger0Event     = CY_TCPWM_CNT_TRIGGER_ON_DISABLED,
        .trigger1Event     = CY_TCPWM_CNT_TRIGGER_ON_DISABLED,
    };
    Cy_TCPWM_Counter_Init(TCPWM0, 512, &counter0);
    Cy_TCPWM_Counter_Enable(TCPWM0, 512);

    //配中断
    cy_stc_sysint_t irqCfg =
    {
        .intrSrc = (NVIC_MUX_TICKTIMER << CY_SYSINT_INTRSRC_MUXIRQ_SHIFT)|tcpwm_0_interrupts_512_IRQn,
        .intrPriority = BSP_PRIORITY_TICKTIMER
    };

    Cy_SysInt_Init(&irqCfg, tick_isr);
    NVIC_ClearPendingIRQ(NVIC_MUX_TICKTIMER);
    NVIC_EnableIRQ(NVIC_MUX_TICKTIMER);
    
    Cy_TCPWM_Counter_SetCounter(TCPWM0, 512U, 0U);
    Cy_TCPWM_TriggerStart_Single(TCPWM0,512);

    tim.init();

    //开启DWT计数器
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    //开启 timer/IO 线程
    TaskHandle_t timer_task = xTaskCreateStatic(Scheduler::timer_process,
        "Timer Process",
        INFENION_TIMER_PROCESS_STACK_SIZE,
        this,
        RTOS_PRIORITY_TIMER_PROCESS,
        _timer_task_stack,
        &_timer_task_buffer);
    CY_ASSERT(timer_task != nullptr);

    TaskHandle_t io_task = xTaskCreateStatic(Scheduler::io_process,
        "IO Process",
        INFENION_IO_PROCESS_STACK_SIZE,
        this,
        RTOS_PRIORITY_IO_PROCESS,
        _io_task_stack,
        &_io_task_buffer);
    CY_ASSERT(io_task != nullptr);
}

void Scheduler::delay(uint16_t ms){
    uint64_t start = AP_HAL::micros64();
    while ((AP_HAL::micros64() - start)/1000 < ms) {
        if (_min_delay_cb_ms <= ms) {
            if (in_main_thread()) {
                call_delay_cb();
            }
        }
        vTaskDelay(1);
    }
}

inline void Scheduler::delay_microseconds(uint16_t us){
    uint64_t target = AP_HAL::micros64() + us;

    if(us>1500){
        vTaskDelay(pdMS_TO_TICKS((us-500)/1000));
    }
    uint64_t current = AP_HAL::micros64();
    if(current > target){
        return;
    }
    if(target-current>50){
        tim.AcquireADelay(target-current);
    }
    current = AP_HAL::micros64();
    if(current > target){
        return;
    }
    if(target - current > 0){
        uint32_t cycles_per_us = SystemCoreClock / 1000000UL;
        uint32_t ticks = (target - current) * cycles_per_us;
        uint32_t start = DWT->CYCCNT;
        while ((uint32_t)(DWT->CYCCNT - start) < ticks) {
            __NOP();
        }
    }
}

void Scheduler::delay_microseconds_boost(uint16_t us) {
    if(in_main_thread()){
        vTaskPrioritySet(xTaskGetCurrentTaskHandle(),RTOS_PRIORITY_MAIN_BOOST);
    }else{
        // int i=0;
        // for(i=0;i<max_boost;i++){
        //     if(slot[i].task==nullptr){
        //         slot[i].task = xTaskGetCurrentTaskHandle();
        //         slot[i].basePriority = uxTaskPriorityGet(slot[i].task);
        //         vTaskPrioritySet(slot[i].task,slot[i].basePriority+1);
        //         break;
        //     }
        // }
        // if(i==8){
        //     DEV_PRINTF("boost_over_flow\n");
        //     CY_ASSERT(0);
        //     return;
        // }
    }
    delay_microseconds(us);
}

void Scheduler::register_timer_process(AP_HAL::MemberProc proc){
    //检查重复
    for(uint8_t i = 0; i < _timer_proc_count; i++) {
        if (_timer_proc[i] == proc) {
            DEV_PRINTF("Timer process already registered\n");
            return;
        }
    }

    //没有重复且任务数量没溢出就添加
    if(_timer_proc_count < MAX_PROCESS_NUM) {
        _timer_proc[_timer_proc_count] = proc;
        _timer_proc_count++;
    } else {
        DEV_PRINTF("Out of timer processes\n");
    }
}

void Scheduler::register_timer_failsafe(AP_HAL::Proc proc, uint32_t period_us){
    _timer_failsafe_proc = proc;
}

void Scheduler::register_io_process(AP_HAL::MemberProc proc) {
    //检查重复
    for(uint8_t i = 0; i < _io_proc_count; i++) {
        if (_io_proc[i] == proc) {
            DEV_PRINTF("IO process already registered\n");
            return;
        }
    }

    //没有重复且任务数量没溢出就添加
    if(_io_proc_count < MAX_PROCESS_NUM) {
        _io_proc[_io_proc_count] = proc;
        _io_proc_count++;
    } else {
        DEV_PRINTF("Out of IO processes\n");
    }
}

void Infineon::Scheduler::boost_end(void)
{
    if(in_main_thread()){
        vTaskPrioritySet(xTaskGetCurrentTaskHandle(),RTOS_PRIORITY_MAIN);
    }else{
        // TaskHandle_t _task = xTaskGetCurrentTaskHandle();
        // for(int i=0;i<max_boost;i++){
        //     if(slot[i].task==_task){
        //         vTaskPrioritySet(slot[i].task,slot[i].basePriority);
        //         slot[i].task = nullptr;
        //         break;
        //     }
        // }
    }
}

void Scheduler::timer_process(void *param){
    Scheduler *sched = (Scheduler *)param;

    while (!sched->_initialized)
    {
        vTaskDelay(1);
    }

    for(;;){
        vTaskDelay(1);

        if(sched->_in_timer_Proc) {
            continue;
        }
        
        sched->_in_timer_Proc = true;
        
        for(uint8_t i = 0; i < sched->_timer_proc_count; i++) {
            if(sched->_timer_proc[i]) {
                sched->_timer_proc[i]();
            }
        }

        if(sched->_timer_failsafe_proc) {
            sched->_timer_failsafe_proc();
        }

        sched->_in_timer_Proc = false;
    }
}

void Scheduler::io_process(void *param) {
    Scheduler *sched = (Scheduler *)param;

    while (!sched->_initialized)
    {
        vTaskDelay(1);
    }

    for(;;){
        vTaskDelay(1);

        if(sched->_in_io_Proc) {
            continue;
        }
        
        sched->_in_io_Proc = true;
        
        for(uint8_t i = 0; i < sched->_io_proc_count; i++) {
            if(sched->_io_proc[i]) {
                sched->_io_proc[i]();
            }
        }

        sched->_in_io_Proc = false;
    }
}

void Scheduler::reboot(bool hold_in_bootloader){ NVIC_SystemReset(); }

bool Scheduler::thread_create(AP_HAL::MemberProc proc, const char *name,
 uint32_t stack_size, priority_base base, int8_t priority){
    //优先级映射
    UBaseType_t task_priority;
    switch (base) {
        case PRIORITY_BOOST:
            task_priority = RTOS_PRIORITY_MAIN_BOOST + priority;
            break;
        case PRIORITY_TIMER:
            task_priority = RTOS_PRIORITY_TIMER_PROCESS + priority;
            break;
        case PRIORITY_MAIN:
            task_priority = RTOS_PRIORITY_MAIN + priority;
            break;
        case PRIORITY_SPI:
            task_priority = RTOS_PRIORITY_SPI_PROCESS + priority;
            break;
        case PRIORITY_I2C:
        case PRIORITY_CAN:
            task_priority = RTOS_PRIORITY_I2C_PROCESS + priority;
            break;
        case PRIORITY_IO:
        case PRIORITY_LED:
        case PRIORITY_RCIN:
        case PRIORITY_RCOUT:
            task_priority = RTOS_PRIORITY_IO_PROCESS + priority;
            break;
        case PRIORITY_UART:
        case PRIORITY_STORAGE:
        case PRIORITY_SCRIPTING:
        case PRIORITY_NET:
            task_priority = RTOS_PRIORITY_STORAGE_PROCESS + priority;
            break;
        default:
            return false; // 无效的优先级基准
    }

    AP_HAL::MemberProc *tproc = (AP_HAL::MemberProc *)malloc(sizeof(proc));
    if (tproc == nullptr) {
        return false;
    }
    *tproc = proc;

    TaskHandle_t new_task = nullptr;
    auto ret = xTaskCreate(thread_entry, name, stack_size/4+1, tproc, task_priority, &new_task);

    if (ret != pdPASS) {
        free(tproc);
        CY_ASSERT(ret == pdPASS);
        return false;
    }
    return new_task != nullptr; // 返回线程创建是否成功
}

void Infineon::Scheduler::thread_entry(void *param)
{
    AP_HAL::MemberProc proc = *(AP_HAL::MemberProc *)param;
    free(param);
    proc();

    vTaskDelete(nullptr); // 线程执行完毕后自删除
}
