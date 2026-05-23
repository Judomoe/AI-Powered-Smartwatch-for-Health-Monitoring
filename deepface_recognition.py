import os
import cv2
from deepface import DeepFace
import socket
import time

# === ESP32-CAM STREAM URL ===
# IMPORTANT: Use the IP of the ESP32-CAM (video source)
url = "http://10.23.43.106:81/stream"  # ← change to your ESP32-CAM IP

# === ESP32 TCP Server Address and Port (for command) ===
# IMPORTANT: Use the IP of the SENSOR ESP32 (the one running the Arduino code)
ESP32_COMMAND_IP = "10.23.43.31"  # <-- CHANGE THIS TO THE SENSOR ESP32'S IP
ESP32_COMMAND_PORT = 12345  # <-- Matching the ESP32 code port

# === Path to your database folder ===
db_path = "database"


# --- Utility: Send command via TCP to ESP32 ---
def send_command_to_esp32(command):
    """Sends a string command to the ESP32 TCP server with retries."""
    max_retries = 3
    delay = 1  # seconds

    for attempt in range(max_retries):
        try:
            # Create a socket object and set a timeout for connection
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(2.0)

            # Connect to the ESP32
            s.connect((ESP32_COMMAND_IP, ESP32_COMMAND_PORT))

            # Send the command followed by a newline for ESP32's readStringUntil('\n')
            s.sendall((command + '\n').encode('utf-8'))

            print(f"[TCP] Sent command: '{command}' to {ESP32_COMMAND_IP}:{ESP32_COMMAND_PORT}")

            # Close the connection and exit retry loop
            s.close()
            return
        except socket.timeout:
            print(f"[ERROR] Attempt {attempt + 1}: Connection timed out to ESP32.")
        except Exception as e:
            print(f"[ERROR] Attempt {attempt + 1}: Could not send command to ESP32: {e}")

        # Exponential backoff
        if attempt < max_retries - 1:
            time.sleep(delay)
            delay *= 2

    print(f"[ERROR] Failed to send command after {max_retries} attempts.")


# --- Utility: skip macOS junk files ---
def valid_image_files(folder):
    valid_files = []
    for root, _, files in os.walk(folder):
        for f in files:
            if f.startswith("._") or f.startswith(".") or f.lower() == "info.json":
                continue
            if f.lower().endswith((".jpg", ".jpeg", ".png")):
                valid_files.append(os.path.join(root, f))
    return valid_files


# --- Validate images ---
images = valid_image_files(db_path)
if not images:
    raise RuntimeError("⚠️ No valid images found in your database folder.")

print(f"[INFO] Found {len(images)} valid images in database.")

# --- DeepFace model setup ---
model_name = "SFace"  # More accurate
detector_backend = "opencv"  # or "mtcnn" if RetinaFace is slow

print("[INFO] Building embeddings, please wait...")
# Build database representation (VCS file) once before the loop
try:
    DeepFace.find(
        img_path=images[0],
        db_path=db_path,
        model_name=model_name,
        detector_backend=detector_backend,
        enforce_detection=False,
        silent=True  # Suppress frequent terminal output
    )
    print("[INFO] Database ready ✅")
except Exception as e:
    print(f"[ERROR] Failed to build DeepFace database: {e}")

# --- Open ESP32 stream ---
cap = cv2.VideoCapture(url, cv2.CAP_FFMPEG)
if not cap.isOpened():
    raise RuntimeError("❌ Cannot open ESP32 stream.")

print("[INFO] Stream started...")

frame_count = 0
process_every = 5  # Process every 5th frame to reduce load and improve FPS

while True:
    ret, frame = cap.read()
    if not ret:
        print("[WARN] No frame received — check ESP32 connection. Retrying...")
        cap = cv2.VideoCapture(url, cv2.CAP_FFMPEG)
        time.sleep(1)
        continue

    frame_count += 1

    # Only run DeepFace every 'process_every' frames
    if frame_count % process_every == 0:
        try:
            # 1. Detect faces in the frame
            detections = DeepFace.extract_faces(
                img_path=frame,
                detector_backend=detector_backend,
                enforce_detection=False,
            )

            for face in detections:
                x, y, w, h = (
                    face["facial_area"]["x"],
                    face["facial_area"]["y"],
                    face["facial_area"]["w"],
                    face["facial_area"]["h"],
                )
                # Crop the detected face for recognition
                face_crop = frame[y:y + h, x:x + w]

                # 2. Recognize face against your database
                result = DeepFace.find(
                    img_path=face_crop,
                    db_path=db_path,
                    model_name=model_name,
                    detector_backend=detector_backend,
                    enforce_detection=False,
                    silent=True
                )

                person_name = "Unknown"
                if len(result) > 0 and not result[0].empty:
                    # Extract the person's name from the directory path
                    identity_path = result[0].identity.values[0]
                    person_name = os.path.basename(os.path.dirname(identity_path))

                    # --- CORE LOGIC: SEND COMMAND IF RAGAB IS RECOGNIZED ---
                    if person_name.lower() == "ragab":
                        send_command_to_esp32("ok")

                # 3. Draw visualization elements
                color = (0, 255, 0) if person_name != "Unknown" else (0, 0, 255)
                cv2.rectangle(frame, (x, y), (x + w, y + h), color, 2)
                cv2.putText(
                    frame,
                    person_name,
                    (x, y - 10),
                    cv2.FONT_HERSHEY_SIMPLEX,
                    0.8,
                    color,
                    2,
                )

        except Exception as e:
            # print(f"Detection/Recognition error: {e}") # Log or suppress errors from DeepFace
            pass

    cv2.imshow("DeepFace ESP32 Stream", frame)
    if cv2.waitKey(1) == 27:  # ESC to quit
        break

cap.release()
cv2.destroyAllWindows()
