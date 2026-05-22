#ifndef APP_OTA_GATEWAY_H_
#define APP_OTA_GATEWAY_H_

/**********************************************************************************************************************
 * \file App_OtaGateway.h
 * \brief FreeRTOS wrapper for ZCU OTA Gateway
 *
 * 역할:
 *  - OtaGateway/UdsOtaClient를 FreeRTOS task에서 주기적으로 실행한다.
 *  - App_Can에 저장된 0x601 UDS Response를 꺼내 UdsOtaClient_OnResponse()로 전달한다.
 *  - 상위 Pi/HPC 또는 App 계층에서 받은 OTA_START / OTA_BLOCK 요청을 OtaGateway로 전달한다.
 *
 * 현재 단계:
 *  - Download/Verify phase 담당
 *  - ZCU는 전체 firmware binary를 저장하지 않는다.
 *  - OTA_START에서는 firmwareSize / firmwareCrc32만 받는다.
 *  - OTA_BLOCK에서는 현재 요청된 32-byte block만 받는다.
 *  - SOTA/UCB_SWAP activation은 아직 수행하지 않는다.
 *********************************************************************************************************************/

#include "Ifx_Types.h"
#include "FreeRTOS.h"

#include <stdint.h>

/* ============================================================
   Public API
   ============================================================ */

/**
 * @brief App_OtaGateway FreeRTOS task 시작
 *
 * 내부 동작:
 *  - OtaGateway_Init() 호출
 *  - command queue 생성
 *  - 1ms 주기 task 생성
 *  - task 안에서 0x601 응답 처리 + command 처리 + OtaGateway_MainFunction() 호출
 *
 * 주의:
 *  - AppCan_Start() 이후에 호출해야 한다.
 */
BaseType_t AppOtaGateway_Start(void);


/* ============================================================
   OTA command API
   ============================================================ */

/**
 * @brief OTA Download 시작 요청
 *
 * 상위 Pi/HPC/App 계층에서 firmware size와 CRC32를 알게 되면 호출한다.
 *
 * 내부 동작:
 *  - command queue에 START_DOWNLOAD 명령을 넣는다.
 *  - App_OtaGateway task 문맥에서 OtaGateway_Start()가 호출된다.
 *
 * @param firmwareSize  전체 firmware binary size
 * @param firmwareCrc32 전체 firmware CRC32
 * @param waitTicks     queue send 대기 tick
 *
 * @return pdPASS / pdFAIL
 */
BaseType_t AppOtaGateway_RequestDownload(uint32_t firmwareSize,
                                         uint32_t firmwareCrc32,
                                         TickType_t waitTicks);


/**
 * @brief 현재 요청된 firmware block 제공
 *
 * 상위 Pi/HPC/App 계층에서 block data를 받으면 호출한다.
 *
 * 호출 전 확인 권장:
 *  - AppOtaGateway_IsWaitingBlock() == TRUE
 *  - blockIndex == AppOtaGateway_GetRequestedBlockIndex()
 *  - length == AppOtaGateway_GetRequestedLength()
 *
 * 내부 동작:
 *  - command queue에 PROVIDE_BLOCK 명령을 넣는다.
 *  - App_OtaGateway task 문맥에서 OtaGateway_ProvideBlock()이 호출된다.
 *
 * @param blockIndex 제공할 block index
 * @param data       block data pointer
 * @param length     block length. 보통 32 bytes, 마지막 block은 32보다 작을 수 있음
 * @param waitTicks  queue send 대기 tick
 *
 * @return pdPASS / pdFAIL
 */
BaseType_t AppOtaGateway_ProvideBlock(uint32_t blockIndex,
                                      const uint8_t *data,
                                      uint8_t length,
                                      TickType_t waitTicks);


/**
 * @brief 진행 중인 OTA Download 취소
 *
 * @param waitTicks queue send 대기 tick
 *
 * @return pdPASS / pdFAIL
 */
BaseType_t AppOtaGateway_Cancel(TickType_t waitTicks);


/* ============================================================
   상태 확인 API
   ============================================================ */

boolean AppOtaGateway_IsBusy(void);
boolean AppOtaGateway_IsWaitingBlock(void);
boolean AppOtaGateway_IsDone(void);
boolean AppOtaGateway_IsError(void);


/* ============================================================
   요청 block 정보 확인 API
   ============================================================ */

uint32_t AppOtaGateway_GetRequestedBlockIndex(void);
uint32_t AppOtaGateway_GetRequestedOffset(void);
uint8_t  AppOtaGateway_GetRequestedLength(void);


/* ============================================================
   진행률 확인 API
   ============================================================ */

uint8_t AppOtaGateway_GetProgress(void);

#endif /* APP_OTA_GATEWAY_H_ */
