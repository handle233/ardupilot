#include "DMARingBuffer.h"
#include "Scheduler.h"

extern const AP_HAL::HAL& hal;

Infineon::DMA_ring::DMA_ring(uint32_t buffer_size, uint32_t block_size, void* src, void *dst)
    : _buffer_size(buffer_size), _block_size(block_size), _src(src), _dst(dst)
{
    descriptors = nullptr;
}

Infineon::DMA_ring::~DMA_ring(){
    if(descriptors){   
        //delete[] descriptors;
        //descriptors = nullptr;
    }
}

int Infineon::DMA_ring::init(){
    cy_en_dma_status_t dma_ret = CY_DMA_SUCCESS;
    uint32_t block_count = _buffer_size / _block_size;
    descriptors = new cy_stc_dma_descriptor_t[block_count];

    for(uint32_t i = 0; i < block_count; i++){
        cy_stc_dma_descriptor_config_t desc_cfg =
        {
            .retrigger       = CY_DMA_RETRIG_IM,
            .interruptType   = CY_DMA_DESCR,
            .triggerOutType  = CY_DMA_DESCR_CHAIN,
            .channelState    = CY_DMA_CHANNEL_ENABLED,
            .triggerInType   = CY_DMA_1ELEMENT,

            .dataSize        = CY_DMA_BYTE,
            .srcTransferSize = CY_DMA_TRANSFER_SIZE_WORD,
            .dstTransferSize = CY_DMA_TRANSFER_SIZE_DATA,

            .descriptorType  = CY_DMA_1D_TRANSFER,

            .srcAddress      = _src,
            .dstAddress      = (void*)((uint8_t*)_dst + i * _block_size),

            .srcXincrement   = 0,
            .dstXincrement   = 1,

            .xCount          = _block_size,

            .srcYincrement   = 0,
            .dstYincrement   = 0,
            .yCount          = 1,

            .nextDescriptor  = descriptors + (i + 1u) % block_count,
        };

        dma_ret = Cy_DMA_Descriptor_Init(&descriptors[i], &desc_cfg);
        CY_ASSERT(dma_ret == CY_DMA_SUCCESS);
    }

    SCB_CleanDCache_by_Addr(descriptors,sizeof(cy_stc_dma_descriptor_t)*block_count);

    return (int)dma_ret;
}

void Infineon::DMA_ring::deinit(){
    Cy_DMA_Descriptor_DeInit(descriptors);
    delete[] descriptors;
}

cy_stc_dma_descriptor_t* Infineon::DMA_ring::get_descriptors(){
    return descriptors;
}


Infineon::DMA_Channel::DMA_Channel(
     DW_Type* dw, uint32_t channel_num,cy_stc_dma_descriptor_t *descriptors,
     cy_israddress callback, uint32_t priority,
     en_trig_output_1to1_scb_dw1_tr_t trigger,uint32_t intr_src,uint32_t mux)
        : _dw(dw), _channel_num(channel_num),
        _priority(priority), _intr_src(intr_src), _mux(mux),
         _descriptors(descriptors), _callback(callback), _trigger(trigger)
{

}

Infineon::DMA_Channel::~DMA_Channel(){

}

int Infineon::DMA_Channel::init(){
    cy_en_dma_status_t dma_ret;
    cy_stc_dma_channel_config_t ch_cfg =
    {
        .descriptor  = &_descriptors[0],
        .preemptable = false,
        .priority    = BSP_PRIORITY_DMA,
        .enable      = false,
        .bufferable  = false,
    };

    dma_ret = Cy_DMA_Channel_Init(_dw, _channel_num, &ch_cfg);
    CY_ASSERT(dma_ret == CY_DMA_SUCCESS);

    /*
     * 开 DMA channel 中断。
     */
    Cy_DMA_Channel_SetInterruptMask(_dw, _channel_num, CY_DMA_INTR_MASK);

    cy_stc_sysint_t dma_irq_cfg =
    {
        .intrSrc      = (((uint32_t)_mux << CY_SYSINT_INTRSRC_MUXIRQ_SHIFT) |
                                 (uint32_t)_intr_src),
        .intrPriority = _priority,
    };

    cy_en_sysint_status_t int_ret = Cy_SysInt_Init(&dma_irq_cfg, _callback);
    CY_ASSERT(int_ret == CY_SYSINT_SUCCESS);
    NVIC_EnableIRQ((IRQn_Type)_mux);

    /*
     * 连接 UART trigger 到 DMA channel trigger input。
     */
    cy_en_trigmux_status_t trig_ret = Cy_TrigMux_Select(
        _trigger,
        false,
        TRIGGER_TYPE_LEVEL
    );
    CY_ASSERT(trig_ret == CY_TRIGMUX_SUCCESS);

    Cy_DMA_Channel_Enable(_dw, _channel_num);
    Cy_DMA_Enable(_dw);

    return dma_ret;
}

int Infineon::DMA_Channel::deinit(){
    Cy_DMA_Disable(_dw);
    Cy_DMA_Channel_Disable(_dw, _channel_num);
    Cy_TrigMux_Deselect(_trigger);
    Cy_DMA_Channel_SetInterruptMask(_dw, _channel_num, 0);
    Cy_DMA_Channel_DeInit(_dw, _channel_num);
    return 0;
}

void Infineon::DMA_Channel::disable()
{
    Cy_DMA_Channel_SetInterruptMask(_dw, _channel_num, 0);
}

void Infineon::DMA_Channel::enable()
{
    Cy_DMA_Channel_SetInterruptMask(_dw, _channel_num, CY_DMA_INTR_MASK);
}

uint32_t Infineon::DMA_Channel::GetXIndex()
{
    return ((DW_CH_IDX(_dw,_channel_num) & DW_CH_STRUCT_CH_IDX_X_IDX_Msk) >> DW_CH_STRUCT_CH_IDX_X_IDX_Pos);
}


Infineon::DMA_RingBuffer::DMA_RingBuffer(void *external_buffer, uint32_t buffer_size)
{
    buf = (uint8_t*)external_buffer;
    size = buffer_size;
    irq = 0xFFFFFFFF;
}

uint32_t Infineon::DMA_RingBuffer::available(void)
{
    offirq();
    uint32_t available = tail - head;
    onirq();
    return available;
}

void Infineon::DMA_RingBuffer::clear(void)
{
    offirq();

    head = tail = 0;

    onirq();
}

uint32_t Infineon::DMA_RingBuffer::space(void)
{
    offirq();
    uint32_t space = size - (tail - head);
    onirq();
    return space;
}

bool Infineon::DMA_RingBuffer::is_empty(void)
{
    offirq();
    bool ans = head == tail;
    onirq();
    return ans;
}

uint32_t Infineon::DMA_RingBuffer::write(const uint8_t *data, uint32_t len)
{
    offirq();
    uint32_t space = size - (tail - head);
    uint32_t l = len>space ? space : len;
    onirq();

    if(l > (size - tail % size)){
        memcpy(buf + (tail % size), data, (size - tail % size));
        memcpy(buf, data + (size - tail % size), l - (size - tail % size));
    }else{
        memcpy(buf + (tail % size), data, l);
    }
    

    offirq();
    tail += l;
    onirq();
    
    return l;
}

uint32_t Infineon::DMA_RingBuffer::read(uint8_t *data, uint32_t len)
{
    uint32_t l = len>available() ? available() : len;

    if(l > (size - head % size)){
        memcpy(data, buf + (head % size), (size - head % size));
        memcpy(data + (size - head % size), buf, l - (size - head % size));
    }else{
        memcpy(data, buf + (head % size), l);
    }

    offirq();
    head += l;
    onirq();

    return l;
}

bool Infineon::DMA_RingBuffer::movetail(uint32_t offset, uint32_t align)
{
    offirq();
    tail += offset - tail % align;

    if(tail -head > size){
        DEV_PRINTF("UART overflow!\n");
        head = tail;
        onirq();
        return true;
    }
    onirq();

    return false;
}

void Infineon::DMA_RingBuffer::movehead(uint32_t offset, uint32_t align)
{
    offirq();

    head += offset - head % align;

    if(head > tail){
        CY_ASSERT(0);
    }

    onirq();
}

uint32_t Infineon::DMA_RingBuffer::gethead()
{
    return (head % size);
}

uint32_t Infineon::DMA_RingBuffer::gettail()
{
    return (tail % size);
}

uint8_t *Infineon::DMA_RingBuffer::getbase()
{
    return buf;
}

void Infineon::DMA_RingBuffer::offirq()
{
    if(irq != 0xFFFFFFFF){
        return;
    }
    irq = __get_PRIMASK();
    __disable_irq();
}

void Infineon::DMA_RingBuffer::onirq()
{
    if(irq == 0xFFFFFFFF){
        return;
    }
    __set_PRIMASK(irq);
    irq = 0xFFFFFFFF;
}
