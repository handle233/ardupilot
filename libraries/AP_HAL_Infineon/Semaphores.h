#pragma once

#include <AP_HAL/AP_HAL_Boards.h>
#include <AP_HAL/Semaphores.h>
#include "AP_HAL_Infineon_Namespace.h"

#include "infineon.h"


class Infineon::Semaphore : public AP_HAL::Semaphore {
public:
    Semaphore();
    bool give() override;
    bool take(uint32_t timeout_ms) override;
    bool take_nonblocking() override;
    bool check_owner() { return xTaskGetCurrentTaskHandle() == _owner; }
    ~Semaphore() override;
private:
    bool _taken;
    SemaphoreHandle_t _semaphore;
    TaskHandle_t _owner;
    uint32_t _take_count;
};

class Infineon::BinarySemaphore : public AP_HAL::BinarySemaphore {
public:
    /*
      create a binary semaphore. initial_state determines if a wait()
      immediately after creation would block. If initial_state is true
      then it won't block, if initial_state is false it will block
     */
    BinarySemaphore(bool initial_state=false);

    // do not allow copying
    CLASS_NO_COPY(BinarySemaphore);

    bool wait(uint32_t timeout_us) override ;
    bool wait_blocking() override;
    bool wait_nonblocking() override;

    void signal() override;
    void signal_ISR() override;
    
    ~BinarySemaphore(void);

private:
    QueueHandle_t _Semaphore;
};
