#ifndef FLASH_OTA_H_
#define FLASH_OTA_H_

/**********************************************************************************************************************
 * \file FlashOta.h
 * \brief Sensor ECU Flash OTA Core
 *
 * 역할:
 *  - Application 영역 erase
 *  - 32-byte PFLASH page write
 *  - read-back verify
 *  - CRC32 계산/검증
 *  - Application jump
 *
 * UdsOta.c는 UDS 프로토콜을 처리하고,
 * 실제 Flash 동작은 이 FlashOta 모듈을 호출한다.
 *********************************************************************************************************************/

#include "Ifx_Types.h"
#include <stdint.h>

/* Application 영역 */
#define FLASH_OTA_APP_START_ADDR_C       0x80040000U
#define FLASH_OTA_APP_START_ADDR_NC      0xA0040000U

/* LED_Blink OTA 성공 기준 */
#define FLASH_OTA_PAGE_SIZE              32U
#define FLASH_OTA_SECTOR_SIZE_BYTES      0x2000U   /* 실험 성공 기준: 8KB 단위 */
#define FLASH_OTA_MAX_IMAGE_SIZE         0x30000U  /* 필요 시 실제 App 크기에 맞게 조정 */

/* Debug status */
typedef struct
{
    boolean started;
    boolean transferExitDone;
    boolean crcVerified;

    uint32_t targetAddress;
    uint32_t firmwareSize;
    uint32_t receivedBytes;

    uint32_t expectedCrc32;
    uint32_t calculatedCrc32;

    uint16_t lastBlockIndex;
    uint32_t lastWriteAddress;
    uint32_t verifyFailOffset;

    uint32_t eraseCount;
    uint32_t writeOkCount;
    uint32_t writeFailCount;
} FlashOta_DebugInfo_t;

void FlashOta_Init(void);
void FlashOta_Reset(void);

boolean FlashOta_BeginDownload(uint32_t targetAddress, uint32_t firmwareSize);

boolean FlashOta_WriteBlock(uint16_t blockIndex,
                            const uint8_t *data,
                            uint16_t length);

boolean FlashOta_EndTransfer(void);

boolean FlashOta_CheckCrc32(uint32_t expectedCrc32,
                            uint32_t *calculatedCrc32);

boolean FlashOta_RequestJumpToApp(uint8_t resetType);

void FlashOta_GetDebugInfo(FlashOta_DebugInfo_t *info);

boolean FlashOta_IsJumpPending(void);
void FlashOta_Service(void);

#endif /* FLASH_OTA_H_ */
