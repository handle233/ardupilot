#pragma once
/*
* expose the PDL to HAL, only include this file to avoid conflict
*/
#if defined (CY_USING_HAL)
#include "cyhal.h"
#endif
#include "cybsp.h"
#include "cy_pdl.h"
#include "system_cat1c.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#undef GPIO
#undef FAULT
#undef TYPE

cy_en_sysclk_status_t infineon_init_clock(
    en_clk_dst_t clk_dst,
    cy_en_divider_types_t div_num,
    uint32_t divider,
    uint32_t divirate
);