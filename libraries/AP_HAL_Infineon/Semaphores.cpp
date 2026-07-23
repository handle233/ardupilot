#include "AP_HAL/AP_HAL.h"
#include "Semaphores.h"

using namespace Infineon;

Semaphore::Semaphore() : _taken(false) {
    _semaphore = xSemaphoreCreateRecursiveMutex();
    if(_semaphore == NULL) {
        AP_HAL::panic("Failed to create semaphore");
    }
    _owner = NULL;
    _take_count = 0;
}

bool Semaphore::give() {
    if(_owner != xTaskGetCurrentTaskHandle()) {
        return false;
    }
    if(xSemaphoreGiveRecursive(_semaphore) == pdTRUE) {
        if (_take_count > 0) {
            _take_count--;
        }
        if (_take_count == 0) {
            _taken = false;
            _owner = NULL;
        }
        return true;
    }
    return false;
}

bool Semaphore::take(uint32_t timeout_ms) {
    const TickType_t timeout_ticks = (timeout_ms == HAL_SEMAPHORE_BLOCK_FOREVER) ?
        portMAX_DELAY :
        pdMS_TO_TICKS(timeout_ms);
    if(xSemaphoreTakeRecursive(_semaphore, timeout_ticks) == pdTRUE) {
        _taken = true;
        _owner = xTaskGetCurrentTaskHandle();
        _take_count++;
        return true;
    }
    return false;
}

bool Semaphore::take_nonblocking() {
    /* No syncronisation primitives to garuntee this is correct */
    if(xSemaphoreTakeRecursive(_semaphore, 0) == pdTRUE) {
        _taken = true;
        _owner = xTaskGetCurrentTaskHandle();
        _take_count++;
        return true;
    }
    return false;
}

Semaphore::~Semaphore() {
    vSemaphoreDelete(_semaphore);
}

BinarySemaphore::BinarySemaphore(bool initial_state) {
    _Semaphore = xSemaphoreCreateBinary();
    if(_Semaphore == NULL) {
        AP_HAL::panic("Failed to create binary semaphore");
    }
    if(initial_state) {
        xSemaphoreGive(_Semaphore);
    }
}

BinarySemaphore::~BinarySemaphore() {
    vSemaphoreDelete(_Semaphore);
}

bool BinarySemaphore::wait(uint32_t timeout_us) {
    return xSemaphoreTake(_Semaphore, pdMS_TO_TICKS((timeout_us + 999) / 1000)) == pdTRUE;
}

bool BinarySemaphore::wait_blocking() {
    return xSemaphoreTake(_Semaphore, portMAX_DELAY) == pdTRUE;
}

bool BinarySemaphore::wait_nonblocking() {
    return xSemaphoreTake(_Semaphore, 0) == pdTRUE;
}

void BinarySemaphore::signal() {
    xSemaphoreGive(_Semaphore);
}

void BinarySemaphore::signal_ISR() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(_Semaphore, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
