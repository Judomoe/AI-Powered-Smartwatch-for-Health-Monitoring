// ESP32 + MPU6050 + MAX30102 + 1.8" ST7735 TFT + Button
// FINAL VERSION: Strict Unlock Control
// - Reads Temp from MAX30105
// - Decodes URL text from MacroDroid
// - Shows Notifications for 10 seconds (DOES NOT UNLOCK)
// - Only "ok" from Python unlocks the watch

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <WiFi.h>
#include "time.h"
#include "MAX30105.h"
#include "heartRate.h"
#include "spo2_algorithm.h"

// --- WiFi Credentials ---
const char* ssid = "Mostafa Ahmed";
const char* password = "AwMsM#76aWmSm#95";

// --- TCP Server Configuration ---
const int commandPort = 12345;
WiFiServer server(commandPort);
WiFiClient client;

// --- Time / Watch Configuration ---
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7200;      // UTC +2 (Cairo)
const int daylightOffset_sec = 3600;  // DST offset

// --- Display Configuration ---
#define TFT_CS 5
#define TFT_DC 16
#define TFT_RST 17
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// --- Inputs ---
#define BUTTON_PIN 0

// --- Sensors ---
Adafruit_MPU6050 mpu;
MAX30105 particleSensor;

// --- State Variables ---
volatile int screenIndex = 0;
volatile bool isRagabRecognized = false;
unsigned long recognitionTimeout = 0;
const unsigned long RECOGNITION_DURATION_MS = 60000;

// ** NEW: Notification State **
bool isNotificationActive = false;
String notificationMsg = "";
unsigned long notificationTimeout = 0;
const unsigned long NOTIF_DURATION = 10000;  // Show for 10 seconds

// --- Graphing Constants ---
#define GRAPH_WIDTH 124
#define GRAPH_HEIGHT 60
#define GRAPH_X 2
#define GRAPH_Y 50

// --- MPU Data ---
const int ACCEL_BUF_LEN = 128;
float accelBuffer[ACCEL_BUF_LEN];
int accelIdx = 0;
int stepCount = 0;
unsigned long lastStepTime = 0;
float lp_mag = 0.0f;
const float LP_ALPHA = 0.98f;

// --- SpO2 Algorithm Data ---
const int32_t SPO2_BUFFER_SIZE = 100;
uint32_t irBuffer[SPO2_BUFFER_SIZE];
uint32_t redBuffer[SPO2_BUFFER_SIZE];
int32_t maxim_spo2 = 0;
int8_t maxim_spo2_valid = 0;
int32_t maxim_hr = 0;
int8_t maxim_hr_valid = 0;
int spo2_sample_idx = 0;

// --- HR & Graphing Buffers ---
float spo2GraphBuffer[GRAPH_WIDTH];
int spo2GraphIdx = 0;
float hrGraphBuffer[GRAPH_WIDTH];
int hrGraphIdx = 0;

// --- Custom BPM Logic ---
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
int beatAvg = 0;
bool fingerDetected = false;
int validBPM = 0;
long lastIrValue = 0;
const long beatThreshold = 80000;

// --- Timing ---
unsigned long lastSampleMs = 0;
const unsigned long SAMPLE_INTERVAL = 40;  // 25Hz
unsigned long lastButtonMs = 0;
const unsigned long DEBOUNCE_MS = 200;

// --- Prototypes ---
void updateScreen();
void drawLockScreen();
void drawMPUScreen();
void drawSpO2Screen();
void drawHRscreen();
void drawWatchScreen();
void drawNotificationScreen();
void drawAllScreens();
void detectStep(float hp);
String urlDecode(String str);

void IRAM_ATTR handleButtonInterrupt() {
  unsigned long now = millis();
  if (now - lastButtonMs < DEBOUNCE_MS) return;
  lastButtonMs = now;

  // If notification is active, dismiss it
  if (isNotificationActive) {
    isNotificationActive = false;
    // Don't return here; allow user to cycle screens if already unlocked
    // But if locked, they stay locked.
  }

  if (isRagabRecognized) {
    screenIndex++;
    screenIndex %= 4;
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(5, 5);
  tft.print("System Init...");

  tft.setCursor(5, 15);
  tft.print("Connecting WiFi...");
  WiFi.begin(ssid, password);
  int counter = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    counter++;
    if (counter > 15) {
      tft.print("Skip");
      break;
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    tft.setCursor(5, 25);
    tft.print("Time Synced");

    // Show IP Address for Linking
    serial.print("IP: ");
    serial.println(WiFi.localIP());
  }

  server.begin();
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleButtonInterrupt, FALLING);
  Wire.begin(21, 22);

  if (!mpu.begin()) Serial.println("MPU Fail");
  mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    tft.print("Sensor Fail");
    while (1)
      ;
  }

  // Sensor Config
  byte ledBrightness = 0x3F;
  byte sampleAverage = 4;
  byte ledMode = 2;
  int sampleRate = 100;
  int pulseWidth = 411;
  int adcRange = 4096;
  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
  particleSensor.enableDIETEMPRDY();  // Enable internal temp sensor

  memset(accelBuffer, 0, sizeof(accelBuffer));
  memset(spo2GraphBuffer, 0, sizeof(spo2GraphBuffer));
  memset(hrGraphBuffer, 0, sizeof(hrGraphBuffer));

  tft.fillScreen(ST77XX_BLACK);
  drawAllScreens();
}

void loop() {
  unsigned long now = millis();

  // --- WiFi Command Listener ---
  if (server.hasClient()) {
    if (client && client.connected()) client.stop();
    client = server.available();
  }

  if (client && client.connected() && client.available()) {
    String line = client.readStringUntil('\n');
    line.trim();  // Remove any \r or whitespace

    // --- CASE 1: UNLOCK COMMAND (From Python "ok" or Browser "GET /ok") ---
    if (line.equalsIgnoreCase("ok") || line.indexOf("GET /ok") >= 0) {
      isRagabRecognized = true;
      recognitionTimeout = now + RECOGNITION_DURATION_MS;
      
      // If a notification was blocking the view, clear it so we see the dashboard immediately
      isNotificationActive = false; 
      
      drawAllScreens();  // Force immediate update
    } 
    // --- CASE 2: NOTIFICATION (From MacroDroid "GET /NOTIFY/...") ---
    else if (line.indexOf("GET /NOTIFY/") >= 0) {
      int startIdx = line.indexOf("/NOTIFY/") + 8;
      int endIdx = line.indexOf(' ', startIdx);
      if (endIdx == -1) endIdx = line.length();

      if (endIdx > startIdx) {
        String encodedMsg = line.substring(startIdx, endIdx);
        notificationMsg = urlDecode(encodedMsg);
        
        // ACTIVATE NOTIFICATION ONLY
        isNotificationActive = true;
        notificationTimeout = now + NOTIF_DURATION;

        // ** CRITICAL CHANGE: WE DO NOT SET isRagabRecognized = true HERE **
        // The watch remains locked (or unlocked) in its previous state.
        
        updateScreen(); // This will overlay the notification on top
      }
    }

    // Send HTTP Response
    client.println("HTTP/1.1 200 OK");
    client.println("Connection: close");
    client.println();
    delay(10);
    client.stop();
  }

  // --- Timeout Checks ---
  if (isRagabRecognized && now > recognitionTimeout) {
    isRagabRecognized = false;
    screenIndex = 0;  // Return to lock screen
    drawAllScreens();
  }
  
  if (isNotificationActive && now > notificationTimeout) {
    isNotificationActive = false;
    // When notification expires, screen automatically reverts to Lock or Dashboard
    // based on isRagabRecognized state in next loop
    drawAllScreens(); 
  }

  // --- Sensor Logic ---
  if (now - lastSampleMs >= SAMPLE_INTERVAL) {
    lastSampleMs = now;

    // Step Logic
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    float mag = sqrt(a.acceleration.x * a.acceleration.x + a.acceleration.y * a.acceleration.y + a.acceleration.z * a.acceleration.z);
    lp_mag = LP_ALPHA * lp_mag + (1.0f - LP_ALPHA) * mag;
    float hp = mag - lp_mag;
    detectStep(hp);
    accelBuffer[accelIdx] = hp;
    accelIdx = (accelIdx + 1) % ACCEL_BUF_LEN;

    // Pulse Logic
    if (particleSensor.available()) {
      uint32_t irValue = particleSensor.getIR();
      uint32_t redValue = particleSensor.getRed();
      particleSensor.nextSample();

      if (irValue < 50000) {
        fingerDetected = false;
        beatAvg = 0;
        validBPM = 0;
        maxim_spo2 = 0;
        spo2_sample_idx = 0;
        lastIrValue = irValue;
      } else {
        fingerDetected = true;
        if (irValue > beatThreshold && lastIrValue < beatThreshold) {
          long delta = millis() - lastBeat;
          if (delta > 200) {
            lastBeat = millis();
            float beatsPerMinute = 60.0f / (delta / 1000.0f);
            if (beatsPerMinute < 255 && beatsPerMinute > 20) {
              rates[rateSpot++] = (byte)beatsPerMinute;
              rateSpot %= RATE_SIZE;
              beatAvg = 0;
              for (byte x = 0; x < RATE_SIZE; x++) beatAvg += rates[x];
              beatAvg /= RATE_SIZE;
            }
          }
        }
        lastIrValue = (long)irValue;

        irBuffer[spo2_sample_idx] = irValue;
        redBuffer[spo2_sample_idx] = redValue;
        spo2_sample_idx++;
        if (spo2_sample_idx >= SPO2_BUFFER_SIZE) {
          maxim_heart_rate_and_oxygen_saturation(irBuffer, SPO2_BUFFER_SIZE, redBuffer, &maxim_spo2, &maxim_spo2_valid, &maxim_hr, &maxim_hr_valid);
          for (int i = 25; i < 100; i++) {
            irBuffer[i - 25] = irBuffer[i];
            redBuffer[i - 25] = redBuffer[i];
          }
          spo2_sample_idx = 75;
        }
      }
    } else {
      particleSensor.check();
    }

    static unsigned long lastDraw = 0;
    if (now - lastDraw >= 100) {
      lastDraw = now;
      updateScreen();
    }
  }
}

// --- HELPER: URL Decoder ---
String urlDecode(String str) {
  String encodedString = str;
  String decodedString = "";
  char c;
  char code0;
  char code1;
  for (int i = 0; i < encodedString.length(); i++) {
    c = encodedString.charAt(i);
    if (c == '+') {
      decodedString += ' ';
    } else if (c == '%') {
      i++;
      code0 = encodedString.charAt(i);
      i++;
      code1 = encodedString.charAt(i);
      c = (h2int(code0) << 4) | h2int(code1);
      decodedString += c;
    } else {
      decodedString += c;
    }
  }
  return decodedString;
}

unsigned char h2int(char c) {
  if (c >= '0' && c <= '9') {
    return ((unsigned char)c - '0');
  }
  if (c >= 'a' && c <= 'f') {
    return ((unsigned char)c - 'a' + 10);
  }
  if (c >= 'A' && c <= 'F') {
    return ((unsigned char)c - 'A' + 10);
  }
  return (0);
}

void detectStep(float hp) {
  static bool above = false;
  static float peak = 0.0f;
  static unsigned long peakStart = 0;
  const float THRESH = 1.2f;
  if (hp > THRESH) {
    if (!above) {
      above = true;
      peak = hp;
      peakStart = millis();
    } else if (hp > peak) peak = hp;
  } else {
    if (above) {
      unsigned long duration = millis() - peakStart;
      if (duration > 80 && duration < 400 && (millis() - lastStepTime) > 350) {
        stepCount++;
        lastStepTime = millis();
      }
    }
    above = false;
  }
}

void updateScreen() {
  // 1. PRIORITY: Notification Screen (Overlays everything)
  if (isNotificationActive) {
    drawNotificationScreen();
    return;
  }

  // 2. CHECK: If locked, show Lock Screen
  if (!isRagabRecognized) {
    drawLockScreen();
    return;
  }

  // 3. IF UNLOCKED: Show Dashboard Screens
  if (beatAvg > 0) validBPM = beatAvg;
  else if (maxim_hr_valid && maxim_hr > 0) validBPM = maxim_hr;
  else validBPM = 0;

  spo2GraphBuffer[spo2GraphIdx] = (fingerDetected && maxim_spo2_valid) ? (float)maxim_spo2 : 0;
  spo2GraphIdx = (spo2GraphIdx + 1) % GRAPH_WIDTH;
  hrGraphBuffer[hrGraphIdx] = (fingerDetected) ? (float)validBPM : 0;
  hrGraphIdx = (hrGraphIdx + 1) % GRAPH_WIDTH;

  if (screenIndex == 0) drawMPUScreen();
  else if (screenIndex == 1) drawSpO2Screen();
  else if (screenIndex == 2) drawHRscreen();
  else drawWatchScreen();
}

void drawAllScreens() {
  updateScreen();
}

void drawNotificationScreen() {
  tft.fillScreen(ST77XX_BLUE);
  tft.setCursor(5, 10);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.print("NOTIFY");

  tft.setTextSize(1);
  tft.setCursor(5, 40);
  tft.print(notificationMsg);

  // Alert Bar
  if ((millis() / 500) % 2 == 0) {
    tft.fillRect(0, 150, 128, 10, ST77XX_YELLOW);
  }
}

void drawWatchScreen() {
  tft.fillScreen(ST77XX_BLACK);
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    tft.setCursor(10, 60);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_RED);
    tft.print("Syncing Time...");
    return;
  }

  tft.setCursor(20, 30);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(1);
  tft.printf("%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);

  tft.setCursor(10, 55);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(3);
  tft.printf("%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
  tft.setCursor(45, 90);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_YELLOW);
  tft.printf(":%02d", timeinfo.tm_sec);

  tft.setCursor(25, 120);
  tft.setTextColor(ST77XX_ORANGE);
  tft.setTextSize(2);
  float tempC = particleSensor.readTemperature();
  tft.print(tempC, 1);
  tft.print(" C");
}

void drawLockScreen() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(10, 50);
  tft.setTextColor(ST77XX_RED);
  tft.setTextSize(3);
  tft.print("LOCKED");
  tft.setTextSize(1);
  tft.setCursor(10, 100);
  tft.setTextColor(ST77XX_YELLOW);
  tft.print("Waiting for Face ID...");
}

void drawMPUScreen() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(2, 5);
  tft.print("StepsCount");
  tft.setCursor(2, 25);
  tft.print(stepCount);
  tft.drawRect(GRAPH_X - 1, GRAPH_Y - 1, GRAPH_WIDTH + 2, GRAPH_HEIGHT + 2, ST77XX_WHITE);

  float minv = 1000, maxv = -1000;
  for (int i = 0; i < GRAPH_WIDTH; i++) {
    if (accelBuffer[i] < minv) minv = accelBuffer[i];
    if (accelBuffer[i] > maxv) maxv = accelBuffer[i];
  }
  if (maxv == minv) maxv += 1;
  for (int i = 0; i < GRAPH_WIDTH; i++) {
    int idx = (accelIdx + i) % ACCEL_BUF_LEN;
    float val = accelBuffer[idx];
    float norm = (val - minv) / (maxv - minv);
    int y = GRAPH_Y + GRAPH_HEIGHT - (int)(norm * GRAPH_HEIGHT);
    tft.drawPixel(GRAPH_X + i, y, ST77XX_GREEN);
  }
}

void drawSpO2Screen() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(2, 5);
  tft.print("SpO2 Level");
  tft.setCursor(2, 25);
  if (fingerDetected && maxim_spo2_valid && maxim_spo2 > 0) {
    tft.print(maxim_spo2);
    tft.print("%");
  } else {
    tft.setTextColor(ST77XX_RED);
    if (!fingerDetected) tft.print("No Finger");
    else tft.print("Loading..");
  }
  tft.drawRect(GRAPH_X - 1, GRAPH_Y - 1, GRAPH_WIDTH + 2, GRAPH_HEIGHT + 2, ST77XX_WHITE);
  for (int i = 0; i < GRAPH_WIDTH; i++) {
    int idx = (spo2GraphIdx + i) % GRAPH_WIDTH;
    float val = spo2GraphBuffer[idx];
    float norm = (val - 80) / 20.0f;
    if (norm < 0) norm = 0;
    if (norm > 1) norm = 1;
    int y = GRAPH_Y + GRAPH_HEIGHT - (int)(norm * GRAPH_HEIGHT);
    if (val > 0) tft.drawPixel(GRAPH_X + i, y, ST77XX_CYAN);
  }
}

void drawHRscreen() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(2, 5);
  tft.print("Heart Rate");
  tft.setCursor(2, 25);
  if (fingerDetected && validBPM > 0) {
    tft.print(validBPM);
    tft.print(" BPM");
  } else {
    tft.setTextColor(ST77XX_RED);
    if (!fingerDetected) tft.print("No Finger");
    else tft.print("Reading..");
  }
  tft.drawRect(GRAPH_X - 1, GRAPH_Y - 1, GRAPH_WIDTH + 2, GRAPH_HEIGHT + 2, ST77XX_WHITE);
  for (int i = 0; i < GRAPH_WIDTH; i++) {
    int idx = (hrGraphIdx + i) % GRAPH_WIDTH;
    float val = hrGraphBuffer[idx];
    float norm = (val - 40) / 110.0f;
    if (norm < 0) norm = 0;
    if (norm > 1) norm = 1;
    int y = GRAPH_Y + GRAPH_HEIGHT - (int)(norm * GRAPH_HEIGHT);
    if (val > 0) tft.drawPixel(GRAPH_X + i, y, ST77XX_RED);
  }
}
