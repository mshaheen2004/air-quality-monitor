// Copyright (c) 2026 Jobit Joseph, Circuit Digest
// SPDX-License-Identifier: MIT
// =====================================================================
// ESP32 + SPS30 (UART) + ILI9341 + CircuitDigestCloud + WhatsApp Alert
// 2x5 Dashboard (PORTRAIT MODE) + Dual-batch MQTT + Indian CPCB AQI
// Extended 5-Second Soft-Start, Brownout Handling & Auto-Reset on Wi-Fi Loss
// =====================================================================

#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>
#include <SensirionUartSps30.h>
#include <CircuitDigestCloud.h>
#include <WiFiClientSecure.h>

// ESP32 System Headers for Brownout Detector Configuration
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ── Fill in your credentials ────────────────────────────────────────────────
#define WIFI_SSID      "your-Wi-Fi SSID"       // your Wi-Fi SSID
#define WIFI_PASS      "your-Wi-Fi password"   // your Wi-Fi password
#define DEVICE_ID      "your-device-id"        // Physical Device ID
#define CONNECTION_KEY "your-connection-key"   // Connection Key
#define API_KEY        "your-api-key"          // API Key

// WhatsApp Notification Settings
#define WHATSAPP_PHONE     "your-registered-phone-number" // Linked phone number with country code
#define AQI_POOR_THRESHOLD 200.0f                         // CPCB limit for POOR Air Quality (AQI > 200)

// Cloud Variable Keys
#define KEY_PM1_0_MASS "analog-input-1"
#define KEY_PM2_5_MASS "analog-input-2"
#define KEY_PM4_0_MASS "analog-input-3"
#define KEY_PM10_MASS  "analog-input-4"
#define KEY_PM0_5_NUM  "analog-input-5"
#define KEY_PM1_0_NUM  "analog-input-6"
#define KEY_PM2_5_NUM  "analog-input-7"
#define KEY_PM4_0_NUM  "analog-input-8"
#define KEY_PM10_NUM   "analog-input-9"
#define KEY_AQI        "analog-input-10" // Indian CPCB AQI
// ────────────────────────────────────────────────────────────────────────────

#define SENSOR_SERIAL_INTERFACE Serial2   // ESP32 HW Serial2 -> RX16 / TX17

#ifdef NO_ERROR
#undef NO_ERROR
#endif
#define NO_ERROR 0

CircuitDigestCloud CDcloud;
SensirionUartSps30 sensor;
static char errorMessage[64];
static int16_t error;

// Flag to track alert state and prevent spamming WhatsApp messages
static bool whatsappAlertTriggered = false;

// --- TFT Pin Configurations ---
#define TFT_CS   2   // GPIO 2
#define TFT_DC   5   // GPIO 5
#define TFT_RST  4   // GPIO 4
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

// --- Layout: 2 columns x 5 rows = 10 boxes, 240x320 Portrait ---
const int boxW = 120; // 2 cols * 120 = 240px width
const int boxH = 64;  // 5 rows * 64  = 320px height
const int cols = 2;
const int rows = 5;

struct ParamBox {
  const char* label;
  const char* unit;
  float threshold;
  bool  isCPCB;
};

ParamBox params[10] = {
  { "PM1.0 MASS", "ug/m3",  30.0,  false },
  { "PM2.5 MASS", "ug/m3",  60.0,  true  },
  { "PM4.0 MASS", "ug/m3",  80.0,  false },
  { "PM10 MASS",  "ug/m3",  100.0, true  },
  { "PM0.5 NUM",  "#/cm3",  1000.0,false },
  { "PM1.0 NUM",  "#/cm3",  800.0, false },
  { "PM2.5 NUM",  "#/cm3",  700.0, false },
  { "PM4.0 NUM",  "#/cm3",  500.0, false },
  { "PM10 NUM",   "#/cm3",  400.0, false },
  { "CPCB AQI",  "",       100.0, true  }
};

void drawDashboardGrid();
void updateBox(int i, float value);
void sendWhatsAppAlert(float aqiValue);
float calculateAQI_CPCB(float pm25, float pm10);

void setup() {
  // 1. Disable brownout detector to prevent rebooting on battery voltage dips
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(2000); // Initial 2s delay for power supply voltage stabilization

  // --- Step 1: Initialize Display ---
  Serial.println("[Boot Step 1/3] Starting TFT Display...");
  tft.begin();
  tft.setRotation(0);             // Portrait mode, 240x320
  tft.fillScreen(ILI9341_BLACK);
  drawDashboardGrid();

  Serial.println("Waiting 5 seconds for display power stabilization...");
  delay(5000); // ⏳ 5-second pause to isolate display power draw

  // --- Step 2: Initialize SPS30 Sensor ---
  Serial.println("[Boot Step 2/3] Starting SPS30 Sensor...");
  SENSOR_SERIAL_INTERFACE.begin(115200, SERIAL_8N1, 16, 17);
  sensor.begin(SENSOR_SERIAL_INTERFACE);

  sensor.stopMeasurement();

  int8_t serialNumber[32] = {0};
  error = sensor.readSerialNumber(serialNumber, 32);
  if (error != NO_ERROR) {
    Serial.print("Error reading serial number: ");
    errorToString(error, errorMessage, sizeof errorMessage);
    Serial.println(errorMessage);
  }

  error = sensor.startMeasurement(SPS30_OUTPUT_FORMAT_OUTPUT_FORMAT_FLOAT);
  if (error != NO_ERROR) {
    Serial.print("Error starting measurement: ");
    errorToString(error, errorMessage, sizeof errorMessage);
    Serial.println(errorMessage);
  } else {
    Serial.println("SPS30 Measurement started successfully.");
  }

  Serial.println("Waiting 5 seconds for SPS30 internal fan to stabilize...");
  delay(5000); // ⏳ 5-second pause to let fan motor reach full operational speed

  // --- Step 3: Lower Wi-Fi TX Power & Connect to Cloud ---
  Serial.println("[Boot Step 3/3] Turning on Wi-Fi & Connecting to Cloud...");
  
  // Lower Wi-Fi transmit power to prevent voltage sags on battery power
  WiFi.setTxPower(WIFI_POWER_15dBm); 

  if (!CDcloud.begin(WIFI_SSID, WIFI_PASS, DEVICE_ID, CONNECTION_KEY, API_KEY)) {
    Serial.println("CDcloud begin() failed — check credentials. Rebooting...");
    delay(2000);
    ESP.restart(); // Reset system if initial cloud connection fails
  } else {
    Serial.println("CDcloud initialized successfully.");
  }

  Serial.println("Boot sequence complete. Entering main loop.");
}

void loop() {
  // Drive WiFi reconnect, MQTT connection, and auto-heartbeat
  CDcloud.loop();

  // ── Wi-Fi Disconnection & Full System Reboot Handler ───────────────────────
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ Wi-Fi Disconnected!");
    Serial.println("Shutting down sensor and restarting system to execute staged soft-start...");

    // Stop SPS30 fan before resetting system
    sensor.stopMeasurement();

    // 2-second delay to settle power grid before rebooting
    delay(2000);

    // Trigger full ESP32 microcontroller software reboot
    ESP.restart();
  }
  // ──────────────────────────────────────────────────────────────────────────

  static uint32_t lastPublish = 0;
  if (millis() - lastPublish >= 5000) { // Update every 5 seconds
    lastPublish = millis();

    float mc1p0 = 0, mc2p5 = 0, mc4p0 = 0, mc10p0 = 0;
    float nc0p5 = 0, nc1p0 = 0, nc2p5 = 0, nc4p0 = 0, nc10p0 = 0;
    float typicalParticleSize = 0;

    error = sensor.readMeasurementValuesFloat(mc1p0, mc2p5, mc4p0, mc10p0,
                                               nc0p5, nc1p0, nc2p5, nc4p0,
                                               nc10p0, typicalParticleSize);

    if (error != NO_ERROR) {
      Serial.print("Error reading SPS30 values: ");
      errorToString(error, errorMessage, sizeof errorMessage);
      Serial.println(errorMessage);
      return;
    }

    // Compute Indian CPCB AQI based on PM2.5 and PM10
    float cpcbAqi = calculateAQI_CPCB(mc2p5, mc10p0);

    float values[10] = {
      mc1p0, mc2p5, mc4p0, mc10p0,
      nc0p5, nc1p0, nc2p5, nc4p0, nc10p0,
      cpcbAqi
    };

    // 1. Refresh local TFT screen boxes
    for (int i = 0; i < 10; i++) {
      updateBox(i, values[i]);
    }

    // 2. Publish BATCH 1 (5 Parameters)
    CDcloud.publish({
      {KEY_PM1_0_MASS, mc1p0},
      {KEY_PM2_5_MASS, mc2p5},
      {KEY_PM4_0_MASS, mc4p0},
      {KEY_PM10_MASS,  mc10p0},
      {KEY_PM0_5_NUM,  nc0p5}
    });

    delay(200);

    // 3. Publish BATCH 2 (5 Parameters)
    CDcloud.publish({
      {KEY_PM1_0_NUM,  nc1p0},
      {KEY_PM2_5_NUM,  nc2p5},
      {KEY_PM4_0_NUM,  nc4p0},
      {KEY_PM10_NUM,   nc10p0},
      {KEY_AQI,        cpcbAqi}
    });

    Serial.println("SPS30 metrics published to Cloud.");

    // 4. Check CPCB AQI Threshold for WhatsApp Alert (Poor Category > 200)
    if (cpcbAqi > AQI_POOR_THRESHOLD) {
      if (!whatsappAlertTriggered) {
        Serial.println("⚠️ CPCB AQI in POOR Category (>200)! Triggering WhatsApp Alert...");
        sendWhatsAppAlert(cpcbAqi);
        whatsappAlertTriggered = true; // Lock alert until AQI recovers below 200
      }
    } else {
      // Reset alert state when AQI drops back to Safe/Moderate range (<= 200)
      whatsappAlertTriggered = false;
    }
  }
}

// ---------------------------------------------------------------
// Indian CPCB AQI Calculation Sub-functions
// ---------------------------------------------------------------
float getSubIndex_PM25(float pm25) {
  if (pm25 <= 0.0f)   return 0.0f;
  if (pm25 <= 30.0f)  return (50.0f / 30.0f) * pm25;
  if (pm25 <= 60.0f)  return 51.0f + ((100.0f - 51.0f) / (60.0f - 30.0f)) * (pm25 - 30.0f);
  if (pm25 <= 90.0f)  return 101.0f + ((200.0f - 101.0f) / (90.0f - 60.0f)) * (pm25 - 60.0f);
  if (pm25 <= 120.0f) return 201.0f + ((300.0f - 201.0f) / (120.0f - 90.0f)) * (pm25 - 90.0f);
  if (pm25 <= 250.0f) return 301.0f + ((400.0f - 301.0f) / (250.0f - 120.0f)) * (pm25 - 120.0f);
  return 401.0f + ((500.0f - 401.0f) / (150.0f)) * (pm25 - 250.0f);
}

float getSubIndex_PM10(float pm10) {
  if (pm10 <= 0.0f)   return 0.0f;
  if (pm10 <= 50.0f)  return (50.0f / 50.0f) * pm10;
  if (pm10 <= 100.0f) return 51.0f + ((100.0f - 51.0f) / (100.0f - 50.0f)) * (pm10 - 50.0f);
  if (pm10 <= 250.0f) return 101.0f + ((200.0f - 101.0f) / (250.0f - 100.0f)) * (pm10 - 100.0f);
  if (pm10 <= 350.0f) return 201.0f + ((300.0f - 201.0f) / (350.0f - 250.0f)) * (pm10 - 250.0f);
  if (pm10 <= 430.0f) return 301.0f + ((400.0f - 301.0f) / (430.0f - 350.0f)) * (pm10 - 430.0f);
  return 401.0f + ((500.0f - 401.0f) / (70.0f)) * (pm10 - 430.0f);
}

float calculateAQI_CPCB(float pm25, float pm10) {
  float subPM25 = getSubIndex_PM25(pm25);
  float subPM10 = getSubIndex_PM10(pm10);
  return (subPM25 > subPM10) ? subPM25 : subPM10;
}

// ---------------------------------------------------------------
// Function to send WhatsApp HTTP REST API alert
// ---------------------------------------------------------------
void sendWhatsAppAlert(float aqiValue) {
  WiFiClientSecure client;
  client.setInsecure(); // Bypass SSL certificate verification

  if (!client.connect("www.circuitdigest.cloud", 443)) {
    Serial.println("WhatsApp connection failed!");
    return;
  }

  String payload =
    "{\"phone_number\":\"" + String(WHATSAPP_PHONE) + "\","
    "\"template_id\":\"threshold_violation_alert\","
    "\"variables\":{"
    "\"device_name\":\"ESP32 Air Monitor\","
    "\"parameter\":\"CPCB AQI\","
    "\"measured_value\":\"" + String((int)aqiValue) + "\","
    "\"limit\":\"200 (POOR Category)\","
    "\"location\":\"Indoor Room\"}}";

  client.println("POST /api/v1/whatsapp/send HTTP/1.1");
  client.println("Host: www.circuitdigest.cloud");
  client.println("X-API-Key: " + String(API_KEY));
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.print("Content-Length: ");
  client.println(payload.length());
  client.println();
  client.print(payload);

  Serial.println("WhatsApp API Payload Sent. Server Response:");
  while (client.connected() || client.available()) {
    if (client.available()) {
      String line = client.readStringUntil('\n');
      Serial.println(line);
    }
  }
  client.stop();
}

// ---------------------------------------------------------------
// Draw static 2x5 grid dashboard (Portrait Layout)
// ---------------------------------------------------------------
void drawDashboardGrid() {
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(1);
  for (int i = 0; i < 10; i++) {
    int col = i % cols;
    int row = i / cols;
    int x = col * boxW;
    int y = row * boxH;

    tft.drawRect(x, y, boxW, boxH, ILI9341_DARKGREY);
    tft.setCursor(x + 5, y + 4);
    tft.print(params[i].label);
  }
}

// ---------------------------------------------------------------
// Update content area of one dashboard slot (Portrait Layout)
// ---------------------------------------------------------------
void updateBox(int i, float value) {
  int col = i % cols;
  int row = i / cols;
  int x = col * boxW;
  int y = row * boxH;

  // Clear value and status area inside the box
  tft.fillRect(x + 2, y + 16, boxW - 4, boxH - 18, ILI9341_BLACK);

  bool hasThreshold = params[i].threshold > 0;
  bool safe = true;
  uint16_t valColor = ILI9341_CYAN;

  if (hasThreshold) {
    safe = (value <= params[i].threshold);
    valColor = safe ? ILI9341_GREEN : ILI9341_RED;
  }

  // --- Value Line ---
  tft.setTextSize(2);
  tft.setTextColor(valColor);
  tft.setCursor(x + 5, y + 20);
  
  // Format AQI without decimals, keep 1 decimal for other values
  if (i == 9) {
    tft.print(value, 0);
  } else {
    tft.print(value, 1);
  }

  // --- Unit Line ---
  tft.setTextSize(1);
  tft.setCursor(x + 5, y + 38);
  tft.setTextColor(ILI9341_WHITE);
  tft.print(params[i].unit);

  // --- Status Banner Line ---
  tft.setCursor(x + 5, y + 50);

  if (!hasThreshold) {
    tft.setTextColor(ILI9341_YELLOW);
    tft.print("INFO ONLY");
  } else if (safe) {
    tft.setTextColor(ILI9341_GREEN);
    tft.print(params[i].isCPCB ? "SAFE (CPCB)" : "SAFE");
  } else {
    tft.setTextColor(ILI9341_RED);
    tft.print(params[i].isCPCB ? "NOT SAFE(CPCB)" : "NOT SAFE");
  }
}
