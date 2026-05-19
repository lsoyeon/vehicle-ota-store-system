/**********************************************************************************************************************
 * \file FlashOta.c
 * \brief Sensor ECU Flash OTA Core
 *********************************************************************************************************************/

#include "FlashOta.h"

#include "IfxCpu.h"
#include "IfxScuWdt.h"
#include "IfxFlash.h"
#include "IfxStm.h"

#include <string.h>

/* ============================================================
   Internal state
   ============================================================ */

static FlashOta_DebugInfo_t g_flashOtaDebug;

static volatile boolean g_jumpPending = FALSE;
static volatile uint8_t g_pendingResetType = 0U;

/* ============================================================
   Private prototypes
   ============================================================ */

static uint32_t readU32Le(const uint8_t *p);
static uint32_t calcSectorCount(uint32_t firmwareSize);
static uint32_t crc32Update(uint32_t crc, uint8_t data);
static uint32_t crc32FlashOriginalSize(void);
static void delayMs(uint32 ms);

#pragma section code "cpu0_psram"

static void pflashEraseSectorsPspr(uint32 sectorAddr, uint32 sectorCount);
static void pflashWritePagePspr(uint32 pageAddr, const uint32 *data);

#pragma section code restore

/* ============================================================
   Public API
   ============================================================ */

void FlashOta_Init(void)
{
    FlashOta_Reset();
}

void FlashOta_Reset(void)
{
    memset(&g_flashOtaDebug, 0, sizeof(g_flashOtaDebug));
    g_flashOtaDebug.verifyFailOffset = 0xFFFFFFFFU;

    g_jumpPending = FALSE;
    g_pendingResetType = 0U;
}

boolean FlashOta_BeginDownload(uint32_t targetAddress, uint32_t firmwareSize)
{
    uint32_t sectorCount;

    FlashOta_Reset();

    if (targetAddress != FLASH_OTA_APP_START_ADDR_C)
    {
        return FALSE;
    }

    if ((firmwareSize == 0U) || (firmwareSize > FLASH_OTA_MAX_IMAGE_SIZE))
    {
        return FALSE;
    }

    sectorCount = calcSectorCount(firmwareSize);

    g_flashOtaDebug.targetAddress = targetAddress;
    g_flashOtaDebug.firmwareSize = firmwareSize;
    g_flashOtaDebug.receivedBytes = 0U;
    g_flashOtaDebug.started = TRUE;

    /*
     * Flash command는 uncached 주소 사용.
     */
    pflashEraseSectorsPspr(FLASH_OTA_APP_START_ADDR_NC, sectorCount);

    g_flashOtaDebug.eraseCount = sectorCount;

    return TRUE;
}

boolean FlashOta_WriteBlock(uint16_t blockIndex,
                            const uint8_t *data,
                            uint16_t length)
{
    uint8_t page[FLASH_OTA_PAGE_SIZE];
    uint32 words[8];
    uint32_t targetAddrNc;
    uint32_t offset;
    uint32_t remaining;
    volatile const uint8 *readPtr;

    if (g_flashOtaDebug.started == FALSE)
    {
        return FALSE;
    }

    if ((data == NULL_PTR) || (length == 0U) || (length > FLASH_OTA_PAGE_SIZE))
    {
        return FALSE;
    }

    offset = ((uint32_t)blockIndex * FLASH_OTA_PAGE_SIZE);

    if (offset >= g_flashOtaDebug.firmwareSize)
    {
        return FALSE;
    }

    remaining = g_flashOtaDebug.firmwareSize - offset;

    if (length > remaining)
    {
        return FALSE;
    }

    /*
     * 마지막 block이 32바이트보다 작을 수 있으므로 나머지는 0xFF padding.
     */
    memset(page, 0xFF, sizeof(page));
    memcpy(page, data, length);

    targetAddrNc = FLASH_OTA_APP_START_ADDR_NC + offset;

    words[0] = readU32Le(&page[0]);
    words[1] = readU32Le(&page[4]);
    words[2] = readU32Le(&page[8]);
    words[3] = readU32Le(&page[12]);
    words[4] = readU32Le(&page[16]);
    words[5] = readU32Le(&page[20]);
    words[6] = readU32Le(&page[24]);
    words[7] = readU32Le(&page[28]);

    pflashWritePagePspr(targetAddrNc, words);

    /*
     * Read-back verify.
     */
    readPtr = (volatile const uint8 *)targetAddrNc;

    for (uint32 i = 0U; i < FLASH_OTA_PAGE_SIZE; i++)
    {
        if (readPtr[i] != page[i])
        {
            g_flashOtaDebug.verifyFailOffset = offset + i;
            g_flashOtaDebug.writeFailCount++;
            return FALSE;
        }
    }

    g_flashOtaDebug.receivedBytes += length;
    g_flashOtaDebug.lastBlockIndex = blockIndex;
    g_flashOtaDebug.lastWriteAddress = targetAddrNc;
    g_flashOtaDebug.writeOkCount++;

    return TRUE;
}

boolean FlashOta_EndTransfer(void)
{
    if (g_flashOtaDebug.started == FALSE)
    {
        return FALSE;
    }

    if (g_flashOtaDebug.receivedBytes != g_flashOtaDebug.firmwareSize)
    {
        return FALSE;
    }

    g_flashOtaDebug.transferExitDone = TRUE;

    return TRUE;
}

boolean FlashOta_CheckCrc32(uint32_t expectedCrc32,
                            uint32_t *calculatedCrc32)
{
    uint32_t crc;

    if (g_flashOtaDebug.transferExitDone == FALSE)
    {
        return FALSE;
    }

    crc = crc32FlashOriginalSize();

    g_flashOtaDebug.expectedCrc32 = expectedCrc32;
    g_flashOtaDebug.calculatedCrc32 = crc;

    if (calculatedCrc32 != NULL_PTR)
    {
        *calculatedCrc32 = crc;
    }

    if (crc == expectedCrc32)
    {
        g_flashOtaDebug.crcVerified = TRUE;
        return TRUE;
    }

    g_flashOtaDebug.crcVerified = FALSE;
    return FALSE;
}

boolean FlashOta_RequestJumpToApp(uint8_t resetType)
{
    if (g_flashOtaDebug.crcVerified == FALSE)
    {
        return FALSE;
    }

    g_pendingResetType = resetType;
    g_jumpPending = TRUE;

    return TRUE;
}

boolean FlashOta_IsJumpPending(void)
{
    return g_jumpPending;
}

void FlashOta_Service(void)
{
    typedef void (*AppEntryFunc)(void);

    AppEntryFunc appEntry;

    if (g_jumpPending == FALSE)
    {
        return;
    }

    /*
     * 0x601: 51 01 응답이 실제 CAN으로 나갈 시간을 조금 준다.
     * 이 동안 인터럽트는 켜져 있어야 TX complete가 처리될 수 있음.
     */
    delayMs(200U);

    g_jumpPending = FALSE;

    IfxCpu_disableInterrupts();

    appEntry = (AppEntryFunc)FLASH_OTA_APP_START_ADDR_C;
    appEntry();

    while (1)
    {
    }
}

void FlashOta_GetDebugInfo(FlashOta_DebugInfo_t *info)
{
    if (info != NULL_PTR)
    {
        memcpy(info, &g_flashOtaDebug, sizeof(FlashOta_DebugInfo_t));
    }
}

/* ============================================================
   Private functions
   ============================================================ */

static uint32_t readU32Le(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint32_t calcSectorCount(uint32_t firmwareSize)
{
    uint32_t count;

    count = firmwareSize / FLASH_OTA_SECTOR_SIZE_BYTES;

    if ((firmwareSize % FLASH_OTA_SECTOR_SIZE_BYTES) != 0U)
    {
        count++;
    }

    if (count == 0U)
    {
        count = 1U;
    }

    return count;
}

static uint32_t crc32Update(uint32_t crc, uint8_t data)
{
    crc ^= data;

    for (uint8 i = 0U; i < 8U; i++)
    {
        if ((crc & 1U) != 0U)
        {
            crc = (crc >> 1) ^ 0xEDB88320U;
        }
        else
        {
            crc = crc >> 1;
        }
    }

    return crc;
}

static uint32_t crc32FlashOriginalSize(void)
{
    volatile const uint8 *flashPtr = (volatile const uint8 *)FLASH_OTA_APP_START_ADDR_NC;
    uint32_t crc = 0xFFFFFFFFU;

    for (uint32_t i = 0U; i < g_flashOtaDebug.firmwareSize; i++)
    {
        crc = crc32Update(crc, flashPtr[i]);
    }

    return crc ^ 0xFFFFFFFFU;
}

static void delayMs(uint32 ms)
{
    Ifx_STM *stm = &MODULE_STM0;
    uint32 ticks = IfxStm_getTicksFromMilliseconds(stm, ms);

    IfxStm_waitTicks(stm, ticks);
}

/* ============================================================
   PSRAM functions
   ============================================================ */

#pragma section code "cpu0_psram"

static void pflashEraseSectorsPspr(uint32 sectorAddr, uint32 sectorCount)
{
    uint16 safetyWdtPassword;

    safetyWdtPassword = IfxScuWdt_getSafetyWatchdogPasswordInline();

    IfxScuWdt_clearSafetyEndinitInline(safetyWdtPassword);
    IfxFlash_eraseMultipleSectors(sectorAddr, sectorCount);
    IfxScuWdt_setSafetyEndinitInline(safetyWdtPassword);

    IfxFlash_waitUnbusy(0, IfxFlash_FlashType_P0);
}

static void pflashWritePagePspr(uint32 pageAddr, const uint32 *data)
{
    uint16 safetyWdtPassword;

    IfxFlash_enterPageMode(pageAddr);
    IfxFlash_waitUnbusy(0, IfxFlash_FlashType_P0);

    /*
     * PFLASH page = 32 bytes = 8 words
     */
    IfxFlash_loadPage2X32(pageAddr, data[0], data[1]);
    IfxFlash_loadPage2X32(pageAddr, data[2], data[3]);
    IfxFlash_loadPage2X32(pageAddr, data[4], data[5]);
    IfxFlash_loadPage2X32(pageAddr, data[6], data[7]);

    safetyWdtPassword = IfxScuWdt_getSafetyWatchdogPasswordInline();

    IfxScuWdt_clearSafetyEndinitInline(safetyWdtPassword);
    IfxFlash_writePage(pageAddr);
    IfxScuWdt_setSafetyEndinitInline(safetyWdtPassword);

    IfxFlash_waitUnbusy(0, IfxFlash_FlashType_P0);
}

#pragma section code restore
