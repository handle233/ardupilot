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
#include <AP_HAL/I2CDevice.h>

#include "Semaphores.h"
#include "infineon.h"

namespace Infineon {

class I2CBus {
public:
    I2CBus(infineon_i2c_def_t &i2c_info);

    bool init();
    bool deinit();

    bool transfer(uint8_t address,
         const uint8_t *send, uint32_t send_len, uint8_t *recv, uint32_t recv_len, uint32_t timeout_ms=4);

    AP_HAL::Semaphore *get_semaphore() { return &bus_lock; }

    void i2c_isr();
    void i2c_event_isr(uint32_t event);
    void* register_periodic_process(uint32_t period_usec, AP_HAL::Device::PeriodicCb);
    struct PeriodicSlot {
        uint32_t period_usec;
        uint64_t next_usec;
        AP_HAL::Device::PeriodicCb cb;
    };
    uint8_t get_bus_id() const { return _i2c_info.id; }

    static void periodic_process(void* pthis);
    void set_speed(AP_HAL::Device::Speed speed);
private:

    bool _initialised;
    infineon_i2c_def_t &_i2c_info;
    TaskHandle_t task;

    cy_stc_scb_i2c_context_t i2cContext;
    Semaphore bus_lock;
    BinarySemaphore isr_signal;
    uint32_t _event;

    static constexpr uint8_t MAX_PERIODIC_CALLBACKS = 8;
    PeriodicSlot periodic_cb[MAX_PERIODIC_CALLBACKS];
    uint8_t periodic_cb_count = 0;
};

class I2CDevice : public AP_HAL::I2CDevice {
public:
    I2CDevice(I2CBus *bus, uint8_t address,
     AP_HAL::Device::Speed speed,
     bool use_smbus = false,
     uint32_t timeout_ms=4);

    virtual ~I2CDevice() { }
    /* See AP_HAL::I2CDevice::set_address() */
    void set_address(uint8_t address) override {
        _address = address;
        set_device_address(address);
    }
    /* See AP_HAL::I2CDevice::set_retries() */
    void set_retries(uint8_t retries) override {
        _retries = retries;
    }

    /* See AP_HAL::Device::transfer() */
    bool transfer(const uint8_t *send, uint32_t send_len, uint8_t *recv, uint32_t recv_len) override;
    bool read_registers_multiple(uint8_t first_reg, uint8_t *recv, 
        uint32_t recv_len, uint8_t times) override;
    /* See AP_HAL::Device::set_speed() */
    bool set_speed(AP_HAL::Device::Speed speed) override { 
        _speed = speed;
        return true; 
    }
    /* See AP_HAL::Device::get_semaphore() */
    AP_HAL::Semaphore *get_semaphore() override { return _bus.get_semaphore(); }
    /* See AP_HAL::Device::register_periodic_callback() */
    AP_HAL::Device::PeriodicHandle register_periodic_callback(
        uint32_t period_usec, AP_HAL::Device::PeriodicCb) override;

    /* See Device::adjust_periodic_callback() */
    virtual bool adjust_periodic_callback(
        AP_HAL::Device::PeriodicHandle h, uint32_t period_usec) override;

private:
    I2CBus &_bus;
    uint8_t _address;
    AP_HAL::Device::Speed _speed;
    bool _use_smbus;
    uint32_t _timeout_ms;
    uint8_t _retries;
};

class I2CDeviceManager : public AP_HAL::I2CDeviceManager {
public:
//the original work to DeviceManager
    I2CDeviceManager() { }

    /* AP_HAL::I2CDeviceManager implementation */
    AP_HAL::I2CDevice *get_device_ptr(uint8_t bus, uint8_t address,
                                      uint32_t bus_clock=400000,
                                      bool use_smbus = false,
                                      uint32_t timeout_ms=4) override;
    uint32_t get_bus_mask(void) const override;
    uint32_t get_bus_mask_external(void) const override;
    uint32_t get_bus_mask_internal(void) const override;

//manage device lists

//deal with i2c bus initialize, this is a general initialization
    bool initialize();
};

}
