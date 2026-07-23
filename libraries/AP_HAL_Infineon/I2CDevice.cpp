#include "AP_HAL_Infineon.h"
#include "I2CDevice.h"
#include "Scheduler.h"

#include <string.h>

using namespace Infineon;
extern const AP_HAL::HAL& hal;

I2C_DIFINITION;
I2C_BUS_DEFINITION;

I2CBus::I2CBus(infineon_i2c_def_t &i2c_info) :
    _initialised(false),
    _i2c_info(i2c_info)
{

}

bool I2CBus::init(){
    Cy_GPIO_Pin_FastInit(MAKE_PORT(_i2c_info.scl), MAKE_NUM(_i2c_info.scl),
     CY_GPIO_DM_OD_DRIVESLOW, 1, (en_hsiom_sel_t)_i2c_info.scl_hsiom);
    Cy_GPIO_Pin_FastInit(MAKE_PORT(_i2c_info.sda), MAKE_NUM(_i2c_info.sda),
     CY_GPIO_DM_OD_DRIVESLOW, 0, (en_hsiom_sel_t)_i2c_info.sda_hsiom);

    const cy_stc_scb_i2c_config_t scb_8_config =
    {
        .i2cMode = CY_SCB_I2C_MASTER,
        .useRxFifo = true,
        .useTxFifo = true,
        .slaveAddress = 0U,
        .slaveAddressMask = 0U,
        .acceptAddrInFifo = false,
        .ackGeneralAddr = false,
        .enableWakeFromSleep = false,
        .enableDigitalFilter = false,
        .lowPhaseDutyCycle = 10,
        .highPhaseDutyCycle = 10,
    };

    Cy_SCB_I2C_Init((CySCB_Type*)_i2c_info.scb, &scb_8_config, &i2cContext);

    infineon_init_clock((en_clk_dst_t)_i2c_info.clk_dst, CY_SYSCLK_DIV_8_BIT,
     _i2c_info.divider, 50);//初始化默认为100k，之后调用setspeed设置
    
    cy_stc_sysint_t uart_intr_config = {
        .intrSrc = (((uint32_t)NVIC_MUX_I2C_INT << CY_SYSINT_INTRSRC_MUXIRQ_SHIFT) | \
                                 (uint32_t)_i2c_info.irq_num),
        .intrPriority = BSP_PRIORITY_I2C1
    };


    Cy_SCB_I2C_RegisterEventCallback((CySCB_Type*)_i2c_info.scb, 
    (cy_cb_scb_i2c_handle_events_t)_i2c_info.event_callback, &i2cContext);
    
    cy_en_sysint_status_t intstatus = Cy_SysInt_Init(&uart_intr_config, (cy_israddress)_i2c_info.callback);
    CY_ASSERT(intstatus == CY_SYSINT_SUCCESS);

    
    NVIC_ClearPendingIRQ(NVIC_MUX_I2C_INT);
    NVIC_EnableIRQ(NVIC_MUX_I2C_INT);

    //启动
    Cy_SCB_I2C_Enable((CySCB_Type*)_i2c_info.scb);

    
    xTaskCreate(periodic_process,
        "I2C Process",
        INFENION_I2C_PROCESS_STACK_SIZE,
        this,
        RTOS_PRIORITY_I2C_PROCESS,
        &task);

    _initialised = true;

    set_speed(AP_HAL::Device::SPEED_HIGH);

    return true;
}

bool Infineon::I2CBus::deinit()
{
    Cy_SCB_I2C_Disable((CySCB_Type*)_i2c_info.scb, &i2cContext);
    Cy_SCB_I2C_DeInit((CySCB_Type*)_i2c_info.scb);

    _initialised = false;
    return true;
}

bool Infineon::I2CBus::transfer(uint8_t address,
     const uint8_t *send, uint32_t send_len, uint8_t *recv, uint32_t recv_len, uint32_t timeout_ms)
{

    cy_stc_scb_i2c_master_xfer_config_t transfer = {0};

    if(send_len > 0) {
        transfer.slaveAddress = address;
        transfer.buffer = (uint8_t *)send;
        transfer.bufferSize = send_len;
        transfer.xferPending = recv_len > 0; //如果还有接收数据则发送后不发送stop
        
        while(isr_signal.wait_nonblocking());
        if(CY_SCB_I2C_SUCCESS != Cy_SCB_I2C_MasterWrite((CySCB_Type*)_i2c_info.scb,&transfer,&i2cContext)) {
            return false;
        }

        if(isr_signal.wait(timeout_ms * 1000)) {
            if(_event & CY_SCB_I2C_MASTER_ERR_EVENT) {
                return false;
            }
        } else {
            Cy_SCB_I2C_MasterAbortWrite((CySCB_Type*)_i2c_info.scb, &i2cContext);
            return false;
        }
    }

    if(recv_len > 0) {
        transfer.slaveAddress = address;
        transfer.buffer = recv;
        transfer.bufferSize = recv_len;
        transfer.xferPending = false;

        while(isr_signal.wait_nonblocking());
        if(CY_SCB_I2C_SUCCESS != Cy_SCB_I2C_MasterRead((CySCB_Type*)_i2c_info.scb,&transfer,&i2cContext)) {
            return false;
        }

        if(isr_signal.wait(timeout_ms * 1000)) {
            if(_event & CY_SCB_I2C_MASTER_ERR_EVENT) {
                return false;
            }
        } else {
            Cy_SCB_I2C_MasterAbortRead((CySCB_Type*)_i2c_info.scb, &i2cContext);
            return false;
        }
    }
    return true;
}

void Infineon::I2CBus::i2c_isr()
{
    Cy_SCB_I2C_Interrupt((CySCB_Type*)_i2c_info.scb, &i2cContext);
}

void Infineon::I2CBus::i2c_event_isr(uint32_t event)
{
    _event = event;
    if(event & (CY_SCB_I2C_MASTER_WR_CMPLT_EVENT|
      CY_SCB_I2C_MASTER_RD_CMPLT_EVENT|CY_SCB_I2C_MASTER_ERR_EVENT)) {
        isr_signal.signal_ISR();
    }
}

void* Infineon::I2CBus::register_periodic_process(uint32_t period_usec, AP_HAL::Device::PeriodicCb cb)
{
    if(periodic_cb_count >= MAX_PERIODIC_CALLBACKS) {
        return nullptr;
    }
    periodic_cb[periodic_cb_count].period_usec = period_usec;
    periodic_cb[periodic_cb_count].cb = cb;
    periodic_cb[periodic_cb_count].next_usec = AP_HAL::micros() + period_usec;
    periodic_cb_count++;
    return &periodic_cb[periodic_cb_count-1];
}

void Infineon::I2CBus::periodic_process(void* pthis)
{
    I2CBus *pbus = (I2CBus*)pthis;
    while (!pbus->_initialised)
    {
        vTaskDelay(1);
    }

    for(;;){
        uint64_t now = AP_HAL::micros64();
        uint64_t nearest = 0;
        const uint8_t cb_count = pbus->periodic_cb_count;

        if (cb_count == 0) {
            vTaskDelay(1);
            continue;
        }

        //执行periodic
        for (uint8_t i = 0; i < cb_count; i++) {
            PeriodicSlot &slot = pbus->periodic_cb[i];

            if (now >= slot.next_usec) {
                while (now >= slot.next_usec) {
                    slot.next_usec += slot.period_usec;
                }
                pbus->bus_lock.take_blocking();
                slot.cb();
                pbus->bus_lock.give();
            }
        }
        //查找下一个执行点
        now = AP_HAL::micros64();
        for (uint8_t i = 0; i < cb_count; i++) {
            PeriodicSlot &slot = pbus->periodic_cb[i];

            if (nearest == 0 ||
                slot.next_usec < nearest) {
                nearest = slot.next_usec;
                if(nearest < now){
                    nearest = now;
                }
            }
        }

        // delay for at most 100ms, to handle newly added callbacks
        uint32_t delay = 100000;
        if (nearest >= now && nearest - now < delay) {
            delay = nearest - now;
        }
        // don't delay for less than 100usec, so one thread doesn't
        // completely dominate the CPU
        if (delay < 100) {
            delay = 100;
        }
        hal.scheduler->delay_microseconds(delay);
    }
}

void Infineon::I2CBus::set_speed(AP_HAL::Device::Speed speed)
{
    Cy_SCB_I2C_Disable((CySCB_Type*)_i2c_info.scb, &i2cContext);
    switch (speed) {
        case AP_HAL::Device::Speed::SPEED_LOW:
            infineon_init_clock((en_clk_dst_t)_i2c_info.clk_dst, CY_SYSCLK_DIV_8_BIT,
            _i2c_info.divider, 50);
            Cy_SCB_I2C_SetDataRate((CySCB_Type*)_i2c_info.scb, 100, 2000000);
            break;
        case AP_HAL::Device::Speed::SPEED_HIGH:
            infineon_init_clock((en_clk_dst_t)_i2c_info.clk_dst, CY_SYSCLK_DIV_8_BIT,
            _i2c_info.divider, 10);
            Cy_SCB_I2C_SetDataRate((CySCB_Type*)_i2c_info.scb, 400, 10000000);
            break;
    }
    Cy_SCB_I2C_Enable((CySCB_Type*)_i2c_info.scb);
}

AP_HAL::I2CDevice* Infineon::I2CDeviceManager::get_device_ptr(uint8_t bus, uint8_t address,
     uint32_t bus_clock,
     bool use_smbus,
     uint32_t timeout_ms){
    for(auto &i2cbus : I2CBuses) {
        if(i2cbus->get_bus_id() == bus) {
            return NEW_NOTHROW I2CDevice
            (i2cbus, address,
                bus_clock>100000?(AP_HAL::Device::Speed::SPEED_HIGH)
                :(AP_HAL::Device::Speed::SPEED_LOW), use_smbus, timeout_ms);
        }
    }
    return nullptr;
}

uint32_t Infineon::I2CDeviceManager::get_bus_mask(void) const
{
    uint32_t mask = 0;
    for(auto &i2cbus : I2CBuses) {
        const uint8_t bus_id = i2cbus->get_bus_id();
        if(bus_id < 32) {
            mask |= 1U << bus_id;
        }
    }
    return mask;
}

uint32_t Infineon::I2CDeviceManager::get_bus_mask_external(void) const
{
    return get_bus_mask();
}

uint32_t Infineon::I2CDeviceManager::get_bus_mask_internal(void) const
{
    return 0;
}

bool Infineon::I2CDeviceManager::initialize()
{
    for(auto &i2cbus : I2CBuses) {
        if(!i2cbus->init()) {
            CY_ASSERT(0);
            return false;
        }
    }
    return true;
}

Infineon::I2CDevice::I2CDevice(I2CBus *bus, uint8_t address, AP_HAL::Device::Speed speed,
     bool use_smbus, uint32_t timeout_ms) :
     _bus(*bus), _address(address), _speed(speed), _use_smbus(use_smbus), _timeout_ms(timeout_ms)
{
    set_device_address(address);
    set_device_bus(bus->get_bus_id());
    _retries = 0;
}

bool Infineon::I2CDevice::transfer(const uint8_t *send, 
    uint32_t send_len, uint8_t *recv, uint32_t recv_len){

    for (uint8_t attempt = 0; attempt <= _retries; attempt++) {
        if (_bus.transfer(_address, send, send_len, recv, recv_len, _timeout_ms)) {
            return true;
        }
    }
    return false;
}

bool Infineon::I2CDevice::read_registers_multiple(uint8_t first_reg, uint8_t *recv, 
    uint32_t recv_len, uint8_t times){
    if(recv == nullptr || recv_len == 0 || times == 0) {
        return false;
    }

    for (uint8_t time = 0; time < times; time++) {
        if (!transfer(&first_reg, 1, recv + time * recv_len, recv_len)) {
            return false;
        }
    }
    return true;
}

AP_HAL::Device::PeriodicHandle Infineon::I2CDevice::register_periodic_callback(
    uint32_t period_usec, AP_HAL::Device::PeriodicCb cb){
    return _bus.register_periodic_process(period_usec, cb);
}

bool Infineon::I2CDevice::adjust_periodic_callback(
    AP_HAL::Device::PeriodicHandle h, uint32_t period_usec){
    I2CBus::PeriodicSlot *slot = (I2CBus::PeriodicSlot*)h;
    if(slot) {
        slot->period_usec = period_usec;
        slot->next_usec = AP_HAL::micros() + period_usec;
        return true;
    }
    return false;
}
