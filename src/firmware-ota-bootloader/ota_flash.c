#include "ota_flash.h"
#include "IfxCpu.h"
#include <string.h>

/*************************************************************/
/* 전역 변수                                                  */
/*************************************************************/
static OTA_FlashFunc g_func;
static boolean       g_funcCopied = FALSE;

/*************************************************************/
/* PSPR 래퍼 함수 (static — 외부 노출 불필요)                */
/*************************************************************/

/* Erase 래퍼 — PSPR에서 실행됨 */
static void OTA_EraseWrapper(uint32 sectorAddr, uint32 sectorCount)
{
    uint16 pw = IfxScuWdt_getSafetyWatchdogPasswordInline();

    IfxScuWdt_clearSafetyEndinitInline(pw);
    g_func.eraseSectors(sectorAddr, sectorCount);
    IfxScuWdt_setSafetyEndinitInline(pw);

    g_func.waitUnbusy(FLASH_MODULE, OTA_PFLASH_TYPE);  /* P1 busy 대기 */
}

/* Write 래퍼 — PSPR에서 실행됨, 32바이트 한 페이지 */
static void OTA_WritePageWrapper(uint32 pageAddr, uint8 *buf)
{
    uint16 pw = IfxScuWdt_getSafetyWatchdogPasswordInline();

    g_func.enterPageMode(pageAddr);
    g_func.waitUnbusy(FLASH_MODULE, OTA_PFLASH_TYPE);  /* P1 busy 대기 */

    /* 8바이트씩 4번 = 32바이트 로드 */
    g_func.load2X32bits(pageAddr, *((uint32*)(buf+0)),  *((uint32*)(buf+4)));
    g_func.load2X32bits(pageAddr, *((uint32*)(buf+8)),  *((uint32*)(buf+12)));
    g_func.load2X32bits(pageAddr, *((uint32*)(buf+16)), *((uint32*)(buf+20)));
    g_func.load2X32bits(pageAddr, *((uint32*)(buf+24)), *((uint32*)(buf+28)));

    IfxScuWdt_clearSafetyEndinitInline(pw);
    g_func.writePage(pageAddr);
    IfxScuWdt_setSafetyEndinitInline(pw);

    g_func.waitUnbusy(FLASH_MODULE, OTA_PFLASH_TYPE);  /* P1 busy 대기 */
}

/*************************************************************/
/* PSPR 복사 (static — 내부 사용)                            */
/*************************************************************/
static void OTA_CopyFuncsToPSPR(void)
{
    if (g_funcCopied) return;

    memcpy((void*)ERASESECTOR_ADDR,   (const void*)IfxFlash_eraseMultipleSectors, ERASESECTOR_LEN);
    memcpy((void*)WAITUNBUSY_ADDR,    (const void*)IfxFlash_waitUnbusy,           WAITUNBUSY_LEN);
    memcpy((void*)ENTERPAGEMODE_ADDR, (const void*)IfxFlash_enterPageMode,        ENTERPAGEMODE_LEN);
    memcpy((void*)LOAD2X32_ADDR,      (const void*)IfxFlash_loadPage2X32,         LOADPAGE2X32_LEN);
    memcpy((void*)WRITEPAGE_ADDR,     (const void*)IfxFlash_writePage,            WRITEPAGE_LEN);
    memcpy((void*)ERASEWRAPPER_ADDR,  (const void*)OTA_EraseWrapper,              ERASEWRAPPER_LEN);
    memcpy((void*)WRITEWRAPPER_ADDR,  (const void*)OTA_WritePageWrapper,          WRITEWRAPPER_LEN);

    g_func.eraseSectors  = (void*)ERASESECTOR_ADDR;
    g_func.waitUnbusy    = (void*)WAITUNBUSY_ADDR;
    g_func.enterPageMode = (void*)ENTERPAGEMODE_ADDR;
    g_func.load2X32bits  = (void*)LOAD2X32_ADDR;
    g_func.writePage     = (void*)WRITEPAGE_ADDR;
    g_func.eraseWrapper  = (void*)ERASEWRAPPER_ADDR;
    g_func.writePageFull = (void*)WRITEWRAPPER_ADDR;

    g_funcCopied = TRUE;
}

/*************************************************************/
/* 외부 인터페이스 구현                                       */
/*************************************************************/

void OTA_Flash_Erase(uint32 addr, uint32 size)
{
    /* PF1 Erase 전 CPU1/CPU2 정지 필수 */
    IfxCpu_setCoreMode(&MODULE_CPU1, IfxCpu_CoreMode_halt);
    IfxCpu_setCoreMode(&MODULE_CPU2, IfxCpu_CoreMode_halt);

    OTA_CopyFuncsToPSPR();

    uint32 flashAddr   = TO_FLASH_ADDR(addr);
    uint32 alignedAddr = flashAddr & ~(PFLASH_SECTOR_SIZE - 1);
    uint32 alignedEnd  = ((flashAddr + size) + PFLASH_SECTOR_SIZE - 1)
                         & ~(PFLASH_SECTOR_SIZE - 1);
    uint32 sectorCount = (alignedEnd - alignedAddr) / PFLASH_SECTOR_SIZE;

    boolean irq = IfxCpu_disableInterrupts();
    g_func.eraseWrapper(alignedAddr, sectorCount);
    IfxCpu_restoreInterrupts(irq);
}

boolean OTA_Flash_Write(uint32 addr, uint8 *data, uint16 len)
{
    OTA_CopyFuncsToPSPR();

    uint32 writeAddr = TO_FLASH_ADDR(addr);
    uint16 offset    = 0;
    uint8  pageBuf[PFLASH_PAGE_LEN];

    boolean irq = IfxCpu_disableInterrupts();

    while (offset < len)
    {
        memset(pageBuf, 0xFF, PFLASH_PAGE_LEN);
        uint16 copyLen = ((len - offset) > PFLASH_PAGE_LEN)
                         ? PFLASH_PAGE_LEN : (uint16)(len - offset);
        memcpy(pageBuf, data + offset, copyLen);

        g_func.writePageFull(writeAddr, pageBuf);

        writeAddr += PFLASH_PAGE_LEN;
        offset    += PFLASH_PAGE_LEN;
    }

    IfxCpu_restoreInterrupts(irq);
    return TRUE;
}

/* ── CRC32 검증 ─────────────────────────────────────────────── */
/* Flash에 기록된 데이터를 읽어서 CRC 계산                         */
boolean OTA_Flash_VerifyCRC(uint32 addr, uint32 size, uint32 expectedCRC)
{
    uint32  crc = 0xFFFFFFFF;
    uint8  *ptr = (uint8 *)addr;

    for (uint32 i = 0; i < size; i++)
    {
        crc ^= ptr[i];
        for (uint8 j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320UL & (uint32)(-(sint32)(crc & 1)));
    }
    crc ^= 0xFFFFFFFF;

    /*
     * TODO: RPi가 0x37 요청에 예상 CRC를 포함해서 보내도록 확장 시
     *       여기서 expected CRC와 비교.
     *       현재는 계산만 하고 항상 TRUE 반환 (개발 단계)
     */
    //return TRUE;
    return (crc == expectedCRC);  // 실제 비교
}

/* ── DFLASH OTA 플래그 기록 ─────────────────────────────────── */
/* Flash_Programming.c writeDataFlash() 와 동일한 패턴            */
void OTA_Flash_SetFlag(uint32 fwSize, uint32 expectedCRC)
{
    uint16 pw = IfxScuWdt_getSafetyWatchdogPassword();

    /* 1. Erase */
    IfxScuWdt_clearSafetyEndinit(pw);
    IfxFlash_eraseMultipleSectors(OTA_FLAG_ADDR, 1);
    IfxScuWdt_setSafetyEndinit(pw);
    IfxFlash_waitUnbusy(FLASH_MODULE, IfxFlash_FlashType_D0);

    /* 2. Write (DFLASH는 PSPR 불필요 — 예제와 동일) */
    /* 2. MAGIC 저장 */
    IfxFlash_enterPageMode(OTA_FLAG_ADDR);
    IfxFlash_waitUnbusy(FLASH_MODULE, IfxFlash_FlashType_D0);
    IfxFlash_loadPage2X32(OTA_FLAG_ADDR, OTA_FLAG_MAGIC, 0x00000000UL);
    IfxScuWdt_clearSafetyEndinit(pw);
    IfxFlash_writePage(OTA_FLAG_ADDR);
    IfxScuWdt_setSafetyEndinit(pw);
    IfxFlash_waitUnbusy(FLASH_MODULE, IfxFlash_FlashType_D0);

    /* 3. fw_size 저장 */
    IfxFlash_enterPageMode(OTA_FLAG_ADDR + 8);
    IfxFlash_waitUnbusy(FLASH_MODULE, IfxFlash_FlashType_D0);
    IfxFlash_loadPage2X32(OTA_FLAG_ADDR + 8, fwSize, 0x00000000UL);
    IfxScuWdt_clearSafetyEndinit(pw);
    IfxFlash_writePage(OTA_FLAG_ADDR + 8);
    IfxScuWdt_setSafetyEndinit(pw);
    IfxFlash_waitUnbusy(FLASH_MODULE, IfxFlash_FlashType_D0);

    /* 4. expectedCRC 저장 */
    IfxFlash_enterPageMode(OTA_FLAG_ADDR + 16);
    IfxFlash_waitUnbusy(FLASH_MODULE, IfxFlash_FlashType_D0);
    IfxFlash_loadPage2X32(OTA_FLAG_ADDR + 16, expectedCRC, 0x00000000UL);
    IfxScuWdt_clearSafetyEndinit(pw);
    IfxFlash_writePage(OTA_FLAG_ADDR + 16);
    IfxScuWdt_setSafetyEndinit(pw);
    IfxFlash_waitUnbusy(FLASH_MODULE, IfxFlash_FlashType_D0);
}

/* ── DFLASH OTA 플래그 클리어 ──────────────────────────────── */
/* Bootloader에서 플래그 확인 후 클리어할 때 사용                  */
void OTA_Flash_ClearFlag(void)
{
    uint16 pw = IfxScuWdt_getSafetyWatchdogPassword();

    IfxScuWdt_clearSafetyEndinit(pw);
    IfxFlash_eraseMultipleSectors(OTA_FLAG_ADDR, 1);
    IfxScuWdt_setSafetyEndinit(pw);
    IfxFlash_waitUnbusy(FLASH_MODULE, IfxFlash_FlashType_D0);
}

/*********************************************************************************************************************/
