/*
 * Copyright (C) 2015-2016  Intel Corporation. All rights reserved.
 *
 * This file is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once

#include <inttypes.h>

#include <AP_HAL/HAL.h>
#include <AP_HAL/SPIDevice.h>

#include "Semaphores.h"
#include "infineon.h"

namespace Infineon {

class SPIBus{
public:
    SPIBus(infineon_spi_def_t &spi_info);
    
    bool init();
    bool deinit();

   bool transfer(uint8_t address, const uint8_t *send, uint8_t *recv, uint32_t len);

    AP_HAL::Semaphore *get_semaphore() { return &bus_lock; }

    void spi_isr();
    void spi_event_isr(uint32_t events);
    void* register_periodic_process(uint32_t period_usec, AP_HAL::Device::PeriodicCb);
    struct PeriodicSlot {
        uint32_t period_usec;
        uint64_t next_usec;
        AP_HAL::Device::PeriodicCb cb;
    };
    uint8_t get_bus_id() const { return _spi_info.id; }
    void set_speed(AP_HAL::Device::Speed speed);

    static void periodic_process(void* pdata);
private:

    bool _initialised;
    infineon_spi_def_t &_spi_info;

    cy_stc_scb_spi_context_t spiContext;
    Semaphore bus_lock;
    volatile uint32_t _event;

    TaskHandle_t task;
    volatile TaskHandle_t waiting_task;
    constexpr static UBaseType_t notification_index = 1;
    constexpr static uint32_t notification_bit = 1UL << 0;

    static constexpr uint8_t MAX_PERIODIC_CALLBACKS = 8;
    PeriodicSlot periodic_cb[MAX_PERIODIC_CALLBACKS];
    uint8_t periodic_cb_count = 0;
};

class SPIDevice : public AP_HAL::SPIDevice {
public:
    SPIDevice(SPIBus &bus,infineon_spidev_def_t & _spi_dev);

    virtual ~SPIDevice();

    /* AP_HAL::Device implementation */

    /* See AP_HAL::Device::set_speed() */
    bool set_speed(AP_HAL::Device::Speed speed) override;

    /* See AP_HAL::Device::transfer() */
    inline bool transfer(const uint8_t *send, uint32_t send_len,
                  uint8_t *recv, uint32_t recv_len) override;

    /* See AP_HAL::SPIDevice::transfer_fullduplex() */
    bool transfer_fullduplex(const uint8_t *send, uint8_t *recv,
                             uint32_t len) override;

    bool transfer_fullduplex(uint8_t *send_recv, uint32_t len) override;

    /* See AP_HAL::Device::get_semaphore() */
    AP_HAL::Semaphore *get_semaphore() override;

    /* See AP_HAL::Device::register_periodic_callback() */
    AP_HAL::Device::PeriodicHandle register_periodic_callback(
        uint32_t period_usec, AP_HAL::Device::PeriodicCb) override;

    bool adjust_periodic_callback(PeriodicHandle h, uint32_t period_usec) override;

private:
    SPIBus &_bus;
    infineon_spidev_def_t& dev_info;
    AP_HAL::Device::Speed _speed;
    constexpr static uint32_t max_spi_trans_len = 255;
};

class SPIDeviceManager : public AP_HAL::SPIDeviceManager {
public:
    SPIDeviceManager() { }
    AP_HAL::SPIDevice *get_device_ptr(const char *name) override;


//deal with spi bus initialize, this is a general initialization
    bool initialize();
};

}
