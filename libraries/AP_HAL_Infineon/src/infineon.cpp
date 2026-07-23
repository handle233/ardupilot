#include "infineon.h"

// a useful function to init the clock for a peripheral, such as SCB, TCPWM, etc.
cy_en_sysclk_status_t infineon_init_clock(
    en_clk_dst_t clk_dst,
    cy_en_divider_types_t div_num,
    uint32_t divider,
    uint32_t divirate
)
{
    cy_en_sysclk_status_t clk_status;
    /* 1) disable divider */
    clk_status = Cy_SysClk_PeriPclkDisableDivider(
        clk_dst,
        div_num,
        divider);
    CY_ASSERT(clk_status == CY_SYSCLK_SUCCESS);

    /* 2) set divider value */
    clk_status = Cy_SysClk_PeriPclkSetDivider(
        clk_dst,
        div_num,
        divider,
        divirate);
    CY_ASSERT(clk_status == CY_SYSCLK_SUCCESS);

    /* 3) assign the divider to peripheral */
    clk_status = Cy_SysClk_PeriPclkAssignDivider(
        clk_dst,
        div_num,
        divider);
    CY_ASSERT(clk_status == CY_SYSCLK_SUCCESS);

    /* 4) enable this divider */
    clk_status = Cy_SysClk_PeriPclkEnableDivider(
        clk_dst,
        div_num,
        divider);
    CY_ASSERT(clk_status == CY_SYSCLK_SUCCESS);

    return clk_status;
}