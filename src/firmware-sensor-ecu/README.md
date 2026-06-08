# Sensor ECU

## 1. 프로젝트 개요

Sensor ECU는 차량의 센서 데이터를 수집하고, 이를 ZCU(Zone Control Unit)로 전달하는 ECU입니다.
본 프로젝트에서는 TC375 기반 Sensor ECU를 구성하여 TOF 센서, Hall Sensor 등의 데이터를 처리하고, CAN/CAN FD 통신을 통해 ZCU와 데이터를 주고받습니다.

또한 Sensor ECU는 ZCU를 통해 OTA 업데이트를 수행할 수 있도록 설계되어 있으며, Sparse OTA와 SOTA 기반 A/B Slot 전환 구조를 적용했습니다.

---

## 2. Sensor ECU의 역할

Sensor ECU의 주요 역할은 다음과 같습니다.

* TOF 센서를 이용한 전방 거리 데이터 수집
* Hall Sensor를 이용한 차량 속도 계산
* ZCU로 센서 데이터 송신
* ZCU로부터 OTA 요청 수신
* CAN FD 기반 UDS OTA 수행
* Inactive Slot에 신규 펌웨어 기록
* CRC 검증 후 Bootloader/SOTA를 통한 Application 전환

---

## 3. 시스템 구조

```text
Vehicle Computer / HPC
        |
        | Ethernet / DoIP
        v
Front ZCU
        |
        | CAN / CAN FD
        v
Sensor ECU
```

Sensor ECU는 ZCU와 CAN/CAN FD로 연결됩니다.

일반 센서 데이터는 Classical CAN으로 송신하고, OTA 관련 데이터는 CAN FD를 사용합니다.

---

## 4. CAN Interface

| CAN ID | 방향               | 메시지명             | 설명                       | 프레임    |
| ------ | ---------------- | ---------------- | ------------------------ | ------ |
| 0x201  | Sensor ECU → ZCU | TofDistanceData  | TOF 센서 거리 데이터            | CAN    |
| 0x202  | Sensor ECU → ZCU | SpeedData        | Hall Sensor 기반 차량 속도 데이터 | CAN    |
| 0x600  | ZCU → Sensor ECU | OtaRequest       | OTA UDS 요청               | CAN FD |
| 0x601  | Sensor ECU → ZCU | OtaResponse      | OTA UDS 응답               | CAN FD |
| 0x700  | ZCU → Sensor ECU | Version Request  | Sensor ECU 버전 요청         | CAN    |
| 0x703  | Sensor ECU → ZCU | Version Response | Sensor ECU 버전 응답         | CAN    |

---

## 5. 센서 기능

### 5.1 TOF Sensor

TOF 센서는 전방 장애물과의 거리를 측정합니다.
Sensor ECU는 측정된 거리 값을 CAN 메시지 `0x201 TofDistanceData`로 ZCU에 전달합니다.

```text
Sensor ECU → ZCU
CAN ID: 0x201
Message: TofDistanceData
```

ZCU는 이 거리 데이터를 AEB 판단 로직에 활용합니다.

---

### 5.2 Hall Sensor

Hall Sensor는 바퀴에 부착된 자석의 자기장 변화를 감지하여 펄스를 생성합니다.
Sensor ECU는 이 펄스 간격 또는 펄스 개수를 기반으로 차량 속도를 계산합니다.

```text
Sensor ECU → ZCU
CAN ID: 0x202
Message: SpeedData
```

계산된 속도 값은 ZCU의 AEB 판단 및 차량 상태 판단에 사용됩니다.

---

## 6. OTA / SOTA 기능

Sensor ECU는 ZCU를 통해 OTA 업데이트를 수행합니다.

OTA 흐름은 다음과 같습니다.

```text
HPC / Vehicle Computer
        |
        | DoIP / UDS
        v
Front ZCU
        |
        | CAN FD / UDS
        v
Sensor ECU
```

ZCU는 Sensor ECU에 대해 OTA Gateway 역할을 수행합니다.
Sensor ECU는 ZCU로부터 `0x600 OtaRequest`를 수신하고, 처리 결과를 `0x601 OtaResponse`로 응답합니다.

---

## 7. Sparse OTA

기존 Full BIN OTA 방식은 Application Slot 전체를 하나의 BIN 파일처럼 전송했습니다.
이 방식은 실제 데이터가 없는 Address Gap까지 함께 전송해야 하므로 OTA 시간이 길어지는 문제가 있었습니다.

이를 개선하기 위해 Sparse OTA 방식을 적용했습니다.

Sparse OTA는 전체 BIN 파일을 전송하는 대신, 실제 데이터가 존재하는 Segment만 전송합니다.
각 Segment의 위치와 크기 정보는 Manifest/Metadata로 함께 전달됩니다.

```text
기존 방식:
Full BIN 전체 전송

Sparse OTA:
Segment 1 data + Segment 2 data + Manifest/Metadata 전송
```

Sparse OTA에서 전달되는 주요 정보는 다음과 같습니다.

* Segment 개수
* 각 Segment의 offset
* 각 Segment의 size
* Virtual Size
* Expected CRC32
* Gap Fill 값

전송하지 않은 Address Gap은 Flash erase 상태인 `0xFF`로 간주하여 Virtual CRC를 계산합니다.

---

## 8. Flash Erase / Write 정책

Sparse OTA에서는 첫 번째 Segment 다운로드 시 inactive slot 전체를 먼저 erase합니다.
이후 실제 데이터가 있는 Segment만 해당 offset 위치에 write합니다.

```text
Inactive Slot 전체 erase
        ↓
Segment 1 write
        ↓
Address Gap은 0xFF 상태 유지
        ↓
Segment 2 write
```

이 구조를 통해 전송하지 않은 빈 공간도 CRC 검증 시 일관된 상태로 유지할 수 있습니다.

---

## 9. Bootloader / SOTA Swap

OTA 다운로드가 완료되면 Sensor ECU는 CRC 검증을 수행합니다.
CRC가 정상일 경우 pending metadata를 DFLASH에 저장하고 reset을 수행합니다.

Reset 이후 Bootloader는 DFLASH에 저장된 pending metadata를 확인합니다.
Inactive Slot의 CRC가 정상이라면 SOTA Swap을 수행하여 새 Application을 활성화합니다.

```text
OTA 완료
  ↓
CRC 검증
  ↓
Pending Metadata 저장
  ↓
Reset
  ↓
Bootloader 진입
  ↓
Inactive Slot CRC 검증
  ↓
SOTA Swap
  ↓
New Application 실행
```

---

## 10. Version Request / Response

ZCU는 Sensor ECU의 현재 펌웨어 버전을 확인하기 위해 version request를 보낼 수 있습니다.

```text
ZCU → Sensor ECU
CAN ID: 0x700
Message: Version Request

Sensor ECU → ZCU
CAN ID: 0x703
Message: Version Response
```

이를 통해 ZCU는 Sensor ECU의 현재 버전을 확인하고, 필요한 경우 OTA 업데이트 여부를 판단할 수 있습니다.

---

## 11. 핵심 특징

* TC375 기반 Sensor ECU 구현
* TOF 센서 기반 거리 측정
* Hall Sensor 기반 속도 계산
* CAN 기반 센서 데이터 송신
* CAN FD 기반 OTA 통신
* ZCU를 통한 Sensor ECU OTA Gateway 구조
* Sparse OTA 적용으로 전송량 감소
* Virtual CRC 기반 무결성 검증
* Bootloader/SOTA 기반 A/B Slot 전환

---

## 12. 요약

Sensor ECU는 차량의 센서 데이터를 수집하여 ZCU에 전달하고, ZCU의 제어 판단에 필요한 정보를 제공합니다.
또한 CAN FD 기반 OTA와 Sparse OTA 구조를 적용하여 펌웨어 업데이트 시간을 줄이고, Bootloader/SOTA 구조를 통해 안정적인 Application 전환을 수행합니다.
