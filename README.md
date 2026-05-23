# ⌚ AI-Powered Smartwatch for Health Monitoring & Security

## Overview
An **AI-powered smartwatch** built on ESP32 platform that combines **continuous health monitoring** with **biometric security**. The device tracks heart rate, blood oxygen levels, step count, and temperature while using facial recognition for secure unlocking - all at a low cost.

---

## Features

### Health Monitoring
- ❤️ **Heart Rate Monitoring** - Real-time BPM tracking with visual graphs
- 💨 **Blood Oxygen (SpO2)** - Continuous oxygen saturation monitoring
- 👣 **Step Counting** - Accurate step detection using MPU6050 accelerometer
- 🌡️ **Temperature Sensing** - Internal temperature monitoring
- 📊 **Real-time Graphs** - Visual representation of HR and SpO2 trends

### Security
- 👤 **Facial Recognition** - DeepFace AI integration for biometric authentication
- 🔒 **Auto-Lock** - Automatically locks after 60 seconds of inactivity
- 🚫 **Zero-Input Unlock** - No PINs or patterns needed
- 📹 **ESP32-CAM Integration** - Real-time face capture and recognition

### User Interface
- 🖥️ **TFT Color Display** - 1.8" ST7735 screen
- 🔘 **Single Button Control** - Cycle through screens when unlocked
- 📱 **Notification Support** - Receive and display notifications
- ⏰ **Real-Time Clock** - NTP-synchronized time display

---

## Hardware Components

| Component | Model | Purpose |
|-----------|-------|---------|
| Microcontroller | ESP32-WROOM-32 | Main processing & connectivity |
| Camera Module | ESP32-CAM | Face capture for authentication |
| Heart Rate Sensor | MAX30102 | HR & SpO2 measurement |
| Motion Sensor | MPU6050 | Step detection & movement |
| Display | ST7735 TFT | 1.8" color LCD |
| Button | GPIO 0 | User input control |
| Communication | I2C/SPI | Sensor & display interfaces |

---

## System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    ESP32-WROOM-32 (Watch)                    │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │  MAX30102   │  │  MPU6050    │  │   ST7735 TFT        │  │
│  │  (I2C)      │  │  (I2C)      │  │   (SPI)             │  │
│  └──────┬──────┘  └──────┬──────┘  └──────────┬──────────┘  │
│         │                │                    │              │
│         └────────────────┼────────────────────┘              │
│                          │                                   │
│                    ┌─────┴─────┐                            │
│                    │   WiFi    │                            │
│                    └─────┬─────┘                            │
└──────────────────────────┼──────────────────────────────────┘
                           │
                    ┌──────┴──────┐
                    │  TCP/IP     │
                    └──────┬──────┘
                           │
┌──────────────────────────┼──────────────────────────────────┐
│                    ┌─────┴─────┐                            │
│                    │  ESP32-CAM │                            │
│                    └─────┬─────┘                            │
│                          │                                   │
│                    ┌─────┴─────┐                            │
│                    │ DeepFace  │                            │
│                    │  PC/Pi    │                            │
│                    └───────────┘                            │
│                     AI Processing Unit                       │
└─────────────────────────────────────────────────────────────┘
```

---

## Data Flow

### Health Monitoring Flow
```
Sensors (MAX30102/MPU6050) → ESP32 Processing → TFT Display
                                    ↓
                            WiFi (Optional)
                                    ↓
                            Cloud/App (Future)
```

### Security Authentication Flow
```
User Face → ESP32-CAM → TCP Stream → DeepFace PC
                                            ↓
                                    Face Recognition
                                            ↓
                              "ok" Command → ESP32
                                            ↓
                                    Watch Unlocked
```

---

## Pin Configuration

| Component | Pin | Interface |
|-----------|-----|-----------|
| TFT CS | 5 | SPI |
| TFT DC | 16 | SPI |
| TFT RST | 17 | SPI |
| Button | 0 | GPIO (Pull-up) |
| I2C SDA | 21 | I2C |
| I2C SCL | 22 | I2C |

---

## Screen Modes

### Lock Screen
- Shows "LOCKED" status
- Displays "Waiting for Face ID..."
- No sensor data visible
- Button presses ignored

### Dashboard Screens (Unlocked)

| Screen | Display |
|--------|---------|
| Screen 0 | Step count + acceleration graph |
| Screen 1 | SpO2 level + oxygen graph |
| Screen 2 | Heart rate + BPM graph |
| Screen 3 | Digital watch + date + temperature |

### Notification Screen
- Blue background overlay
- Shows incoming messages
- Auto-dismisses after 10 seconds
- Does NOT unlock the watch

---

## AI Face Recognition (DeepFace)

### Python Script Features
- Real-time face detection from ESP32-CAM stream
- Database of authorized faces
- Sends "ok" command via TCP when face recognized
- Supports multiple face detection per frame

### Face Database Structure
```
database/
├── ragab/
│   ├── image1.jpg
│   └── image2.jpg
└── other_person/
    └── image.jpg
```

### Model Configuration
- **Model:** SFace (more accurate)
- **Detector Backend:** OpenCV
- **Processing:** Every 5th frame for performance

---

## WiFi & Network

### ESP32 Watch Configuration
```cpp
const char* ssid = "Your_SSID";
const char* password = "Your_Password";
const int commandPort = 12345;
```

### TCP Commands

| Command | Function |
|---------|----------|
| `ok` | Unlock the watch |
| `GET /ok` | HTTP unlock command |
| `GET /NOTIFY/{message}` | Display notification |

### NTP Time Synchronization
- Server: pool.ntp.org
- GMT Offset: +2 hours (Cairo)
- DST Offset: +1 hour

---

## Sensor Specifications

### MAX30102 (Heart Rate & SpO2)
| Parameter | Value |
|-----------|-------|
| LED Brightness | 0x3F |
| Sample Average | 4 |
| Sample Rate | 100 Hz |
| Pulse Width | 411 µs |
| ADC Range | 4096 |

### MPU6050 (Accelerometer)
| Parameter | Value |
|-----------|-------|
| Range | ±4G |
| Gyro Range | ±500°/s |
| Filter Bandwidth | 21 Hz |

---

## Installation Guide

### Prerequisites
- Arduino IDE with ESP32 board support
- Python 3.7+ with DeepFace
- USB cables for both ESP32 modules

### ESP32 Watch Setup

```bash
# 1. Install required libraries in Arduino IDE
- Adafruit GFX Library
- Adafruit ST7735 Library
- Adafruit MPU6050 Library
- Adafruit Sensor Library
- MAX30105 Library

# 2. Configure WiFi credentials
Edit ssid and password in the code

# 3. Upload to ESP32-WROOM-32
Select board: ESP32 Dev Module
Upload the Arduino code
```

### DeepFace Server Setup

```bash
# 1. Install Python dependencies
pip install opencv-python deepface

# 2. Configure ESP32-CAM IP
Edit url in Python script to your ESP32-CAM address

# 3. Configure ESP32 Watch IP
Edit ESP32_COMMAND_IP to your watch's IP address

# 4. Create face database folder
mkdir database
# Add authorized face images in subfolders

# 5. Run the recognition script
python face_recognition.py
```

---

## Notification System

### MacroDroid Integration
Send notifications via HTTP GET:
```
http://[ESP32_IP]/NOTIFY/Your%20Message%20Here
```

### URL Encoding
Spaces become `%20`
- `Hello World` → `Hello%20World`
- `Heart rate high!` → `Heart%20rate%20high!`

---

## Circuit Diagram

```
                    ESP32-WROOM-32
                  ┌─────────────────┐
                  │                 │
    MAX30102 ─────│ SDA (21)  SCL(22)│───── MPU6050
                  │                 │
    ST7735  ─────│ CS(5)   DC(16)  │
                  │ RST(17)         │
                  │                 │
    Button  ─────│ GPIO 0 (INPUT_PULLUP)
                  │                 │
                  └─────────────────┘
```

---

## Technical Algorithms

### Step Detection
- High-pass filter on accelerometer magnitude
- Peak detection with timing constraints
- Debouncing to prevent false counts

### Heart Rate Calculation
- Running average of 4 beats
- Threshold crossing detection
- Valid BPM range: 20-255 BPM

### SpO2 Calculation
- MAX30102 built-in algorithm
- 100-sample buffer for accuracy
- Valid output with finger detection

---

## Troubleshooting

### Watch Won't Unlock
- Verify ESP32-CAM stream is accessible
- Check DeepFace server is running
- Ensure face is in database folder
- Verify TCP connection between devices

### No Heart Rate Reading
- Ensure finger is properly placed on sensor
- Check MAX30102 connections
- Verify I2C address (0x57)

### Steps Not Counting
- Check MPU6050 orientation
- Verify I2C communication
- Adjust step detection threshold

### Display Issues
- Check SPI pin connections
- Verify TFT initialization
- Adjust rotation if needed

---

## Future Improvements

- 📱 Mobile app integration
- ☁️ Cloud data storage
- 📈 Historical health trends
- 🚨 Emergency alert system
- 🔋 Power optimization
- 🎤 Voice commands
- 📊 ECG waveform display
- 🌡️ External temperature sensor

---

## Project Team

| Name | ID |
|------|-----|
| Mohamed Mustafa Kamel | 221017636 |
| Abduallah Ragab | 221007888 |
| Moustafa Ahmed Elashry | 221017920 |

---

## Course
Smart Technology Innovations

## Institution
Arab Academy for Science & Technology & Maritime Transport

## Year
2024

---

## License
This project was created for educational purposes as part of the Smart Technology Innovations course.
```

---

## Copy Instructions

1. **Click and drag** from the first line (`# ⌚ AI-Powered Smartwatch...`) to the last line
2. **Press Ctrl+C** (Windows/Linux) or **Cmd+C** (Mac)
3. **Go to GitHub** → Your repository → Click "Add file" → "Create new file"
4. **Name the file** `README.md`
5. **Press Ctrl+V** to paste
6. **Scroll down** and click "Commit new file"

---

## GitHub Topics (Tags)
- `esp32`
- `smartwatch`
- `facial-recognition`
- `deepface`
- `health-monitoring`
- `heart-rate-monitor`
- `spo2`
- `step-counter`
- `iot`
- `wearable-technology`
- `arduino`
- `python`
- `computer-vision`
