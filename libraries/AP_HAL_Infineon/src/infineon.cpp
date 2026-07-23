#include "infineon.h"

cy_en_sysclk_status_t infineon_init_clock(
    en_clk_dst_t clk_dst,
    cy_en_divider_types_t div_num,
    uint32_t divider,
    uint32_t divirate
)
{
    //初始化时钟
    cy_en_sysclk_status_t clk_status;
    /* 1) 先把这个 divider 关掉 */
    clk_status = Cy_SysClk_PeriPclkDisableDivider(
        clk_dst,
        div_num,
        divider);
    CY_ASSERT(clk_status == CY_SYSCLK_SUCCESS);

    /* 2) 设置 divider 值 */
    clk_status = Cy_SysClk_PeriPclkSetDivider(
        clk_dst,
        div_num,
        divider,
        divirate);
    CY_ASSERT(clk_status == CY_SYSCLK_SUCCESS);

    /* 3) 把这个 divider 分配给 SCB1 */
    clk_status = Cy_SysClk_PeriPclkAssignDivider(
        clk_dst,
        div_num,
        divider);
    CY_ASSERT(clk_status == CY_SYSCLK_SUCCESS);

    /* 4) 使能这个 divider */
    clk_status = Cy_SysClk_PeriPclkEnableDivider(
        clk_dst,
        div_num,
        divider);
    CY_ASSERT(clk_status == CY_SYSCLK_SUCCESS);

    return clk_status;
}