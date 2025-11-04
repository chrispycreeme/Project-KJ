// Ultra-aggressive IRAM optimization
#pragma GCC optimize ("Os")

// Disable all IRAM attributes
#define IRAM_ATTR
#define ICACHE_RAM_ATTR

#include <WiFi.h>
#include "esp_camera.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "board_config.h"

// Configuration
const char *ssid = "Familia";
const char *password = "#Jeuel1317";
const char *geminiApiKey = "AIzaSyB-7fuqQgJlRIOa4TlflXRNq9TLSW2qqWw";
const char *geminiModel = "gemini-2.5-flash-lite";

// BLE removed: using wired Serial1 to communicate with MainSystem

const int IR_SENSOR_PIN = 14;
const bool IR_ACTIVE_LOW = true;
const unsigned long IR_COOLDOWN_MS = 5000;
const framesize_t FRAME_SIZE = FRAMESIZE_VGA;  // Reduced to VGA (640x480)

// Global state
// (BLE removed) no BLE client state
unsigned long lastTrigger = 0;
int lastIrState = IR_ACTIVE_LOW ? HIGH : LOW;

// Base64 encoding table in PROGMEM to save RAM
const char b64_table[] PROGMEM = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// ----- Serial wiring to MainSystem (wired UART) -----
// Wire CAMERA TX -> MAIN RX, CAMERA RX -> MAIN TX, and common GND.
const int CAMERA_SERIAL_RX = 16; // receives from Main TX
const int CAMERA_SERIAL_TX = 17; // transmits to Main RX
const unsigned long IPC_BAUD = 115200;

// Minimal base64 encoder
String b64(const uint8_t *d, size_t len) {
  if (!d || !len) return "";
  String out;
  out.reserve(((len + 2) / 3) * 4);
  
  for (size_t i = 0; i < len; i += 3) {
    uint32_t v = (uint32_t)d[i] << 16;
    if (i + 1 < len) v |= (uint32_t)d[i + 1] << 8;
    if (i + 2 < len) v |= (uint32_t)d[i + 2];
    
    out += (char)pgm_read_byte(&b64_table[(v >> 18) & 0x3F]);
    out += (char)pgm_read_byte(&b64_table[(v >> 12) & 0x3F]);
    out += (i + 1 < len) ? (char)pgm_read_byte(&b64_table[(v >> 6) & 0x3F]) : '=';
    out += (i + 2 < len) ? (char)pgm_read_byte(&b64_table[v & 0x3F]) : '=';
  }
  return out;
}

// (BLE removed) no BLE helper functions

// Gemini detection
bool detect() {
  // Quick warmup
  for (int i = 0; i < 2; i++) {
    camera_fb_t *t = esp_camera_fb_get();
    if (t) esp_camera_fb_return(t);
    delay(30);
  }
  
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb || fb->len < 100) {
    if (fb) esp_camera_fb_return(fb);
    return false;
  }
  
  String enc = b64(fb->buf, fb->len);
  size_t flen = fb->len;
  esp_camera_fb_return(fb);
  
  if (enc.length() == 0) return false;
  
  // Build minimal JSON
  String json = "{\"contents\":[{\"parts\":[{\"text\":\"Rat? JSON: {\\\"rat_detected\\\":bool,\\\"confidence\\\":float}\"},";
  json += "{\"inline_data\":{\"mime_type\":\"image/jpeg\",\"data\":\"";
  json += enc;
  json += "\"}}]}]}";
  
  WiFiClientSecure cli;
  cli.setInsecure();
  HTTPClient http;
  
  String url = "https://generativelanguage.googleapis.com/v1beta/models/";
  url += geminiModel;
  url += ":generateContent?key=";
  url += geminiApiKey;
  
  if (!http.begin(cli, url)) return false;
  
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(json);
  
  if (code <= 0) {
    http.end();
    return false;
  }
  
  String resp = http.getString();
  http.end();
  
  if (code != 200) return false;
  
  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, resp) != DeserializationError::Ok) return false;
  
  JsonArray cand = doc["candidates"];
  if (cand.isNull()) return false;
  
  for (JsonVariant c : cand) {
    JsonArray parts = c["content"]["parts"];
    if (parts.isNull()) continue;
    for (JsonVariant p : parts) {
      if (p.containsKey("text")) {
        String txt = p["text"].as<String>();
        
        DynamicJsonDocument res(512);
        if (deserializeJson(res, txt) == DeserializationError::Ok) {
          bool rat = res["rat_detected"] | false;
          if (rat) {
            Serial.println("RAT DETECTED!");
            // Send a small JSON payload over UART to the MainSystem
            String payload = "{";
            payload += "\"rat_detected\":true,";
            payload += "\"confidence\":1.00"; // replace with actual confidence if available
            payload += "}";
            if (Serial1) {
              Serial1.println(payload);
              Serial.println("Sent IPC to MainSystem: " + payload);
            }
            // sent over Serial1 to MainSystem; no BLE fallback configured
            return true;
          }
        }
      }
    }
  }
  
  return true;
}

void setup() {
  Serial.begin(115200);
  // Wired IPC to MainSystem
  Serial1.begin(IPC_BAUD, SERIAL_8N1, CAMERA_SERIAL_RX, CAMERA_SERIAL_TX);
  Serial.println("Serial1 (to MainSystem) initialized at 115200");
  delay(100);
  
  // Camera init
  camera_config_t cfg;
  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.ledc_timer = LEDC_TIMER_0;
  cfg.pin_d0 = Y2_GPIO_NUM;
  cfg.pin_d1 = Y3_GPIO_NUM;
  cfg.pin_d2 = Y4_GPIO_NUM;
  cfg.pin_d3 = Y5_GPIO_NUM;
  cfg.pin_d4 = Y6_GPIO_NUM;
  cfg.pin_d5 = Y7_GPIO_NUM;
  cfg.pin_d6 = Y8_GPIO_NUM;
  cfg.pin_d7 = Y9_GPIO_NUM;
  cfg.pin_xclk = XCLK_GPIO_NUM;
  cfg.pin_pclk = PCLK_GPIO_NUM;
  cfg.pin_vsync = VSYNC_GPIO_NUM;
  cfg.pin_href = HREF_GPIO_NUM;
  cfg.pin_sccb_sda = SIOD_GPIO_NUM;
  cfg.pin_sccb_scl = SIOC_GPIO_NUM;
  cfg.pin_pwdn = PWDN_GPIO_NUM;
  cfg.pin_reset = RESET_GPIO_NUM;
  cfg.xclk_freq_hz = 20000000;
  cfg.frame_size = FRAME_SIZE;
  cfg.pixel_format = PIXFORMAT_JPEG;
  cfg.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  cfg.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  cfg.jpeg_quality = 10;
  cfg.fb_count = 1;
  
  if (psramFound()) {
    cfg.jpeg_quality = 8;
    cfg.fb_count = 2;
  }
  
  if (esp_camera_init(&cfg) != ESP_OK) {
    Serial.println("Camera FAIL");
    while(1) delay(1000);
  }
  
  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 1);
    s->set_contrast(s, 1);
  }
  
  // WiFi
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries++ < 20) {
    delay(500);
  }
  
  Serial.println(WiFi.status() == WL_CONNECTED ? "WiFi OK" : "WiFi FAIL");
  
  // (BLE removed) using wired Serial1 for IPC to MainSystem
  
  // IR
  pinMode(IR_SENSOR_PIN, IR_ACTIVE_LOW ? INPUT_PULLUP : INPUT);
  lastIrState = digitalRead(IR_SENSOR_PIN);
  
  Serial.println("READY");
}

void loop() {
  int cur = digitalRead(IR_SENSOR_PIN);
  bool act = IR_ACTIVE_LOW ? (cur == LOW) : (cur == HIGH);
  bool was = IR_ACTIVE_LOW ? (lastIrState == LOW) : (lastIrState == HIGH);
  unsigned long now = millis();
  
  if (act && !was && (now - lastTrigger >= IR_COOLDOWN_MS)) {
    Serial.println("TRIGGER");
    detect();
    lastTrigger = now;
  }
  
  lastIrState = cur;
  delay(50);
}