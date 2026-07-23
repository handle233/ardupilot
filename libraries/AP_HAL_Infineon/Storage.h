#pragma once

#include <AP_HAL/AP_HAL.h>
#include "AP_HAL_Infineon.h"

#define SECTOR_SIZE (128)
//CY_FLASH_SIZEOF_ROW = 512

#define HAL_STORAGE_BASE (uint32_t)0x14030000

//CRC Settings
#define CRC32_WIDTH        (32u)
#define CRC32_POLYNOMIAL   (0x04C11DB7u)
#define CRC32_LFSR_SEED    (0xFFFFFFFFu)
#define CRC32_DATA_REVERSE (1u)
#define CRC32_DATA_XOR     (0u)
#define CRC32_REM_REVERSE  (1u)
#define CRC32_REM_XOR      (0xFFFFFFFFu)

class Infineon::Storage : public AP_HAL::Storage {
public:
    Storage();
    void init() override;
    bool erase() override;
    void read_block(void *dst, uint16_t src, size_t n) override;
    void write_block(uint16_t dst, const void* src, size_t n) override;
    void _timer_tick(void) override;
    bool healthy(void) override;
    bool get_storage_ptr(void *&ptr, size_t &size) override;

private:
    bool _healthy = false;
    uint64_t last_clean_timestamp;
    bool initialized = false;
    TaskHandle_t thread;
    Semaphore bus_lock;

    static void timer_tick_process(void*);

    struct Header{
        uint32_t magic_head;
        uint32_t crc[HAL_STORAGE_SIZE/SECTOR_SIZE];
        uint32_t magic_tail;
    }ActiveHeaders;

    static CY_ALIGN(32) uint8_t _FlashBuffer[HAL_STORAGE_SIZE];
    static CY_ALIGN(32) uint32_t WriteBuffer[SECTOR_SIZE/4];
    enum DirtyMask{
        Clean = 0,
        Dirty
    };
    static DirtyMask DirtyBits[HAL_STORAGE_SIZE/SECTOR_SIZE];

    constexpr static uint32_t HEADER_A_BASE = HAL_STORAGE_BASE;
    constexpr static uint32_t HEADER_B_BASE = HEADER_A_BASE + SECTOR_SIZE*5;
    constexpr static uint32_t SECTOR_A_BASE = HEADER_B_BASE + SECTOR_SIZE*5;
    constexpr static uint32_t SECTOR_B_BASE = SECTOR_A_BASE + HAL_STORAGE_SIZE;

    constexpr static uint32_t magic_byte = 0xA5A5A5A5;
    constexpr static uint8_t sector_num = HAL_STORAGE_SIZE/SECTOR_SIZE;

    /*
    * 读取文件头，仲裁恢复完整数据块到FlashBuffer
    */
    void RecoverData();
    /*
    * 复制Head到结构体里面
    */
    void ReadHead(Header &headA,Header &headB);
    void WriteHead(uint32_t headaddr);
    /*
    * 仲裁数据块，刷新块的DirtyBit，并写入数据到FlashBuffer（如果可用）
    * 传入的是块index，不是地址
    */
    void JudgeData(uint8_t sector, Header &A, Header &B, Header &Trust);
    /*
    * 使用四步写入法正确写入
    */
    void WriteData(uint8_t sector);

//基础函数
    void WriteSector(uint32_t addr);
    void ClearWriteBuf(){ memset(WriteBuffer,0xFF,sizeof(WriteBuffer));}
    bool CalculateCRC(uint32_t *crc, void *src, size_t n);
};
