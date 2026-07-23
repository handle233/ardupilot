
#include "UARTDriver.h"
#include <AP_Common/ExpandingString.h>
#include <stdint.h>

#include "Scheduler.h"
#include "stdio.h"

Infineon::UARTDriver::UARTDriver(infineon_uart_def_t & uart_info,cy_stc_dma_descriptor_t* descriptor_){
    def_uart_info = uart_info;
    tx_dma_descriptor = descriptor_;

    rx_dma_channel = nullptr;
    rx_dma_ring = nullptr;
    rx_dma_ring_buffer = nullptr;
    tx_dma_channel = nullptr;
    tx_dma_ring_buffer = nullptr;
}

/* Infineon implementations of virtual methods */
void Infineon::UARTDriver::_begin(uint32_t b, uint16_t rxS, uint16_t txS) {
    //re initialize?
    if(_initialized){
        if(b == 0)
        {
            return;
        }
        tx_dma_channel->disable();
        Cy_SCB_UART_Disable((CySCB_Type*)def_uart_info.scb,&uart_context);
        uint32_t dummy = PERIPHERAL_CLOCK/b/8;
        baud = b;
        infineon_init_clock(
            (en_clk_dst_t)def_uart_info.clk_dst,
            CY_SYSCLK_DIV_8_BIT,
            def_uart_info.divider,
        dummy);
        Cy_SCB_UART_Enable((CySCB_Type*)def_uart_info.scb);
        tx_dma_channel->enable();
        return ;
    }
    //init GPIO
     Cy_GPIO_Pin_FastInit(MAKE_PORT(def_uart_info.rx),
                        MAKE_NUM(def_uart_info.rx),
                        CY_GPIO_DM_HIGHZ,
                        1u,
                        (en_hsiom_sel_t)def_uart_info.rxhsiom);

    Cy_GPIO_Pin_FastInit(MAKE_PORT(def_uart_info.tx),
                        MAKE_NUM(def_uart_info.tx),
                        CY_GPIO_DM_STRONG_IN_OFF,
                        1u,
                        (en_hsiom_sel_t)def_uart_info.txhsiom);

    const cy_stc_scb_uart_config_t uart_config =
    {
        .uartMode                   = CY_SCB_UART_STANDARD,
        .oversample                 = 8UL,
        .dataWidth                  = 8UL,
        .enableMsbFirst             = false,
        .stopBits                   = CY_SCB_UART_STOP_BITS_1,
        .parity                     = CY_SCB_UART_PARITY_NONE,
        .enableInputFilter          = false,
        .dropOnParityError          = true,
        .dropOnFrameError           = true,
        .enableMutliProcessorMode   = false,
        .receiverAddress            = 0UL,
        .receiverAddressMask        = 0UL,
        .acceptAddrInFifo           = false,
        .irdaInvertRx               = false,
        .irdaEnableLowPowerReceiver = false,
        .smartCardRetryOnNack       = false,
        .enableCts                  = false,
        .ctsPolarity                = CY_SCB_UART_ACTIVE_LOW,
        .rtsRxFifoLevel             = 0UL,
        .rtsPolarity                = CY_SCB_UART_ACTIVE_LOW,
        .breakWidth                 = 11UL,
        .rxFifoTriggerLevel         = 0UL,
        .rxFifoIntEnableMask        = 0UL,
        .txFifoTriggerLevel         = 7UL,
        .txFifoIntEnableMask        = 0UL
    };
    
    //init uart peripheral
    cy_en_scb_uart_status_t status;
    status = Cy_SCB_UART_Init((CySCB_Type*)def_uart_info.scb, &uart_config, &uart_context);
    CY_ASSERT(status == CY_SCB_UART_SUCCESS);

    //assign clock
    if(b == 0)
    {
        b = def_uart_info.baudrate;
    }
        baud = b;
    uint32_t dummy = PERIPHERAL_CLOCK/b/8;
    infineon_init_clock(
        (en_clk_dst_t)def_uart_info.clk_dst,
        CY_SYSCLK_DIV_8_BIT,
        def_uart_info.divider,
    dummy);

    //Enable
    Cy_SCB_UART_Enable((CySCB_Type*)def_uart_info.scb);

    //bind DMA
    if(def_uart_info.dma != nullptr)
    {
        //bind rx DMA ring
        rx_dma_ring = new DMA_ring(def_uart_info.dma[0]->size, def_uart_info.dma[0]->block_size, 
            (void*)(&((CySCB_Type*)def_uart_info.scb)->RX_FIFO_RD), def_uart_info.dma[0]->buf);
        rx_dma_ring->init();

        rx_dma_channel = new DMA_Channel((DW_Type*)def_uart_info.dma[0]->dw, def_uart_info.dma[0]->channel,
         rx_dma_ring->get_descriptors(),
        (cy_israddress)def_uart_info.dma[0]->callback, 3,
         (en_trig_output_1to1_scb_dw1_tr_t)def_uart_info.dma[0]->trigger_dst, 
         def_uart_info.dma[0]->DMA_irq_num, NVIC_MUX_UART_DMA);
        rx_dma_channel->init();

        rx_dma_ring_buffer = new DMA_RingBuffer(def_uart_info.dma[0]->buf, def_uart_info.dma[0]->size);
        

        //bind tx DMA
        cy_stc_dma_descriptor_config_t desc_cfg =
        {
            .retrigger       = CY_DMA_RETRIG_IM,
            .interruptType   = CY_DMA_DESCR,
            .triggerOutType  = CY_DMA_1ELEMENT,
            .channelState    = CY_DMA_CHANNEL_ENABLED,
            .triggerInType   = CY_DMA_1ELEMENT,

            .dataSize        = CY_DMA_BYTE,
            .srcTransferSize = CY_DMA_TRANSFER_SIZE_DATA,
            .dstTransferSize = CY_DMA_TRANSFER_SIZE_WORD,

            .descriptorType  = CY_DMA_1D_TRANSFER,

            .srcAddress      = nullptr,
            .dstAddress      = (void*)&((CySCB_Type*)def_uart_info.scb)->TX_FIFO_WR,

            .srcXincrement   = 1,
            .dstXincrement   = 0,
            .xCount          = 1,

            .srcYincrement   = 0,
            .dstYincrement   = 0,
            .yCount          = 1,

            .nextDescriptor  = NULL,
        };

        Cy_DMA_Descriptor_Init(tx_dma_descriptor, &desc_cfg);

        tx_dma_channel = new DMA_Channel((DW_Type*)def_uart_info.dma[1]->dw, def_uart_info.dma[1]->channel,
         tx_dma_descriptor,
        (cy_israddress)def_uart_info.dma[1]->callback, 3,
         (en_trig_output_1to1_scb_dw1_tr_t)def_uart_info.dma[1]->trigger_dst, 
         def_uart_info.dma[1]->DMA_irq_num, NVIC_MUX_UART_DMA);
        tx_dma_channel->init();
        Cy_DMA_Channel_Disable((DW_Type*)def_uart_info.dma[1]->dw, def_uart_info.dma[1]->channel);

        tx_dma_ring_buffer = new DMA_RingBuffer(def_uart_info.dma[1]->buf, def_uart_info.dma[1]->size);
        transfering = false;
    }else{
        // no DMA, use FIFO only
        // remain to be done
    }

    _initialized = true;
}
void Infineon::UARTDriver::_end() {
    if(!_initialized){
        return;
    }
    _initialized = false;
    Cy_SCB_UART_Disable((CySCB_Type*)def_uart_info.scb,&uart_context);

    delete tx_dma_ring_buffer;
    tx_dma_ring_buffer = nullptr;
    tx_dma_channel->deinit();
    delete tx_dma_channel;
    tx_dma_channel = nullptr;
    Cy_DMA_Descriptor_DeInit(tx_dma_descriptor);

    delete rx_dma_ring_buffer;
    rx_dma_ring_buffer = nullptr;
    rx_dma_channel->deinit();
    delete rx_dma_channel;
    rx_dma_channel = nullptr;
    rx_dma_ring->deinit();
    delete rx_dma_ring;
    rx_dma_ring = nullptr;

    Cy_SCB_UART_DeInit((CySCB_Type*)def_uart_info.scb);
}
void Infineon::UARTDriver::_flush() {
    rx_dma_ring_buffer->movetail(rx_dma_channel->GetXIndex(), def_uart_info.dma[0]->block_size);
}
bool Infineon::UARTDriver::is_initialized() { return _initialized; }
bool Infineon::UARTDriver::tx_pending() 
{
    if(!tx_dma_ring_buffer->is_empty()){
        return true;
    }
    if(transfering){
        return true;
    }
    if(Cy_SCB_UART_GetNumInTxFifo((CySCB_Type*)def_uart_info.scb)>0){
        return true;
    }
    if(Cy_SCB_UART_IsTxComplete((CySCB_Type*)def_uart_info.scb)==false){
        return true;
    }
    return false;
}

uint32_t Infineon::UARTDriver::_available() 
{ 
    _flush();
    return rx_dma_ring_buffer->available(); 
}
uint32_t Infineon::UARTDriver::txspace() 
{ return tx_dma_ring_buffer->space(); }
bool Infineon::UARTDriver::_discard_input() 
{ 
    rx_dma_ring_buffer->clear();
    return rx_dma_ring_buffer->is_empty(); 
}
size_t Infineon::UARTDriver::_write(const uint8_t *buffer, size_t size)
{
    size_t total = 0;
    if(Cy_SCB_UART_GetNumInTxFifo((CySCB_Type*)def_uart_info.scb) < 64  && !transfering){
        //direct into FIFO
        const size_t fifo_written = Cy_SCB_UART_PutArray((CySCB_Type*)def_uart_info.scb, (void*)buffer, size);
        buffer += fifo_written;
        size -= fifo_written;
        total += fifo_written;
    }
    if(size == 0){
        _tx_stats_bytes += total;
        return total;
    }
    const size_t dma_written = tx_dma_ring_buffer->write(buffer,size);
    total += dma_written;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if(!transfering){
        ReloadTxDMA();
    }
    __set_PRIMASK(primask);
    _tx_stats_bytes += total;
    return total;

    // auto l = Cy_SCB_UART_PutArray((CySCB_Type*)def_uart_info.scb, (void*)buffer, size);
    // return l;
}
ssize_t Infineon::UARTDriver::_read(uint8_t *buffer, uint16_t size)
{
    return rx_dma_ring_buffer->read(buffer,size);
}

#if HAL_UART_STATS_ENABLED
void Infineon::UARTDriver::uart_info(ExpandingString &str, StatsTracker &stats, const uint32_t dt_ms)
{
    /*
    * update UART statistics for MAVFTP, remains to be done
    */
    const uint32_t tx_bytes = stats.tx.update(_tx_stats_bytes);
    const uint32_t rx_bytes = stats.rx.update(_rx_stats_bytes);

    const uint32_t tx_buffered = tx_dma_ring_buffer->available();
    const uint32_t rx_buffered = rx_dma_ring_buffer->available();

    str.printf("%-5s TX =%8lu RX =%8lu TXBD=%6lu RXBD=%6lu\n",
               _name,
               (unsigned long)tx_bytes,
               (unsigned long)rx_bytes,
               (unsigned long)tx_buffered,
               (unsigned long)rx_buffered);
}
#endif

uint32_t Infineon::UARTDriver::bw_in_bytes_per_second() const {
        return baud/10;
    }

uint8_t Infineon::UARTDriver::get_parity(void){
    return Cy_SCB_UART_GetParity((CySCB_Type*)def_uart_info.scb);
}

void Infineon::UARTDriver::configure_parity(uint8_t v){
    cy_en_scb_uart_parity_t parity_;
    switch(v){
        case 0:
        parity_ = CY_SCB_UART_PARITY_NONE;
        break;
        case 1:
        parity_ = CY_SCB_UART_PARITY_ODD;
        break;
        case 2:
        parity_ = CY_SCB_UART_PARITY_EVEN;
        break;
        default:
        return;
    }
    Cy_SCB_UART_Disable((CySCB_Type*)def_uart_info.scb, &uart_context);
    Cy_SCB_UART_SetParity((CySCB_Type*)def_uart_info.scb,parity_);

    Cy_SCB_UART_ClearRxFifo((CySCB_Type*)def_uart_info.scb);
    Cy_SCB_UART_ClearRxFifoStatus((CySCB_Type*)def_uart_info.scb,CY_SCB_UART_RX_OVERFLOW |
                                    CY_SCB_UART_RX_ERR_FRAME |
                                    CY_SCB_UART_RX_ERR_PARITY |
                                    CY_SCB_UART_RX_BREAK_DETECT);
    Cy_SCB_UART_Enable((CySCB_Type*)def_uart_info.scb);
}

void Infineon::UARTDriver::set_stop_bits(int n){
    cy_en_scb_uart_stop_bits_t stopbit;
    switch(n){
        case 2:
        stopbit = CY_SCB_UART_STOP_BITS_2;
        break;
        case 3:
        stopbit = CY_SCB_UART_STOP_BITS_3;
        break;
        case 4:
        stopbit = CY_SCB_UART_STOP_BITS_4;
        break;
        case 1:
        default:
        stopbit = CY_SCB_UART_STOP_BITS_1;
    }
    Cy_SCB_UART_Disable((CySCB_Type*)def_uart_info.scb, &uart_context);
    Cy_SCB_UART_SetStopBits((CySCB_Type*)def_uart_info.scb,stopbit);
    Cy_SCB_UART_ClearRxFifo((CySCB_Type*)def_uart_info.scb);
    Cy_SCB_UART_ClearRxFifoStatus((CySCB_Type*)def_uart_info.scb,CY_SCB_UART_RX_OVERFLOW |
                                    CY_SCB_UART_RX_ERR_FRAME |
                                    CY_SCB_UART_RX_ERR_PARITY |
                                    CY_SCB_UART_RX_BREAK_DETECT);
    Cy_SCB_UART_Enable((CySCB_Type*)def_uart_info.scb);
}

void Infineon::UARTDriver::uart_dma_rx_isr()
{
    uint32_t intr = Cy_DMA_Channel_GetInterruptStatusMasked((DW_Type*)def_uart_info.dma[0]->dw, def_uart_info.dma[0]->channel);
    cy_en_dma_intr_cause_t cause = Cy_DMA_Channel_GetStatus((DW_Type*)def_uart_info.dma[0]->dw, def_uart_info.dma[0]->channel);

    if (intr != 0u) {
        Cy_DMA_Channel_ClearInterrupt((DW_Type*)def_uart_info.dma[0]->dw, def_uart_info.dma[0]->channel);

        if (cause != CY_DMA_INTR_CAUSE_COMPLETION) {
            return;
        }

        if(rx_dma_ring_buffer->movetail(def_uart_info.dma[0]->block_size, def_uart_info.dma[0]->block_size)==true){
            _rx_stats_dropped_bytes += def_uart_info.dma[0]->block_size;
        }
        _rx_stats_bytes += def_uart_info.dma[0]->block_size;
    }
}

void Infineon::UARTDriver::uart_dma_tx_isr()
{
    uint32_t intr = Cy_DMA_Channel_GetInterruptStatusMasked((DW_Type*)def_uart_info.dma[1]->dw, def_uart_info.dma[1]->channel);

    if (intr == 0) {
        return;
    }
    if (!transfering) {
        Cy_DMA_Channel_ClearInterrupt((DW_Type*)def_uart_info.dma[1]->dw, def_uart_info.dma[1]->channel);
        return;
    }

    Cy_DMA_Channel_ClearInterrupt((DW_Type*)def_uart_info.dma[1]->dw, def_uart_info.dma[1]->channel);
    tx_dma_ring_buffer->movehead(tx_dma_len,1);
    tx_dma_len = 0;
    
    if(tx_dma_ring_buffer->available()>0){
        ReloadTxDMA();
    }else{
        Cy_DMA_Channel_Disable((DW_Type*)def_uart_info.dma[1]->dw, def_uart_info.dma[1]->channel);
        transfering = false;
    }
}

void Infineon::UARTDriver::ReloadTxDMA()
{
    Cy_DMA_Channel_ClearInterrupt((DW_Type*)def_uart_info.dma[1]->dw, def_uart_info.dma[1]->channel);
    Cy_DMA_Channel_Disable((DW_Type*)def_uart_info.dma[1]->dw, def_uart_info.dma[1]->channel);
    uint32_t len = tx_dma_ring_buffer->available();
    if(len==0){
        transfering = false;
        return;
    }
    if(len > def_uart_info.dma[1]->size - tx_dma_ring_buffer->gethead()){
        len = def_uart_info.dma[1]->size - tx_dma_ring_buffer->gethead();
    }

    len = len>256?256:len;

    Cy_DMA_Descriptor_SetSrcAddress(tx_dma_descriptor,
        tx_dma_ring_buffer->getbase()+ tx_dma_ring_buffer->gethead());
    
    Cy_DMA_Descriptor_SetXloopDataCount(tx_dma_descriptor,len);
    tx_dma_len = len;


    Cy_DMA_Channel_SetInterruptMask((DW_Type*)def_uart_info.dma[1]->dw, def_uart_info.dma[1]->channel, CY_DMA_INTR_MASK);
    Cy_DMA_Channel_SetDescriptor((DW_Type*)def_uart_info.dma[1]->dw, def_uart_info.dma[1]->channel, tx_dma_descriptor);
    //clean_dcache_for_dma(tx_dma_ring_buffer->getbase()+ tx_dma_ring_buffer->gethead(), len);
    //clean_dcache_for_dma(tx_dma_descriptor, sizeof(*tx_dma_descriptor));
    Cy_DMA_Channel_Enable((DW_Type*)def_uart_info.dma[1]->dw, def_uart_info.dma[1]->channel);

    transfering = true;
    //Cy_DMA_Channel_SetSWTrigger(DW1, 16);
}
