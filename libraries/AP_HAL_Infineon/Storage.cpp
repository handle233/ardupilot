
#include <string.h>
#include "Storage.h"
#include "infineon.h"
#include "Scheduler.h"

using namespace Infineon;
extern const AP_HAL::HAL& hal;

CY_ALIGN(32) uint8_t Storage::_FlashBuffer[HAL_STORAGE_SIZE] = {0};
CY_ALIGN(32) uint32_t Storage::WriteBuffer[SECTOR_SIZE/4];
Storage::DirtyMask Storage::DirtyBits[HAL_STORAGE_SIZE/SECTOR_SIZE];

Infineon::Storage::Storage()
{
    ActiveHeaders.magic_head = ActiveHeaders.magic_tail = magic_byte;
    memset(ActiveHeaders.crc,0,sector_num*sizeof(uint32_t));
    memset(WriteBuffer,0xFF,sizeof(WriteBuffer));
}

void Infineon::Storage::init()
{
    Cy_Crypto_Core_Enable(CRYPTO);
    Cy_Flash_Init();
    Cy_Flashc_MainWriteDisable();
    Cy_Flashc_WorkWriteEnable();

    RecoverData();
    
    xTaskCreate(timer_tick_process,
        "Storage Process",
        INFENION_STORAGE_PROCESS_STACK_SIZE,
        this,
        RTOS_PRIORITY_STORAGE_PROCESS,
        &thread);
    _healthy = true;
    initialized = true;
}

bool Infineon::Storage::erase(){
    memset(_FlashBuffer,0xFF,HAL_STORAGE_SIZE);

    for(int i=0;i<sector_num;i++){
        DirtyBits[i] = Dirty;
    }
    return true;
}

void Infineon::Storage::read_block(void *dst, uint16_t src, size_t n)
{
    if(src+n > HAL_STORAGE_SIZE){
        return;
    }
    memcpy(dst,_FlashBuffer+src,n);
}

void Infineon::Storage::write_block(uint16_t dst, const void *src, size_t n)
{
    if(n==0){
        return ;
    }
    if(dst+n > HAL_STORAGE_SIZE){
        return;
    }

    if (memcmp(_FlashBuffer+dst, src, n) == 0) {
        //bus_lock.give();
        return;
    }

    bus_lock.take_blocking();

    memcpy(_FlashBuffer+dst,src,n);

    uint32_t dirty_end = (dst + n - 1)/SECTOR_SIZE;

    for(uint32_t i = dst/SECTOR_SIZE;i<=dirty_end;i++){
        DirtyBits[i] = Dirty;
    }

    bus_lock.give();

    vTaskPrioritySet(thread,RTOS_PRIORITY_STORAGE_BOOST);
}

void Infineon::Storage::_timer_tick(void)
{
    if(AP_HAL::micros64()-last_clean_timestamp > 1 * 1000 * 1000){
        _healthy = false;
    }else{
        _healthy = true;
    }
    for(int sector = 0;sector < sector_num;sector++){
        if(DirtyBits[sector]==Dirty){
            bus_lock.take_blocking();
            WriteData(sector);
            bus_lock.give();
            return;
        }
    }
    vTaskPrioritySet(thread,RTOS_PRIORITY_STORAGE_PROCESS);
    last_clean_timestamp = AP_HAL::micros64();
}

bool Infineon::Storage::healthy(void)
{
    for (uint8_t sector = 0; sector < sector_num; sector++) {
        if (DirtyBits[sector] == Dirty) {
            return false;
        }
    }
    return _healthy;
}

bool Infineon::Storage::get_storage_ptr(void *&ptr, size_t &size)
{
    ptr = nullptr;
    size = 0;
    return false;
}

void Infineon::Storage::timer_tick_process(void * data)
{
    Storage* storage = (Storage*)data;
    while(!storage->initialized){
        vTaskDelay(10);
    }
    storage->last_clean_timestamp = AP_HAL::micros64();
    while(1){
        storage->_timer_tick();
        vTaskDelay(1);
    }
}

void Infineon::Storage::RecoverData()
{
    Header A,B;
    ReadHead(A,B);
    if(A.magic_tail != A.magic_head||A.magic_head != magic_byte){
        memset(&A,0,sizeof(Header));
    }
    if(B.magic_tail != B.magic_head||B.magic_head != magic_byte){
        memset(&B,0,sizeof(Header));
    }
    for(int i=0;i<sector_num;i++){
        JudgeData(i,A,B,ActiveHeaders);
    }
}

void Infineon::Storage::ReadHead(Header &headA, Header &headB)
{
    memcpy(&headA,(void*)HEADER_A_BASE,sizeof(Header));
    memcpy(&headB,(void*)HEADER_B_BASE,sizeof(Header));
}

void Infineon::Storage::WriteHead(uint32_t headaddr)
{
    ClearWriteBuf();
    for(uint16_t ind=0;ind<SECTOR_SIZE*5;ind+=SECTOR_SIZE){
        memcpy(WriteBuffer,(uint8_t*)(&ActiveHeaders)+ind,SECTOR_SIZE);
        WriteSector(headaddr+ind);
    }
}

void Infineon::Storage::JudgeData(uint8_t sector, Header &A, Header &B, Header &Trust)
{
    uint32_t Acrc = 0,Bcrc = 0;
    CalculateCRC(&Acrc,(void*)(SECTOR_A_BASE+sector*SECTOR_SIZE),SECTOR_SIZE);
    CalculateCRC(&Bcrc,(void*)(SECTOR_B_BASE+sector*SECTOR_SIZE),SECTOR_SIZE);
    DirtyBits[sector] = Dirty;
    
    if(Bcrc == A.crc[sector]){
        //B记录匹配，写入第三步完成
        memcpy(_FlashBuffer+sector*SECTOR_SIZE,(void*)(SECTOR_B_BASE+sector*SECTOR_SIZE),SECTOR_SIZE);
        Trust.crc[sector] = A.crc[sector];
        DirtyBits[sector] = Clean;
    }else if(Acrc == A.crc[sector]){
        //A记录匹配，写入第二步完成
        memcpy(_FlashBuffer+sector*SECTOR_SIZE,(void*)(SECTOR_A_BASE+sector*SECTOR_SIZE),SECTOR_SIZE);
        Trust.crc[sector] = A.crc[sector];
        DEV_PRINTF("found err 2 in page%d\n",sector);
    }else if(Acrc == B.crc[sector]){
        //A记录匹配，只写入了第一步
        memcpy(_FlashBuffer+sector*SECTOR_SIZE,(void*)(SECTOR_A_BASE+sector*SECTOR_SIZE),SECTOR_SIZE);
        Trust.crc[sector] = B.crc[sector];
        DEV_PRINTF("found err 1 in page%d\n",sector);
    }else if(Bcrc == B.crc[sector]){
        //B记录匹配，不知道怎么回事
        memcpy(_FlashBuffer+sector*SECTOR_SIZE,(void*)(SECTOR_B_BASE+sector*SECTOR_SIZE),SECTOR_SIZE);
        Trust.crc[sector] = B.crc[sector];
        DEV_PRINTF("found err 0 in page%d\n",sector);
    }else{
        //丢数据
        DEV_PRINTF("found data losing in page%d\n",sector);
        memset(_FlashBuffer+sector*SECTOR_SIZE,0xFF,SECTOR_SIZE);
        CalculateCRC(&Trust.crc[sector],_FlashBuffer+sector*SECTOR_SIZE,SECTOR_SIZE);
    }
}

void Infineon::Storage::WriteData(uint8_t sector)
{
    //先行步骤，求解写入的crc
    CalculateCRC(&ActiveHeaders.crc[sector],_FlashBuffer + sector*SECTOR_SIZE,SECTOR_SIZE);
    //第一步，更新header A
    WriteHead(HEADER_A_BASE);
    //第二步，写入Asector
    memcpy(WriteBuffer,_FlashBuffer + sector*SECTOR_SIZE,SECTOR_SIZE);
    WriteSector(SECTOR_A_BASE + sector * SECTOR_SIZE);
    //第三步，写入Bsector
    WriteSector(SECTOR_B_BASE + sector * SECTOR_SIZE);
    //第四步，更新header B
    WriteHead(HEADER_B_BASE);
    DirtyBits[sector] = Clean;
}

void Infineon::Storage::WriteSector(uint32_t addr)
{
    cy_en_flashdrv_status_t ret;
    ret = Cy_Flash_EraseSector(addr);
    if(ret != CY_FLASH_DRV_SUCCESS){
        CY_ASSERT(0);
        return;
    }
    cy_stc_flash_programrow_config_t config = {
        .destAddr = (uint32_t*)(addr),
        .dataAddr = WriteBuffer,
        .blocking = CY_FLASH_PROGRAMROW_BLOCKING,
        .skipBC = CY_FLASH_PROGRAMROW_SKIP_BLANK_CHECK,
        .dataSize = CY_FLASH_PROGRAMROW_DATA_SIZE_1024BIT,
        .dataLoc = CY_FLASH_PROGRAMROW_DATA_LOCATION_SRAM,
        .intrMask = CY_FLASH_PROGRAMROW_NOT_SET_INTR_MASK
    };
    ret = Cy_Flash_Program_WorkFlash(&config);

    if(ret != CY_FLASH_DRV_SUCCESS){
        CY_ASSERT(0);
        return;
    }

    SCB_InvalidateDCache_by_Addr((void*)(addr),SECTOR_SIZE);
}

bool Infineon::Storage::CalculateCRC(uint32_t *crc, void *src, size_t n)
{
    Cy_Crypto_Core_Crc_CalcInit(
        CRYPTO,
        CRC32_WIDTH,
        CRC32_POLYNOMIAL,
        CRC32_DATA_REVERSE,
        CRC32_DATA_XOR,
        CRC32_REM_REVERSE,
        CRC32_REM_XOR,
        CRC32_LFSR_SEED
    );
    return CY_CRYPTO_SUCCESS == Cy_Crypto_Core_Crc_Calc(CRYPTO, CRC32_WIDTH, crc, src, n);
}
