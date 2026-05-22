import os
os.environ["SDL_VIDEODRIVER"] = "dummy"

import cv2
import numpy as np
import pygame
import socket
import struct
import time
import threading
from collections import deque
from http.server import BaseHTTPRequestHandler, HTTPServer


# ==========================================
# 설정값
# ==========================================
LKAS_ENABLED        = True
CAMERA_INDEX        = 0
IMAGE_SIZE          = (640, 360)
ROI_START_RATIO     = 0.5
CALIBRATION_OFFSET  = 0
MAX_ANGLE           = 20.0
SMOOTH_FRAMES       = 5
LANE_SMOOTH_FRAMES  = 10
STEER_DEADZONE      = 0.1
ANGLE_DEADZONE      = 3.0
SPEED_THRESHOLD     = 117
STREAM_PORT         = 8080
SENSITIVITY = 3.0  # 1.0이 기본, 높을수록 민감
last_frame = None
vehicle_speed = 0

# ==========================================
# 웹 스트리밍
# ==========================================
latest_frame = None
frame_lock   = threading.Lock()

class StreamHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/':
            self.send_response(200)
            self.send_header('Content-type', 'text/html')
            self.end_headers()
            html = b"""
            <html>
            <head>
                <title>LKAS Monitor</title>
                <style>
                    body { background: #111; color: #0f0;
                           font-family: monospace; text-align: center; }
                    img  { width: 640px; border: 2px solid #0f0; }
                </style>
            </head>
            <body>
                <h2>LKAS Live Monitor</h2>
                <img src="/stream">
            </body>
            </html>
            """
            self.wfile.write(html)

        elif self.path == '/stream':
            self.send_response(200)
            self.send_header('Content-type', 'multipart/x-mixed-replace; boundary=frame')
            self.end_headers()
            try:
                while True:
                    with frame_lock:
                        if latest_frame is None:
                            time.sleep(0.05)
                            continue
                        frame_bytes = latest_frame
                    self.wfile.write(b"--frame\r\n")
                    self.wfile.write(b"Content-Type: image/jpeg\r\n\r\n")
                    self.wfile.write(frame_bytes)
                    self.wfile.write(b"\r\n")
            except Exception:
                pass

    def log_message(self, format, *args):
        pass

def start_stream_server():
    server = HTTPServer(('0.0.0.0', STREAM_PORT), StreamHandler)
    print(f"Stream server: http://RaspberryPi_IP:{STREAM_PORT}")
    server.serve_forever()


# ==========================================
# SOME/IP 설정
# ==========================================

ZCU_IP = "192.168.10.2"
ZCU_PORT = 30500

# RX (Vehicle Speed)
SERVICE_ID_SPEED = 0x0002
METHOD_ID_SPEED  = 0x2003

RPI_IP   = "192.168.10.1"
RPI_PORT = 30500

SERVICE_ID = 0x0001
METHOD_ID  = 0x1001

CLIENT_ID = 0x0001

PROTOCOL_VERSION = 0x01
INTERFACE_VERSION = 0x01

MSG_TYPE_REQUEST = 0x00
RETURN_CODE_OK   = 0x00

session_id = 1


# ==========================================
# UDP Socket
# ==========================================

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)


# ==========================================
# Vehicle Speed RX Socket
# ==========================================

rx_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

rx_sock.bind((RPI_IP, RPI_PORT))

rx_sock.setblocking(False)

print(f"Vehicle Speed RX Ready : {RPI_PORT}")


# ==========================================
# 영상 수신 UDP Socket
# ==========================================

VIDEO_PORT = 30501

video_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

video_sock.bind(("0.0.0.0", VIDEO_PORT))

video_sock.setblocking(False)

video_sock.setsockopt(
    socket.SOL_SOCKET,
    socket.SO_RCVBUF,
    65535
)

print(f"Video Receiver Ready : {VIDEO_PORT}")


# ==========================================
# 기어 상태
# ==========================================

GEAR_P = "P"
GEAR_D = "D"

gear_state = GEAR_P

prev_button_p = False
prev_button_d = False


# ==========================================
# pygame 초기화
# ==========================================
pygame.init()
pygame.joystick.init()

while pygame.joystick.get_count() == 0:
    print("Waiting for gamepad...")
    time.sleep(1)
    pygame.joystick.quit()
    pygame.joystick.init()

js = pygame.joystick.Joystick(0)
js.init()
print("Gamepad connected:", js.get_name())


# ==========================================
# 변환 함수
# ==========================================
def axis_to_byte(axis_value):
    value = int((axis_value + 1.0) * 127.5)
    return max(0, min(255, value))

def angle_to_byte(angle_deg):
    normalized = angle_deg / MAX_ANGLE
    normalized = max(-1.0, min(1.0, normalized))
    return axis_to_byte(normalized)


# ==========================================
# SOME/IP 패킷 생성
# ==========================================

def build_someip(service_id,
                 method_id,
                 client_id,
                 session_id,
                 payload):

    message_id = (service_id << 16) | method_id
    request_id = (client_id << 16) | session_id

    # Request ID 4Byte
    # PV/IV/Type/RC 4Byte
    # + payload

    length = 8 + len(payload)

    header = struct.pack(
        "!IIIBBBB",
        message_id,
        length,
        request_id,
        PROTOCOL_VERSION,
        INTERFACE_VERSION,
        MSG_TYPE_REQUEST,
        RETURN_CODE_OK
    )

    return header + payload


# ==========================================
# SOME/IP 파싱
# ==========================================

def parse_someip(data):

    if len(data) < 16:
        return None

    (
        message_id,
        length,
        request_id,
        pv,
        iv,
        msg_type,
        ret_code
    ) = struct.unpack(
        "!IIIBBBB",
        data[:16]
    )

    service_id = (message_id >> 16) & 0xFFFF

    method_id = message_id & 0xFFFF

    payload = data[16:]

    return {
        "service_id": service_id,
        "method_id": method_id,
        "payload": payload
    }


# ==========================================
# Vehicle Speed SOME/IP RX
# ==========================================

def read_vehicle_speed():

    global vehicle_speed

    try:

        while True:

            data, addr = rx_sock.recvfrom(1024)

            parsed = parse_someip(data)

            if parsed is None:
                continue

            if (
                parsed["service_id"] != SERVICE_ID_SPEED
                or
                parsed["method_id"] != METHOD_ID_SPEED
            ):
                continue

            payload = parsed["payload"]

            if len(payload) < 2:
                continue

            low  = payload[0]

            high = payload[1]

            vehicle_speed = low | (high << 8)

    except BlockingIOError:

        pass

    except Exception as e:

        print("Vehicle Speed RX Error :", e)


# ==========================================
# Core 1: 영상 전처리
# ==========================================
def vision_preprocessing(frame):
    h, w  = frame.shape[:2]
    roi_y = int(h * ROI_START_RATIO)
    roi   = frame[roi_y:h, :]

    ycrcb     = cv2.cvtColor(roi, cv2.COLOR_BGR2YCrCb)
    y, cr, cb = cv2.split(ycrcb)

    clahe    = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(4, 4))
    y_eq     = clahe.apply(y)

    ycrcb_eq = cv2.merge([y_eq, cr, cb])
    bgr_eq   = cv2.cvtColor(ycrcb_eq, cv2.COLOR_YCrCb2BGR)
    blurred  = cv2.GaussianBlur(bgr_eq, (3, 3), 0)

    return blurred, roi_y


# ==========================================
# Core 2: 마스크 생성
# ==========================================
def mask_generation(blurred):
    ycrcb     = cv2.cvtColor(blurred, cv2.COLOR_BGR2YCrCb)
    y, cr, cb = cv2.split(ycrcb)

    _, cr_mask = cv2.threshold(cr, 140, 255, cv2.THRESH_BINARY)
    _, cb_mask = cv2.threshold(cb, 120, 255, cv2.THRESH_BINARY_INV)
    red_mask   = cv2.bitwise_and(cr_mask, cb_mask)

    _, y_mask  = cv2.threshold(y, 60, 255, cv2.THRESH_BINARY)
    red_mask   = cv2.bitwise_and(red_mask, y_mask)

    kernel = cv2.getStructuringElement(cv2.MORPH_RECT, (3, 3))
    closed = cv2.morphologyEx(red_mask, cv2.MORPH_CLOSE, kernel)
    opened = cv2.morphologyEx(closed,   cv2.MORPH_OPEN,  kernel)

    return opened


# ==========================================
# Core 3: 윤곽선 검출 및 차선 중앙 계산
# ==========================================
def line_detection(mask, frame_w):
    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)

    left_xs,  left_ys  = [], []
    right_xs, right_ys = [], []
    mid_x = frame_w // 2

    for cnt in contours:
        if cv2.arcLength(cnt, False) < 10:
            continue
        for pt in cnt:
            x, y = pt[0]
            if x < mid_x:
                left_xs.append(x)
                left_ys.append(y)
            else:
                right_xs.append(x)
                right_ys.append(y)

    lane_center = _calc_lane_center(left_xs, right_xs)

    has_left  = len(left_xs)  >= 5
    has_right = len(right_xs) >= 5

    if has_left and has_right:
        status = "ok"
    else:
        status = "fail"

    if lane_center is not None and status == "ok":
        offset      = frame_w // 2 - lane_center - CALIBRATION_OFFSET
        steer_angle = -(offset / (frame_w // 2)) * MAX_ANGLE * SENSITIVITY
    else:
        offset      = 0
        steer_angle = None

    return steer_angle, status, offset, lane_center, left_xs, left_ys, right_xs, right_ys


def _calc_lane_center(left_xs, right_xs):
    left_x_mid  = int(np.median(left_xs))  if len(left_xs)  >= 5 else None
    right_x_mid = int(np.median(right_xs)) if len(right_xs) >= 5 else None

    if left_x_mid is not None and right_x_mid is not None:
        return (left_x_mid + right_x_mid) // 2
    elif left_x_mid is not None:
        return left_x_mid
    elif right_x_mid is not None:
        return right_x_mid
    return None


# ==========================================
# 시각화
# ==========================================
def make_vis_frame(frame, mask, roi_y, steer_angle, status, offset,
                   lane_center, left_xs, left_ys, right_xs, right_ys,
                   frame_w, frame_h, control_mode, fps):
    result     = frame.copy()
    car_center = frame_w // 2
    line_top   = roi_y
    line_bottom= frame_h

    # ROI 경계선
    cv2.line(result, (0, roi_y), (frame_w, roi_y), (255, 255, 0), 1)

    # ── 직선으로 차선 표시 (2차 곡선 제거) ──────────────
    for xs, ys in [(left_xs, left_ys), (right_xs, right_ys)]:
        if len(xs) >= 5:
            try:
                y_min  = min(ys)
                y_max  = max(ys)
                # y_min, y_max에 해당하는 x 평균
                x_top  = int(np.mean([x for x, y in zip(xs, ys) if y == y_min]))
                x_bot  = int(np.mean([x for x, y in zip(xs, ys) if y == y_max]))
                cv2.line(result,
                         (x_top, y_min + roi_y),
                         (x_bot, y_max + roi_y),
                         (0, 200, 255), 3)
            except Exception:
                pass

    # 차량 중앙 (파랑)
    cv2.line(result, (car_center, line_top), (car_center, line_bottom), (255, 0, 0), 2)
    cv2.putText(result, "CAR", (car_center + 5, line_top + 20),
                cv2.FONT_HERSHEY_SIMPLEX, 0.4, (255, 0, 0), 1)

    # 차선 중앙 (빨강)
    if lane_center is not None and status == "ok":
        cv2.line(result, (lane_center, line_top), (lane_center, line_bottom), (0, 0, 255), 2)
        cv2.putText(result, "LANE", (lane_center + 5, line_top + 20),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.4, (0, 0, 255), 1)
        mid_y = (line_top + line_bottom) // 2
        cv2.arrowedLine(result, (car_center, mid_y), (lane_center, mid_y),
                        (0, 255, 255), 2, tipLength=0.2)
        cv2.putText(result, f"Offset: {offset:+d}px",
                    (min(car_center, lane_center) + 5, mid_y - 10),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 255), 1)

    # 마스크 미리보기 (우상단)
    mh, mw       = 90, 120
    mask_preview = cv2.resize(mask, (mw, mh))
    mask_bgr     = cv2.cvtColor(mask_preview, cv2.COLOR_GRAY2BGR)
    result[10:10+mh, frame_w-mw-10:frame_w-10] = mask_bgr

    # 상태 텍스트
    color_map = {"ok": (0, 255, 0), "fail": (0, 0, 255)}
    cv2.putText(result, f"Angle: {steer_angle:+.1f} deg",
                (10, 25),  cv2.FONT_HERSHEY_SIMPLEX, 0.6, color_map[status], 2)
    cv2.putText(result, f"Status: {status}",
                (10, 50),  cv2.FONT_HERSHEY_SIMPLEX, 0.5, color_map[status], 1)
    cv2.putText(result, f"Mode: {control_mode}",
                (10, 72),  cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)
    cv2.putText(result, f"FPS: {fps:.1f}",
                (10, 94),  cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)

    return result


# ==========================================
# 스트림 서버 시작 (백그라운드)
# ==========================================
threading.Thread(target=start_stream_server, daemon=True).start()


# ==========================================
# 메인 루프
# ==========================================
angle_history       = deque(maxlen=SMOOTH_FRAMES)
lane_center_history = deque(maxlen=LANE_SMOOTH_FRAMES)
last_valid_angle    = 0.0
prev_time           = time.time()

while True:
    try:
        pygame.event.pump()

        # ==================================
        # Vehicle Speed 수신
        # ==================================

        read_vehicle_speed()

        # ==================================
        # 기어 버튼 입력
        # ==================================

        button_p = js.get_button(0)
        button_d = js.get_button(1)

        # 버튼 0 → P
        if button_p and not prev_button_p:

            gear_state = GEAR_P

            print("")
            print("==========")
            print("GEAR -> P")
            print("==========")
            print("")

        # 버튼 1 → D
        if button_d and not prev_button_d:

            gear_state = GEAR_D

            print("")
            print("==========")
            print("GEAR -> D")
            print("==========")
            print("")

        prev_button_p = button_p
        prev_button_d = button_d

        # ==================================
        # 게임패드 Axis
        # ==================================

        axis_speed = js.get_axis(1)
        axis_steer = js.get_axis(2)

        speed_byte = axis_to_byte(axis_speed)

        # ==================================
        # P단이면 강제 중립
        # ==================================

        if gear_state == GEAR_P:

            speed_byte = 127
            steer_byte = 127

            axis_speed = 0.0
            axis_steer = 0.0

        else:

            steer_byte = axis_to_byte(axis_steer)

        is_moving = speed_byte < SPEED_THRESHOLD
        manual_steering = abs(axis_steer) > STEER_DEADZONE

        # ── UDP 영상 수신 + 차선 인식 (항상 실행) ──────────────
        lxs, lys, rxs, rys = [], [], [], []

        try:

            data, addr = video_sock.recvfrom(65535)

            np_data = np.frombuffer(data, dtype=np.uint8)

            frame = cv2.imdecode(np_data, cv2.IMREAD_COLOR)

            if frame is None:
                raise Exception("Frame decode failed")

            ret = True

        except BlockingIOError:

            ret = False

        except Exception as video_error:

            print("Video Receive Error:", video_error)

            ret = False

        if ret:
            frame            = cv2.resize(frame, IMAGE_SIZE)
            frame_h, frame_w = frame.shape[:2]
            blurred, roi_y   = vision_preprocessing(frame)
            mask             = mask_generation(blurred)
            steer_angle, status, offset, lane_center, lxs, lys, rxs, rys = \
                line_detection(mask, frame_w)

            # 차선 중앙 smoothing
            if lane_center is not None and status == "ok":
                lane_center_history.append(lane_center)

            if lane_center_history:
                lane_center = int(np.mean(lane_center_history))
                offset      = frame_w // 2 - lane_center - CALIBRATION_OFFSET
                steer_angle = -(offset / (frame_w // 2)) * MAX_ANGLE * SENSITIVITY

            last_frame = frame.copy()

        else:
            if last_frame is not None:
                frame = last_frame.copy()
                frame_h, frame_w = frame.shape[:2]

                blurred, roi_y = vision_preprocessing(frame)
                mask = mask_generation(blurred)

                steer_angle, status, offset, lane_center, lxs, lys, rxs, rys = \
                    line_detection(mask, frame_w)

            else:
                continue

        # ── 조향값 결정 ──────────────────────────────────

        if gear_state == GEAR_P:

            speed_byte  = 127
            steer_byte  = 127
            smooth_angle = 0.0

            control_mode = "P GEAR"

        elif manual_steering or not LKAS_ENABLED or not is_moving:

            steer_byte   = axis_to_byte(axis_steer)
            smooth_angle = axis_steer * MAX_ANGLE

            if manual_steering:
                control_mode = "MANUAL"
            elif not is_moving:
                control_mode = "STOPPED"
            else:
                control_mode = "MANUAL(LKAS OFF)"

        else:

            if status == "ok" and steer_angle is not None:
                steer_angle      = np.clip(steer_angle, -MAX_ANGLE, MAX_ANGLE)
                angle_history.append(steer_angle)
                last_valid_angle = steer_angle
            else:
                steer_angle = last_valid_angle

            smooth_angle = float(np.mean(angle_history)) if angle_history else 0.0

            if abs(smooth_angle) < ANGLE_DEADZONE:
                smooth_angle = 0.0

            steer_byte   = angle_to_byte(smooth_angle)
            control_mode = f"LKAS ({status})"
            
        # ── SOME/IP Payload 생성 ───────────────────────

        payload = bytes([
            speed_byte,
            steer_byte
        ])

        # ── SOME/IP 패킷 생성 ─────────────────────────

        packet = build_someip(
            SERVICE_ID,
            METHOD_ID,
            CLIENT_ID,
            session_id,
            payload
        )

        # ── Ethernet UDP 송신 ────────────────────────

        try:
            sock.sendto(packet, (ZCU_IP, ZCU_PORT))
        except Exception as tx_error:
            print("TX Error :", tx_error)

        # ── Session 증가 ─────────────────────────────

        session_id += 1

        if session_id > 0xFFFF:
            session_id = 1

        # ── FPS 계산 ─────────────────────────────────────
        cur_time  = time.time()
        fps       = 1.0 / (cur_time - prev_time + 1e-9)
        prev_time = cur_time

        # ── 시각화 프레임 → 스트리밍 ─────────────────────
        vis = make_vis_frame(frame, mask, roi_y, smooth_angle, status, offset,
                             lane_center, lxs, lys, rxs, rys,
                             frame_w, frame_h, control_mode, fps)
        _, jpeg = cv2.imencode('.jpg', vis, [cv2.IMWRITE_JPEG_QUALITY, 50])
        with frame_lock:
            latest_frame = jpeg.tobytes()

        # ── 디버깅 출력 ───────────────────────────────────
        print(f"[{control_mode}] Angle: {smooth_angle:+.1f} | "
            f"Vehicle Speed: {vehicle_speed} | "
            f"Steer: {steer_byte} | FPS: {fps:.1f}")

        print(f"GEAR: {gear_state}")
        print(f"Packet: {packet.hex().upper()}")
        print("--------------------------------")

    except KeyboardInterrupt:
        break

    except Exception as e:
        print("Error:", e)
        time.sleep(1)

sock.close()
rx_sock.close()
video_sock.close()
print("소켓 종료")