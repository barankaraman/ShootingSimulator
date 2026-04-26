"""
hand_tracker.py — MediaPipe Tasks API el takip katmanı
(MediaPipe 0.10+ için — eski mp.solutions API kaldırıldı)

Gerekli model: hand_landmarker.task (aynı klasörde olmalı)
İndirmek için:
  curl -L -o hand_landmarker.task \
    "https://storage.googleapis.com/mediapipe-models/hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task"

Protokol: JSON string → UDP 127.0.0.1:5005
  {"x": 0.52, "y": 0.41, "state": 0, "detected": true}
  state: 0 = açık el (nişan), 1 = yumruk (ateş)
"""

import cv2
import mediapipe as mp
from mediapipe.tasks import python as mp_python
from mediapipe.tasks.python import vision as mp_vision
import socket
import json
import time
import os

# ── UDP Ayarları ─────────────────────────────────────────────
UDP_IP   = "127.0.0.1"
UDP_PORT = 5005

# ── Model yolu ───────────────────────────────────────────────
# Bu script'in bulunduğu klasördeki hand_landmarker.task dosyası
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
MODEL_PATH = os.path.join(SCRIPT_DIR, "hand_landmarker.task")

# ── Parmak landmark ID'leri ──────────────────────────────────
FINGER_TIPS = [8, 12, 16, 20]   # İşaret, Orta, Yüzük, Serçe ucu
FINGER_MCPS = [5,  9, 13, 17]   # Karşılık gelen eklemler

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)


def is_fist(landmarks) -> bool:
    """
    Yumruk tespiti:
    Parmak ucu Y'si > MCP Y'si ise parmak kapalı (Y ekseni aşağı iner).
    4 parmaktan 3+ kapalıysa yumruk sayılır.
    """
    bent = 0
    for tip_id, mcp_id in zip(FINGER_TIPS, FINGER_MCPS):
        if landmarks[tip_id].y > landmarks[mcp_id].y:
            bent += 1
    return bent >= 3


def hand_center(landmarks):
    """Bilek (landmark 0) koordinatlarını döndürür, [0.0, 1.0] normalleştirilmiş."""
    return landmarks[0].x, landmarks[0].y


def draw_landmarks(frame, landmarks):
    """Ekrana iskelet çizer."""
    h, w = frame.shape[:2]
    # Nokta → bağlantı çizgileri (basit versiyon)
    connections = [
        (0,1),(1,2),(2,3),(3,4),       # Başparmak
        (0,5),(5,6),(6,7),(7,8),       # İşaret
        (0,9),(9,10),(10,11),(11,12),  # Orta
        (0,13),(13,14),(14,15),(15,16),# Yüzük
        (0,17),(17,18),(18,19),(19,20),# Serçe
        (5,9),(9,13),(13,17),          # Avuç içi
    ]
    pts = [(int(lm.x * w), int(lm.y * h)) for lm in landmarks]
    for a, b in connections:
        cv2.line(frame, pts[a], pts[b], (0, 200, 80), 2)
    for pt in pts:
        cv2.circle(frame, pt, 5, (0, 255, 150), -1)


def main():
    if not os.path.exists(MODEL_PATH):
        print(f"[HATA] Model bulunamadı: {MODEL_PATH}")
        print("Şunu çalıştır:")
        print('  curl -L -o hand_landmarker.task \\\n'
              '    "https://storage.googleapis.com/mediapipe-models/'
              'hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task"')
        return

    # ── HandLandmarker oluştur (VIDEO modu) ─────────────────
    base_options = mp_python.BaseOptions(model_asset_path=MODEL_PATH)
    options = mp_vision.HandLandmarkerOptions(
        base_options=base_options,
        running_mode=mp_vision.RunningMode.VIDEO,
        num_hands=1,
        min_hand_detection_confidence=0.6,
        min_hand_presence_confidence=0.5,
        min_tracking_confidence=0.5,
    )

    cap = cv2.VideoCapture(0)
    if not cap.isOpened():
        print("[HATA] Kamera açılamadı!")
        return

    print(f"[OK] Hand Tracker başlatıldı → UDP {UDP_IP}:{UDP_PORT}")

    with mp_vision.HandLandmarker.create_from_options(options) as detector:
        while cap.isOpened():
            ok, frame = cap.read()
            if not ok:
                continue

            # Aynalama (daha doğal his)
            frame = cv2.flip(frame, 1)

            # BGR → RGB, MediaPipe Image nesnesine sar
            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)

            # VIDEO modunda timestamp vermek zorunlu (ms cinsinden monoton artmalı)
            timestamp_ms = int(time.monotonic() * 1000)
            result = detector.detect_for_video(mp_image, timestamp_ms)

            data = {"x": 0.5, "y": 0.5, "state": 0, "detected": False}

            if result.hand_landmarks:
                landmarks = result.hand_landmarks[0]
                x, y  = hand_center(landmarks)
                state = 1 if is_fist(landmarks) else 0
                data  = {"x": round(x, 4), "y": round(y, 4),
                         "state": state, "detected": True}
                draw_landmarks(frame, landmarks)

            # UDP gönder
            sock.sendto(json.dumps(data).encode(), (UDP_IP, UDP_PORT))

            # Debug overlay
            status = "FIST (FIRE!)" if data["state"] == 1 else "OPEN  (aim)"
            color  = (0, 0, 255) if data["state"] == 1 else (0, 220, 80)
            cv2.putText(frame, status, (10, 38),
                        cv2.FONT_HERSHEY_SIMPLEX, 1.1, color, 2)
            cv2.putText(frame, f"x={data['x']:.2f}  y={data['y']:.2f}", (10, 72),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (200, 200, 200), 1)

            cv2.imshow("Hand Tracker — ESC ile kapat", frame)
            if cv2.waitKey(1) & 0xFF == 27:
                break

    cap.release()
    cv2.destroyAllWindows()
    sock.close()


if __name__ == "__main__":
    main()
