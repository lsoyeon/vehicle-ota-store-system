#ifndef APP_SENSOR_OTA_GATEWAY_UDS_H_
#define APP_SENSOR_OTA_GATEWAY_UDS_H_

/**********************************************************************************************************************
 * \file App_SensorOtaGateway_Uds.h
 * \brief Sensor ECU OTA Gateway UDS adapter for DoIP
 *
 * 역할:
 *  - Pi/HPC가 DoIP로 보낸 UDS payload를 해석한다.
 *  - ZCU 자기 Flash에 쓰지 않고, App_OtaReceiver를 통해 Sensor ECU OTA Gateway로 전달한다.
 *
 * 구조:
 *  Pi/HPC
 *      ↓ DoIP
 *  App_SensorOtaGateway_Doip
 *      ↓ UDS payload
 *  App_SensorOtaGateway_Uds
 *      ↓
 *  App_OtaReceiver
 *      ↓
 *  App_OtaGateway
 *      ↓ CAN FD 0x600 / 0x601
 *  Sensor ECU
 *
 * 지원 UDS 서비스:
 *  - 0x10 DiagnosticSessionControl
 *  - 0x34 RequestDownload
 *  - 0x36 TransferData
 *  - 0x37 RequestTransferExit + CRC32
 *
 * 주의:
 *  - 이 모듈은 ZCU Local OTA용이 아니다.
 *  - Sensor ECU OTA 중계용이다.
 *********************************************************************************************************************/

#include "Ifx_Types.h"

#include <stdint.h>

/* ============================================================
   UDS / Transfer config
   ============================================================ */

/*
 * HPC server.py는 0x34 positive response의 maxBlockSize를 보고
 * TransferData payload 크기를 결정한다.
 *
 * UDS TransferData 한 frame 구조:
 *   [0] SID  = 0x36
 *   [1] BSC  = block sequence counter
 *   [2..] Data
 *
 * Sensor ECU 쪽 내부 OTA는 32-byte block 단위이므로,
 * ZCU는 HPC에게 maxBlockSize = 34를 알려주는 것이 가장 단순하다.
 *
 * 그러면 HPC는:
 *   maxBlockSize - 2 = 32 bytes
 * 단위로 bin을 보낸다.
 */
#define APP_SENSOR_OTA_GATEWAY_UDS_DATA_SIZE          32U
#define APP_SENSOR_OTA_GATEWAY_UDS_MAX_BLOCK_SIZE     34U

#define APP_SENSOR_OTA_GATEWAY_UDS_NEGATIVE_RSP       0x7FU

/* ============================================================
   UDS Service IDs
   ============================================================ */

#define APP_SENSOR_OTA_GATEWAY_UDS_SID_SESSION_CONTROL     0x10U
#define APP_SENSOR_OTA_GATEWAY_UDS_SID_REQUEST_DOWNLOAD    0x34U
#define APP_SENSOR_OTA_GATEWAY_UDS_SID_TRANSFER_DATA       0x36U
#define APP_SENSOR_OTA_GATEWAY_UDS_SID_TRANSFER_EXIT       0x37U
#define APP_SENSOR_OTA_GATEWAY_UDS_SID_ECU_RESET 0x11U
#define APP_SENSOR_OTA_GATEWAY_UDS_RESET_HARD_RESET 0x01U
/* ============================================================
   Session types
   ============================================================ */

#define APP_SENSOR_OTA_GATEWAY_UDS_SESSION_DEFAULT         0x01U
#define APP_SENSOR_OTA_GATEWAY_UDS_SESSION_EXTENDED        0x03U

/* ============================================================
   Positive response
   ============================================================ */

#define APP_SENSOR_OTA_GATEWAY_UDS_POS_RESPONSE_OFFSET     0x40U

/* ============================================================
   Negative Response Codes
   ============================================================ */

#define APP_SENSOR_OTA_GATEWAY_UDS_NRC_GENERAL_REJECT              0x10U
#define APP_SENSOR_OTA_GATEWAY_UDS_NRC_SERVICE_NOT_SUPPORTED       0x11U
#define APP_SENSOR_OTA_GATEWAY_UDS_NRC_SUBFUNC_NOT_SUPPORTED       0x12U
#define APP_SENSOR_OTA_GATEWAY_UDS_NRC_CONDITIONS_NOT_CORRECT      0x22U
#define APP_SENSOR_OTA_GATEWAY_UDS_NRC_REQUEST_OUT_OF_RANGE        0x31U
#define APP_SENSOR_OTA_GATEWAY_UDS_NRC_WRONG_BLOCK_SEQ_COUNTER     0x73U
#define APP_SENSOR_OTA_GATEWAY_UDS_NRC_GENERAL_PROG_FAILURE        0x72U


#define APP_SENSOR_OTA_GATEWAY_UDS_SID_SPARSE_MANIFEST 0xB4U

#define APP_SENSOR_OTA_GATEWAY_UDS_SID_OTA_READY_CHECK    0xB5U
/* ============================================================
   Public API
   ============================================================ */

/**
 * @brief Sensor OTA Gateway UDS 상태 초기화
 *
 * 내부 download 상태, block sequence counter 등을 초기화한다.
 */
void AppSensorOtaGatewayUds_Init(void);

/**
 * @brief Sensor OTA Gateway UDS 주기 처리
 *
 * 현재는 필수 동작이 없을 수 있지만, 기존 팀원 UDS_Task() 구조와 맞추기 위해 유지한다.
 * 나중에 timeout 처리 등이 필요하면 이 함수에 추가한다.
 */
void AppSensorOtaGatewayUds_Task(void);

/**
 * @brief DoIP Diagnostic Message 안의 UDS payload 처리
 *
 * DoIP 수신부는 diagnostic payload만 꺼내서 이 함수에 전달한다.
 *
 * @param rxData  수신 UDS payload
 * @param rxLen   수신 UDS payload 길이
 * @param txData  응답 UDS payload buffer
 * @param txLen   응답 UDS payload 길이 반환
 */
void AppSensorOtaGatewayUds_HandleService(uint8  *rxData,
                                          uint16  rxLen,
                                          uint8  *txData,
                                          uint16 *txLen);

#endif /* APP_SENSOR_OTA_GATEWAY_UDS_H_ */
