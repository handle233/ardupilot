#pragma once

#include "AP_HAL_Infineon.h"
#include "infineon.h"
#include "DMARingBuffer.h"
class Infineon::UARTDriver : public AP_HAL::UARTDriver {
public:
    UARTDriver(infineon_uart_def_t & uart_info, cy_stc_dma_descriptor_t* descriptor_);
    /* Infineon implementations of UARTDriver virtual methods */
    bool is_initialized() override;
    bool tx_pending() override;

    /* Infineon implementations of Stream virtual methods */
    uint32_t txspace() override;


#if HAL_UART_STATS_ENABLED
    // request information on uart I/O for one uart
    void uart_info(ExpandingString &str, StatsTracker &stats, const uint32_t dt_ms) override;
#endif
    uint32_t bw_in_bytes_per_second() const override;

    uint8_t get_parity(void);
    void configure_parity(uint8_t v) override;
    void set_stop_bits(int n) override;

    void uart_dma_rx_isr();
    void uart_dma_tx_isr();
protected:
    void _begin(uint32_t b, uint16_t rxS, uint16_t txS) override;
    size_t _write(const uint8_t *buffer, size_t size) override;
    ssize_t _read(uint8_t *buffer, uint16_t size) override WARN_IF_UNUSED;
    void _end() override;
    void _flush() override;
    uint32_t _available() override;
    bool _discard_input() override;

private:
    infineon_uart_def_t def_uart_info;
    cy_stc_scb_uart_context_t uart_context;

    DMA_ring* rx_dma_ring;
    DMA_Channel* rx_dma_channel;
    DMA_RingBuffer* rx_dma_ring_buffer;

    cy_stc_dma_descriptor_t *tx_dma_descriptor;
    DMA_Channel* tx_dma_channel;
    DMA_RingBuffer* tx_dma_ring_buffer;
    volatile bool transfering;
    uint32_t tx_dma_len;

    bool _initialized = false;

    uint32_t baud;

#if HAL_UART_STATS_ENABLED
    uint32_t _tx_stats_bytes;
    uint32_t _rx_stats_bytes;
    uint32_t _rx_stats_dropped_bytes;
#endif
    const char *_name = "UART";

    void ReloadTxDMA();
};
