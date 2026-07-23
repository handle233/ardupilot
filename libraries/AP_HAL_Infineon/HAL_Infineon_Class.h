#pragma once

#include <AP_HAL/AP_HAL.h>

#include "AP_HAL_Infineon_Namespace.h"

class HAL_Infineon : public AP_HAL::HAL {
public:
    HAL_Infineon();
    void run(int argc, char* const* argv, Callbacks* callbacks) const override;
};
