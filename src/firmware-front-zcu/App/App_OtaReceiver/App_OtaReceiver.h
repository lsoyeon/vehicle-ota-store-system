#ifndef APP_OTA_RECEIVER_H_
#define APP_OTA_RECEIVER_H_

/**********************************************************************************************************************
 * \file App_OtaReceiver.h
 * \brief ZCU OTA input adapter layer
 *
 * 역할:
 *  - Pi/HPC, UART, SOME/IP 등 외부 입력 계층과 App_OtaGateway 사이의 얇은 중간 계층
 *  - 현재 단계에서는 실제 통신 파싱을 하지 않고, OTA_START / OTA_BLOCK API만 제공한다.
 *
 * 구조:
 *  External Input
 *      ↓
 *  App_OtaReceiver
 *      ↓
 *  App_OtaGateway
 *      ↓
 *  OtaGateway / UdsOtaClient
 *
 * 현재 단계:
 *  - Download/Verify phase만 담당
 *  - SOTA/UCB_SWAP activation은 아직 수행하지 않는다.
 *********************************************************************************************************************/

#include "Ifx_Types.h"
#include "FreeRTOS.h"

#include <stdint.h>

/* ============================================================
   Public API
   ============================================================ */

/**
 * @brief OTA Receiver 초기화
 *
 * 현재는 debug 변수 초기화만 수행한다.
 * App_OtaGateway_Start() 이후에 호출해도 되고,
 * App_OtaGateway_Start() 전에 호출해도 된다.
 */
void AppOtaReceiver_Init(void);

/**
 * @brief 외부 입력 계층에서 OTA_START를 받았을 때 호출
 *
 * @param firmwareSize  전체 firmware binary size
 * @param firmwareCrc32 전체 firmware CRC32
 * @param waitTicks     queue send 대기 tick
 *
 * @return pdPASS / pdFAIL
 */
BaseType_t AppOtaReceiver_StartDownload(uint32_t firmwareSize,
                                        uint32_t firmwareCrc32,
                                        TickType_t waitTicks);

/**
 * @brief 외부 입력 계층에서 OTA_BLOCK을 받았을 때 호출
 *
 * @param blockIndex 제공할 block index
 * @param data       block data pointer
 * @param length     block length. 보통 32 bytes, 마지막 block은 32보다 작을 수 있음
 * @param waitTicks  queue send 대기 tick
 *
 * @return pdPASS / pdFAIL
 */
BaseType_t AppOtaReceiver_ProvideBlock(uint32_t blockIndex,
                                       const uint8_t *data,
                                       uint8_t length,
                                       TickType_t waitTicks);

/**
 * @brief OTA 취소 요청
 *
 * @param waitTicks queue send 대기 tick
 *
 * @return pdPASS / pdFAIL
 */
BaseType_t AppOtaReceiver_Cancel(TickType_t waitTicks);

/* ============================================================
   Gateway state helper
   ============================================================ */

boolean AppOtaReceiver_IsBusy(void);
boolean AppOtaReceiver_IsWaitingBlock(void);
boolean AppOtaReceiver_IsDone(void);
boolean AppOtaReceiver_IsError(void);

uint32_t AppOtaReceiver_GetRequestedBlockIndex(void);
uint32_t AppOtaReceiver_GetRequestedOffset(void);
uint8_t  AppOtaReceiver_GetRequestedLength(void);
uint8_t  AppOtaReceiver_GetProgress(void);

#endif /* APP_OTA_RECEIVER_H_ */
