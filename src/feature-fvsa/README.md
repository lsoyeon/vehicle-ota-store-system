# FVSA (Forward Vehicle Start Alarm)

<div align="center">

# 🚘 Forward Vehicle Start Alarm System

Camera ECU & HPC 기반 전방 차량 출발 알림 시스템

</div>

---

# 📌 Overview

본 프로젝트는 차량 정차 상황에서
전방 차량의 출발 여부를 감지하여 운전자에게 알림을 제공하는
FVSA(Forward Vehicle Start Alarm) 시스템 구현 프로젝트입니다.

Camera ECU에서 실시간 영상을 촬영하고
Ethernet 통신을 통해 HPC(High Performance Computer)로 프레임을 전송합니다.

HPC에서는 OpenCV 기반 객체 추적 및 움직임 분석을 수행하여
전방 차량의 이동 여부를 판단하고 운전자에게 알림을 제공합니다.

SDV(Software Defined Vehicle) 환경 및
Zonal Architecture 구조를 고려하여 설계되었습니다.

---

# 🎯 Main Features

* 📷 Camera ECU 기반 영상 수집
* 🌐 Ethernet 기반 프레임 전송
* 🚗 전방 차량 인식
* 📍 차량 움직임 추적
* 🚨 전방 차량 출발 알림
* 🧠 HPC 기반 영상 처리
* ⚡ 실시간 객체 분석
* 🔄 ECU 간 네트워크 통신

---

# 🏗️ System Architecture

```plaintext
┌─────────────────┐
│   Camera ECU    │
│  (Raspberry Pi) │
└────────┬────────┘
         │
         │ Ethernet
         ▼
┌─────────────────┐
│       HPC       │
│ Vehicle Detect  │
│ Motion Tracking │
│  FVSA Control   │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Driver Warning  │
│ Alarm / Display │
└─────────────────┘
```

---

# ⚙️ Tech Stack

## Hardware

* Raspberry Pi
* Camera Module
* Vehicle Platform

## Software

* Python
* OpenCV
* Linux (Ubuntu)
* Socket Programming

## Communication

* Ethernet TCP/IP

---

# 📂 Project Structure

```bash
FVSA/
├── camera_ecu/        # Camera ECU source
├── hpc/               # HPC processing source
├── vehicle_detection/ # Vehicle detection algorithms
├── tracking/          # Motion tracking logic
├── communication/     # Ethernet communication
├── utils/             # Utility functions
├── docs/              # Documents
└── README.md
```

---

# 🚀 How It Works

1. Camera ECU에서 실시간 영상 촬영
2. Ethernet 통신을 통해 HPC로 프레임 전송
3. HPC에서 전방 차량 검출 수행
4. 객체 움직임 분석 수행
5. 차량 출발 여부 판단
6. 운전자에게 경고 및 알림 제공

---

# 🚗 Vehicle Detection Pipeline

```plaintext
Camera Input
      ↓
Frame Streaming
      ↓
Grayscale Conversion
      ↓
Object Detection
      ↓
Vehicle Tracking
      ↓
Motion Analysis
      ↓
Movement Decision
      ↓
Driver Alert
```

---

# 🖥️ Installation

## Clone Repository

```bash
git clone https://github.com/HAMES-6th-Overdrive/FVSA.git
cd FVSA
```

## Install Dependencies

```bash
pip install -r requirements.txt
```

---

# ▶️ Run

## Camera ECU

```bash
python camera_sender.py
```

## HPC

```bash
python main.py
```

---

# 📸 Demo

* Real-time vehicle detection
* Forward vehicle movement tracking
* Ethernet frame streaming
* Driver warning visualization

---

# 📈 Expected Results

* 안정적인 전방 차량 검출
* 실시간 움직임 분석
* 신속한 차량 출발 감지
* 운전자 반응 시간 향상
* ECU 간 안정적인 데이터 전송

---

# 🔥 Future Work

* 딥러닝 기반 객체 인식 적용
* 거리 추정 기능 추가
* CAN 통신 연동
* OTA 기반 업데이트 시스템
* SDV 플랫폼 확장

---

# 👨‍💻 Team

HAMES 6th Overdrive Team

---

# 📄 License

This project is for educational and research purposes.
