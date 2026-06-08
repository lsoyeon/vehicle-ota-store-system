#ifndef UDS_OTA_CLIENT_H_
#define UDS_OTA_CLIENT_H_

/**********************************************************************************************************************
 * \file UdsOtaClient.h
 * \brief ZCU UDS-style OTA Client over CAN FD for FreeRTOS App_Can
 *
 * 역할:
 * - ZCU가 Sensor ECU로 UDS OTA Request를 송신한다.
 * - Sensor ECU의 UDS Response를 수신하여 상태머신을 진행한다.
 *
 * CAN:
 * - TX: 0x600 UDS Request  ZCU -> Sensor ECU
 * - RX: 0x601 UDS Response Sensor ECU -> ZCU
 *
 * 지원 모드:
 * 1. Legacy single stream
 *    - UdsOtaClient_StartStream(firmwareSize, crc32)
 *    - UdsOtaClient_StartStreamWithoutCrc(firmwareSize)
 *
 * 2. Sparse segment stream
 *    - UdsOtaClient_StartSparse(manifest)
 *    - segment마다 0x34 RequestDownload -> 0x36 TransferData 반복 -> 0x37 TransferExit 수행
 *    - 모든 segment 완료 후 0x31 RoutineControl CRC32 수행
 *********************************************************************************************************************/

#include "Ifx_Types.h"
#include <stdint.h>

/* ============================================================
   CAN / UDS fallback define
   ============================================================ */

#ifndef CAN_ID_OTA_REQUEST
#define CAN_ID_OTA_REQUEST                         0x600U
#endif

#ifndef CAN_ID_OTA_RESPONSE
#define CAN_ID_OTA_RESPONSE                        0x601U
#endif

#ifndef CANFD_MAX_DLC
#define CANFD_MAX_DLC                              64U
#endif

#ifndef UDS_TRANSFER_DATA_SIZE
#define UDS_TRANSFER_DATA_SIZE                     32U
#endif

#ifndef UDS_MAX_BLOCK_LENGTH
#define UDS_MAX_BLOCK_LENGTH                       32U
#endif

/*
 * Legacy single stream용 target address.
 *
 * Sparse OTA에서는 이 값을 사용하지 않고,
 * 0x34 RequestDownload address field에 segment offset을 넣는다.
 */
#ifndef UDS_APP_START_ADDR
#define UDS_APP_START_ADDR                         0x80320000U
#endif

#ifndef UDS_SID_DIAGNOSTIC_SESSION_CONTROL
#define UDS_SID_DIAGNOSTIC_SESSION_CONTROL         0x10U
#endif

#ifndef UDS_SID_ECU_RESET
#define UDS_SID_ECU_RESET                          0x11U
#endif

#ifndef UDS_SID_ROUTINE_CONTROL
#define UDS_SID_ROUTINE_CONTROL                    0x31U
#endif

#ifndef UDS_SID_REQUEST_DOWNLOAD
#define UDS_SID_REQUEST_DOWNLOAD                   0x34U
#endif

#ifndef UDS_SID_TRANSFER_DATA
#define UDS_SID_TRANSFER_DATA                      0x36U
#endif

#ifndef UDS_SID_REQUEST_TRANSFER_EXIT
#define UDS_SID_REQUEST_TRANSFER_EXIT              0x37U
#endif

#ifndef UDS_SID_NEGATIVE_RESPONSE
#define UDS_SID_NEGATIVE_RESPONSE                  0x7FU
#endif

#ifndef UDS_POSITIVE_RESPONSE_OFFSET
#define UDS_POSITIVE_RESPONSE_OFFSET               0x40U
#endif

#ifndef UDS_SESSION_PROGRAMMING
#define UDS_SESSION_PROGRAMMING                    0x02U
#endif

#ifndef UDS_DOWNLOAD_DATA_FORMAT_ID
#define UDS_DOWNLOAD_DATA_FORMAT_ID                0x00U
#endif

#ifndef UDS_DOWNLOAD_ADDR_LEN_FORMAT
#define UDS_DOWNLOAD_ADDR_LEN_FORMAT               0x44U
#endif

#ifndef UDS_ROUTINE_START
#define UDS_ROUTINE_START                          0x01U
#endif

#ifndef UDS_ROUTINE_ID_CHECK_CRC32
#define UDS_ROUTINE_ID_CHECK_CRC32                 0x0202U
#endif

#ifndef UDS_RESET_JUMP_TO_APP
#define UDS_RESET_JUMP_TO_APP                      0x01U
#endif

#ifndef UDS_REQ_LEN_DIAGNOSTIC_SESSION_CONTROL
#define UDS_REQ_LEN_DIAGNOSTIC_SESSION_CONTROL     2U
#endif

#ifndef UDS_REQ_LEN_REQUEST_DOWNLOAD
#define UDS_REQ_LEN_REQUEST_DOWNLOAD               11U
#endif

#ifndef UDS_REQ_LEN_TRANSFER_DATA_MIN
#define UDS_REQ_LEN_TRANSFER_DATA_MIN              3U
#endif

#ifndef UDS_REQ_LEN_REQUEST_TRANSFER_EXIT
#define UDS_REQ_LEN_REQUEST_TRANSFER_EXIT          1U
#endif

#ifndef UDS_REQ_LEN_ROUTINE_CONTROL_CHECK_CRC32
#define UDS_REQ_LEN_ROUTINE_CONTROL_CHECK_CRC32    8U
#endif

#ifndef UDS_REQ_LEN_ECU_RESET
#define UDS_REQ_LEN_ECU_RESET                      2U
#endif

/* ============================================================
   UDS OTA Client 설정
   ============================================================ */

#define UDS_OTA_CLIENT_CANFD_PAYLOAD_SIZE          CANFD_MAX_DLC
#define UDS_OTA_CLIENT_TRANSFER_DATA_SIZE          UDS_TRANSFER_DATA_SIZE
#define UDS_OTA_CLIENT_TARGET_APP_ADDR             UDS_APP_START_ADDR

/*
 * Timeout은 UdsOtaClient_MainFunction() 호출 주기에 맞춰 tick 단위로 사용한다.
 * 예: MainFunction이 1ms마다 호출되면 2000 = 2초.
 */
#define UDS_OTA_CLIENT_TIMEOUT_TICKS               2000U

/*
 * Sensor ECU는 0x34 RequestDownload에서 inactive slot erase를 수행할 수 있다.
 * PCAN 직접 테스트에서 5초는 부족했고, 60초에서 성공했다.
 */
#define UDS_OTA_CLIENT_REQUEST_DOWNLOAD_TIMEOUT_TICKS 60000U

#define UDS_OTA_CLIENT_TRANSFER_TIMEOUT_TICKS      50000U
#define UDS_OTA_CLIENT_CRC_TIMEOUT_TICKS           60000U

#define UDS_OTA_CLIENT_MAX_SEGMENTS                2U

/* ============================================================
   Sparse OTA manifest
   ============================================================ */

typedef struct
{
    uint32_t offset;
    uint32_t size;
} UdsOtaClient_SparseSegment_t;

typedef struct
{
    uint32_t virtualSize;
    uint32_t virtualCrc32;
    uint8_t  segmentCount;
    uint8_t  gapFill;
    UdsOtaClient_SparseSegment_t segments[UDS_OTA_CLIENT_MAX_SEGMENTS];
} UdsOtaClient_SparseManifest_t;

/* ============================================================
   OTA Client 상태
   ============================================================ */

typedef enum
{
    UDS_OTA_CLIENT_STATE_IDLE = 0,

    UDS_OTA_CLIENT_STATE_SEND_DIAGNOSTIC_SESSION,
    UDS_OTA_CLIENT_STATE_WAIT_DIAGNOSTIC_SESSION,

    UDS_OTA_CLIENT_STATE_SEND_REQUEST_DOWNLOAD,
    UDS_OTA_CLIENT_STATE_WAIT_REQUEST_DOWNLOAD,

    UDS_OTA_CLIENT_STATE_WAIT_STREAM_BLOCK,

    UDS_OTA_CLIENT_STATE_SEND_TRANSFER_DATA,
    UDS_OTA_CLIENT_STATE_WAIT_TRANSFER_DATA,

    /*
     * Late CRC mode 전용 상태.
     *
     * 모든 firmware block을 Sensor ECU로 전송한 뒤,
     * Pi/HPC가 0x37 단계에서 CRC32를 줄 때까지 여기서 대기한다.
     */
    UDS_OTA_CLIENT_STATE_WAIT_FINAL_CRC,

    UDS_OTA_CLIENT_STATE_SEND_REQUEST_TRANSFER_EXIT,
    UDS_OTA_CLIENT_STATE_WAIT_REQUEST_TRANSFER_EXIT,

    UDS_OTA_CLIENT_STATE_SEND_ROUTINE_CONTROL_CRC,
    UDS_OTA_CLIENT_STATE_WAIT_ROUTINE_CONTROL_CRC,

    UDS_OTA_CLIENT_STATE_SEND_ECU_RESET,
    UDS_OTA_CLIENT_STATE_WAIT_ECU_RESET,

    UDS_OTA_CLIENT_STATE_DONE,
    UDS_OTA_CLIENT_STATE_ERROR
} UdsOtaClient_State_t;

/* ============================================================
   OTA Client 결과
   ============================================================ */

typedef enum
{
    UDS_OTA_CLIENT_RESULT_OK = 0,
    UDS_OTA_CLIENT_RESULT_BUSY,
    UDS_OTA_CLIENT_RESULT_INVALID_PARAM,
    UDS_OTA_CLIENT_RESULT_CAN_TX_ERROR,
    UDS_OTA_CLIENT_RESULT_TIMEOUT,
    UDS_OTA_CLIENT_RESULT_NEGATIVE_RESPONSE,
    UDS_OTA_CLIENT_RESULT_UNEXPECTED_RESPONSE,
    UDS_OTA_CLIENT_RESULT_CRC_MISMATCH,
    UDS_OTA_CLIENT_RESULT_ERROR
} UdsOtaClient_Result_t;

/* ============================================================
   Debug 정보
   ============================================================ */

typedef struct
{
    UdsOtaClient_State_t state;
    UdsOtaClient_Result_t lastResult;

    uint32_t firmwareSize;
    uint32_t firmwareCrc32;
    uint32_t targetAddress;

    uint32_t totalBlocks;
    uint32_t currentBlockIndex;
    uint32_t currentOffset;
    uint32_t sentBytes;
    uint8_t currentBsc;

    uint8_t lastRxSid;
    uint8_t lastRxNrc;
    uint8_t lastExpectedSid;

    uint32_t requestCount;
    uint32_t responseCount;
    uint32_t negativeResponseCount;
    uint32_t timeoutCount;
    uint32_t canTxErrorCount;

    uint32_t tickCount;
    uint32_t stateEnterTick;
    uint32_t lastProgressPercent;
    uint32_t calculatedCrc32FromEcu;

    /*
     * Late CRC mode 확인용.
     * TRUE  : CRC32를 이미 알고 있음
     * FALSE : WAIT_FINAL_CRC에서 상위 계층의 CRC32 입력을 기다릴 수 있음
     */
    boolean finalCrcProvided;

    /*
     * Sparse OTA debug.
     *
     * sparseMode == TRUE이면:
     * - currentBlockIndex는 현재 segment 내부 block index
     * - currentPayloadBaseOffset은 segment1.bin + segment2.bin을 이어붙인 payload stream 기준 시작 offset
     * - GetRequestedBlockIndex()는 global block index를 반환한다.
     */
    boolean sparseMode;
    uint8_t segmentCount;
    uint8_t currentSegmentIndex;
    uint32_t currentSegmentOffset;
    uint32_t currentSegmentSize;
    uint32_t currentPayloadBaseOffset;

} UdsOtaClient_DebugInfo_t;

/* ============================================================
   Public API
   ============================================================ */

void UdsOtaClient_Init(void);
void UdsOtaClient_Reset(void);

/**
 * @brief OTA download 시작 - legacy streaming mode, CRC known
 */
UdsOtaClient_Result_t UdsOtaClient_StartStream(uint32_t firmwareSize, uint32_t crc32);

/**
 * @brief OTA download 시작 - legacy streaming mode, CRC later
 */
UdsOtaClient_Result_t UdsOtaClient_StartStreamWithoutCrc(uint32_t firmwareSize);

/**
 * @brief OTA download 시작 - sparse segment streaming mode
 *
 * 상위 계층은 manifest의 segments 순서대로 payload를 이어붙인 stream을 공급해야 한다.
 * 예:
 * - segment0.size = 40672
 * - segment1.size = 320
 * - GetRequestedOffset()은 0~40991 범위의 payload stream offset을 반환
 */
UdsOtaClient_Result_t UdsOtaClient_StartSparse(const UdsOtaClient_SparseManifest_t *manifest);

/**
 * @brief 마지막 0x37 단계에서 받은 CRC32 설정
 */
UdsOtaClient_Result_t UdsOtaClient_SetFinalCrc(uint32_t crc32);

/**
 * @brief 0x11 ECU Reset 요청
 */
UdsOtaClient_Result_t UdsOtaClient_RequestEcuReset(void);

void UdsOtaClient_MainFunction(void);

void UdsOtaClient_OnResponse(const uint8_t *data, uint8_t length);

UdsOtaClient_State_t UdsOtaClient_GetState(void);
UdsOtaClient_Result_t UdsOtaClient_GetLastResult(void);

boolean UdsOtaClient_IsBusy(void);
boolean UdsOtaClient_IsDone(void);
boolean UdsOtaClient_IsError(void);
boolean UdsOtaClient_IsWaitingStreamBlock(void);
boolean UdsOtaClient_IsWaitingFinalCrc(void);

uint32_t UdsOtaClient_GetRequestedBlockIndex(void);
uint32_t UdsOtaClient_GetRequestedOffset(void);
uint8_t UdsOtaClient_GetRequestedBlockLength(void);

UdsOtaClient_Result_t UdsOtaClient_ProvideStreamBlock(uint32_t blockIndex,
                                                      const uint8_t *data,
                                                      uint8_t length);

uint8_t UdsOtaClient_GetProgress(void);
void UdsOtaClient_GetDebugInfo(UdsOtaClient_DebugInfo_t *info);

#endif /* UDS_OTA_CLIENT_H_ */
