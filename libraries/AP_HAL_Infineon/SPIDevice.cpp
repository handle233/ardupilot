#include "AP_HAL_Infineon.h"
#include "SPIDevice.h"
#include "Scheduler.h"

#include <string.h>

using namespace Infineon;
extern const AP_HAL::HAL& hal;

SPI_DIFINITION;
SPI_BUS_DEFINITION;

SPIDEV_DEFINITION;

Infineon::SPIBus::SPIBus(infineon_spi_def_t &spi_info) :
    _initialised(false),
    _spi_info(spi_info),
    waiting_task(nullptr)
{

}

bool Infineon::SPIBus::init()
{
    Cy_GPIO_Pin_FastInit(MAKE_PORT(_spi_info.mosi), MAKE_NUM(_spi_info.mosi),
                         CY_GPIO_DM_STRONG_IN_OFF, 0, (en_hsiom_sel_t)_spi_info.mosi_hsiom);
    Cy_GPIO_Pin_FastInit(MAKE_PORT(_spi_info.miso), MAKE_NUM(_spi_info.miso),
                         CY_GPIO_DM_HIGHZ, 0, (en_hsiom_sel_t)_spi_info.miso_hsiom);
    Cy_GPIO_Pin_FastInit(MAKE_PORT(_spi_info.sck), MAKE_NUM(_spi_info.sck),
                         CY_GPIO_DM_STRONG_IN_OFF, 1, (en_hsiom_sel_t)_spi_info.sck_hsiom);
    for (int i=0; i<_spi_info.ss_num; i++) {
        Cy_GPIO_Pin_FastInit(MAKE_PORT(_spi_info.ss[i]), MAKE_NUM(_spi_info.ss[i]),
                             CY_GPIO_DM_STRONG_IN_OFF, 1, (en_hsiom_sel_t)_spi_info.ss_hsiom[i]);
    }

    cy_stc_scb_spi_config_t scb_spi_config = {
        .spiMode = CY_SCB_SPI_MASTER,
        .subMode = CY_SCB_SPI_MOTOROLA,
        .sclkMode = CY_SCB_SPI_CPHA0_CPOL0,
        .parity = CY_SCB_SPI_PARITY_NONE,
        .dropOnParityError = false,
        .oversample = _spi_info.prescaler,
        .rxDataWidth = 8UL,
        .txDataWidth = 8UL,
        .enableMsbFirst = true,
        .enableFreeRunSclk = false,
        .enableInputFilter = false,
        .enableMisoLateSample = true,
        .enableTransferSeperation = false,
        .ssPolarity = (CY_SCB_SPI_ACTIVE_LOW << CY_SCB_SPI_SLAVE_SELECT0)|
        (CY_SCB_SPI_ACTIVE_LOW << CY_SCB_SPI_SLAVE_SELECT1)|
        (CY_SCB_SPI_ACTIVE_LOW << CY_SCB_SPI_SLAVE_SELECT2)|
        (CY_SCB_SPI_ACTIVE_LOW << CY_SCB_SPI_SLAVE_SELECT3),
        .ssSetupDelay = false,
        .ssHoldDelay = false,
        .ssInterFrameDelay = false,
        .enableWakeFromSleep = false,
        .rxFifoTriggerLevel = 63UL,
        .rxFifoIntEnableMask = 0UL,
        .txFifoTriggerLevel = 63UL,
        .txFifoIntEnableMask = 0UL,
        .masterSlaveIntEnableMask = 0UL,
    };

    switch (_spi_info.mode) {
    case 0:
        scb_spi_config.sclkMode = CY_SCB_SPI_CPHA0_CPOL0;
        break;
    case 1:
        scb_spi_config.sclkMode = CY_SCB_SPI_CPHA1_CPOL0;
        break;
    case 2:
        scb_spi_config.sclkMode = CY_SCB_SPI_CPHA0_CPOL1;
        break;
    case 3:
        scb_spi_config.sclkMode = CY_SCB_SPI_CPHA1_CPOL1;
        break;
    }

    Cy_SCB_SPI_Init((CySCB_Type*)_spi_info.scb, &scb_spi_config, &spiContext);

    infineon_init_clock((en_clk_dst_t)_spi_info.clk_dst, CY_SYSCLK_DIV_8_BIT,
                        _spi_info.divider, 0);

    //enable
    Cy_SCB_SPI_Enable((CySCB_Type*)_spi_info.scb);

    const BaseType_t task_created = xTaskCreate(periodic_process,
     "SPI Process", INFENION_SPI_PROCESS_STACK_SIZE, this,
     RTOS_PRIORITY_SPI_PROCESS, &task);
    if (task_created != pdPASS) {
        return false;
    }

    _initialised = true;

    return true;
}

bool Infineon::SPIBus::deinit()
{
    Cy_SCB_SPI_Disable((CySCB_Type*)_spi_info.scb, &spiContext);
    Cy_SCB_SPI_DeInit((CySCB_Type*)_spi_info.scb);
    return true;
}

bool Infineon::SPIBus::transfer(uint8_t address, const uint8_t *send, uint8_t *recv, uint32_t len)
{
    if (!bus_lock.check_owner()) {
        return false;
    }

    CySCB_Type *const scb = (CySCB_Type*)_spi_info.scb;
    //waiting_task = xTaskGetCurrentTaskHandle();
    //Cy_SCB_SPI_ClearRxFifoStatus(scb, CY_SCB_SPI_RX_OVERFLOW | CY_SCB_SPI_RX_UNDERFLOW);
    Cy_SCB_SPI_SetActiveSlaveSelect(scb, (cy_en_scb_spi_slave_select_t)address); // 选中片选
    //_event = 0;

    uint32_t rx_len = 0,tx_len = 0;
    const uint32_t timeout_us = len + 100U;
    const uint32_t start_us = AP_HAL::micros();


    for(rx_len = 0;rx_len<len;){
        if(tx_len < len){
            tx_len += Cy_SCB_SPI_WriteArray(scb, (void *)&send[tx_len], len-tx_len);
        }

        uint32_t ready = Cy_SCB_SPI_GetNumInRxFifo(scb);

        for(uint32_t a = 0;a < ready;a++){
            recv[rx_len++] = Cy_SCB_ReadRxFifo(scb);
        }

        if (AP_HAL::micros() - start_us > timeout_us) {
            Cy_SCB_SPI_ClearTxFifo(scb);
            Cy_SCB_SPI_ClearRxFifo(scb);
            waiting_task = nullptr;
            return false;
        }
    }

    const uint32_t rx_status = Cy_SCB_SPI_GetRxFifoStatus(scb);

    if ((rx_status & CY_SCB_SPI_RX_OVERFLOW) != 0) {// || received != len
        Cy_SCB_SPI_ClearRxFifo(scb);
        Cy_SCB_SPI_ClearRxFifoStatus(scb, CY_SCB_SPI_RX_OVERFLOW | CY_SCB_SPI_RX_UNDERFLOW);
        return false;
    }
    

    return true;
}

void Infineon::SPIBus::spi_isr()
{
    Cy_SCB_SPI_Interrupt((CySCB_Type*)_spi_info.scb, &spiContext);
}

/*
* interrupt drived SPI communication is implemented,
* but it is not used in the current version, 
* because the implement now is more efficient and simple
*/
void Infineon::SPIBus::spi_event_isr(uint32_t events)
{
    _event = events;
    if (_event & (CY_SCB_SPI_TRANSFER_CMPLT_EVENT|
                  CY_SCB_SPI_TRANSFER_ERR_EVENT)) {
        TaskHandle_t task_to_notify = waiting_task;
        if (task_to_notify != nullptr) {
            // xTaskNotifyIndexedFromISR(task_to_notify,
            //                           notification_index,
            //                           notification_bit,
            //                           eSetBits,
            //                           &higher_priority_task_woken);
            // //t3 += AP_HAL::micros64()-tick;
            // portYIELD_FROM_ISR(true);
        }
    }
}

void *Infineon::SPIBus::register_periodic_process(uint32_t period_usec, AP_HAL::Device::PeriodicCb cb)
{
    if (period_usec == 0) {
        return nullptr;
    }

    taskENTER_CRITICAL();
    if (periodic_cb_count >= MAX_PERIODIC_CALLBACKS) {
        taskEXIT_CRITICAL();
        return nullptr;
    }
    periodic_cb[periodic_cb_count].period_usec = period_usec;
    periodic_cb[periodic_cb_count].cb = cb;
    periodic_cb[periodic_cb_count].next_usec = AP_HAL::micros64() + period_usec;
    void *handle = &periodic_cb[periodic_cb_count];
    periodic_cb_count++;
    taskEXIT_CRITICAL();
    return handle;
}

void Infineon::SPIBus::periodic_process(void* pdata)
{
    SPIBus* pthis = (SPIBus*)pdata;

    while (!pthis->_initialised) {
        vTaskDelay(1);
    }
    
    while(pthis->periodic_cb_count == 0) {
        vTaskDelay(1);
    }

    for (;;) {
        uint64_t now = AP_HAL::micros64();
        uint64_t nearest = 0;
        const uint8_t cb_count = pthis->periodic_cb_count;
        //do periodic process
        for (uint8_t i = 0; i < cb_count; i++) {
            PeriodicSlot &slot = pthis->periodic_cb[i];
            if (now >= slot.next_usec) {
                while (now >= slot.next_usec) {
                    slot.next_usec += slot.period_usec;
                }
                pthis->bus_lock.take_blocking();
                slot.cb();
                pthis->bus_lock.give();
            }
        }
        //seek for next execute.
        now = AP_HAL::micros64();
        for (uint8_t i = 0; i < cb_count; i++) {
            PeriodicSlot &slot = pthis->periodic_cb[i];

            if (nearest == 0 ||
                slot.next_usec < nearest) {
                nearest = slot.next_usec;
                if(nearest < now){
                    nearest = now;
                }
            }
        }

        // delay for at most 50ms, to handle newly added callbacks
        uint32_t delay = 50000;
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

void Infineon::SPIBus::set_speed(AP_HAL::Device::Speed speed)
{
    Cy_SCB_SPI_Disable((CySCB_Type*)_spi_info.scb,&spiContext);
    switch (speed) {
    case AP_HAL::Device::Speed::SPEED_LOW:
        infineon_init_clock((en_clk_dst_t)_spi_info.clk_dst, CY_SYSCLK_DIV_8_BIT,
                            _spi_info.divider, 9);
        break;
    case AP_HAL::Device::Speed::SPEED_HIGH:
        infineon_init_clock((en_clk_dst_t)_spi_info.clk_dst, CY_SYSCLK_DIV_8_BIT,
                            _spi_info.divider, 0);
        break;
    }
    Cy_SCB_SPI_Enable((CySCB_Type*)_spi_info.scb);
}

AP_HAL::SPIDevice *Infineon::SPIDeviceManager::get_device_ptr(const char *name)
{
    for (auto& dev : spidevs) {
        if (strcmp(dev->dev_name,name)==0) {

            for (auto &_bus : SPIBuses) {
                if (_bus->get_bus_id()==dev->busid) {
                    return NEW_NOTHROW SPIDevice(*_bus,*dev);
                }
            }
        }
    }
    return nullptr;
}

bool Infineon::SPIDeviceManager::initialize()
{
    for (auto &spibus : SPIBuses) {
        if (!spibus->init()) {
            CY_ASSERT(0);
            return false;
        }
    }
    return true;
}

Infineon::SPIDevice::SPIDevice(SPIBus &bus,infineon_spidev_def_t &_spi_dev):
    _bus(bus),dev_info(_spi_dev)
{
    _speed = SPEED_HIGH;
}

Infineon::SPIDevice::~SPIDevice()
{

}

bool Infineon::SPIDevice::set_speed(AP_HAL::Device::Speed speed)
{
    _bus.set_speed(speed);
    return true;
}

bool Infineon::SPIDevice::transfer(const uint8_t *send, uint32_t send_len, uint8_t *recv, uint32_t recv_len)
{
    static uint8_t txbuf[max_spi_trans_len] = {0},
     rxbuf[max_spi_trans_len] = {0};
    const uint32_t total_len = send_len + recv_len;

    memcpy(txbuf, send, send_len);

    const bool ret = _bus.transfer(dev_info.cs,txbuf,rxbuf,total_len);
    if (ret && recv_len > 0) {
        memcpy(recv, &rxbuf[send_len], recv_len);
    }

    return ret;
}

bool Infineon::SPIDevice::transfer_fullduplex(const uint8_t *send, uint8_t *recv, uint32_t len)
{
    return _bus.transfer(dev_info.cs, send, recv, len);
}

bool Infineon::SPIDevice::transfer_fullduplex(uint8_t *send_recv, uint32_t len)
{
    uint8_t buf[len];
    const bool ret = _bus.transfer(dev_info.cs,send_recv,buf,len);
    memcpy(send_recv, buf, len);
    return ret;
}

AP_HAL::Semaphore *Infineon::SPIDevice::get_semaphore()
{
    return _bus.get_semaphore();
}

AP_HAL::Device::PeriodicHandle Infineon::SPIDevice::register_periodic_callback(uint32_t period_usec,
        AP_HAL::Device::PeriodicCb cb)
{
    return _bus.register_periodic_process(period_usec,cb);
}

bool Infineon::SPIDevice::adjust_periodic_callback(PeriodicHandle h, uint32_t period_usec){
    SPIBus::PeriodicSlot *slot = (SPIBus::PeriodicSlot*)h;
    if(slot) {
        slot->period_usec = period_usec;
        slot->next_usec = AP_HAL::micros() + period_usec;
        return true;
    }
    return false;
}

