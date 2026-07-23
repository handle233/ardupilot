#include "infineon.h"
#include "AP_HAL_Infineon.h"

namespace Infineon{

class DMA_ring{
public:
    DMA_ring(uint32_t buffer_size, uint32_t block_size, void* src, void *dst);
    ~DMA_ring();

    int init();
    void deinit();

    cy_stc_dma_descriptor_t* get_descriptors();

private:
    cy_stc_dma_descriptor_t *descriptors;

    uint32_t _buffer_size, _block_size;
    void* _src;
    void * _dst;
};

class DMA_Channel{
public:
    DMA_Channel(DW_Type* dw, uint32_t channel_num,cy_stc_dma_descriptor_t *descriptors,
     cy_israddress callback, uint32_t priority,
     en_trig_output_1to1_scb_dw1_tr_t trigger,uint32_t intr_src,uint32_t mux);
    ~DMA_Channel();

    int init();
    int deinit();

    void disable();
    void enable();

    uint32_t GetXIndex();

private:
    DW_Type* _dw;
    uint32_t _channel_num,_priority,_intr_src,_mux;
    cy_stc_dma_descriptor_t *_descriptors;
    cy_israddress _callback;
    en_trig_output_1to1_scb_dw1_tr_t _trigger;
};

class DMA_RingBuffer{
public:
    DMA_RingBuffer(void* external_buffer,uint32_t buffer_size);

    uint32_t available(void);
    void clear(void);
    uint32_t space(void);
    bool is_empty(void);
    uint32_t write(const uint8_t *data, uint32_t len);
    uint32_t read(uint8_t *data, uint32_t len);
    bool movetail(uint32_t offset,uint32_t align);
    void movehead(uint32_t offset,uint32_t align);

    uint32_t gethead();
    uint32_t gettail();
    uint8_t *getbase();

private:
    uint8_t *buf;
    uint32_t size;
    volatile uint32_t head = 0;
    volatile uint32_t tail = 0;

    volatile uint32_t irq;

    inline void offirq();
    inline void onirq();
};

};
