#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <base64.h>

// ===== HARDCODED CREDENTIALS - CHANGE THESE =====
const char* WIFI_SSID = "ProjectKJ";
const char* WIFI_PASSWORD = "projectkj";
const char* GEMINI_API_KEY = "AIzaSyBYEZiJ_ksDS9BuGncDiZxKwhQm0ztl1A4";
// ===============================================

// UDP for discovery
WiFiUDP udpListener;
const int UDP_BEACON_PORT = 4210;

// Main ESP32 connection
String mainESP32_IP = "";

// Camera pins - ESP32-WROVER-KIT
#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM    21
#define SIOD_GPIO_NUM    26
#define SIOC_GPIO_NUM    27
#define Y9_GPIO_NUM      35
#define Y8_GPIO_NUM      34
#define Y7_GPIO_NUM      39
#define Y6_GPIO_NUM      36
#define Y5_GPIO_NUM      19
#define Y4_GPIO_NUM      18
#define Y3_GPIO_NUM       5
#define Y2_GPIO_NUM       4
#define VSYNC_GPIO_NUM   25
#define HREF_GPIO_NUM    23
#define PCLK_GPIO_NUM    22

// Detection timing
unsigned long lastDetectionTime = 0;
const unsigned long detectionInterval = 8000; // 8 seconds between detections

// Rat detection logic
int ratConsecutive = 0;
const int ratRequired = 2; // Require 2 consecutive YES

// Gemini API
const char* geminiHost = "generativelanguage.googleapis.com";
const int geminiPort = 443;
const char* geminiPath = "/v1/models/gemini-1.5-flash:generateContent";

// Connection health
int consecutiveFailures = 0;
const int MAX_FAILURES_BEFORE_REDISCOVER = 3;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== Camera ESP32 with AI Detection ===");
  Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
  
  // Connect to WiFi
  Serial.println("\n[WiFi] Connecting...");
  WiFi.setSleep(false);
  connectWiFi();
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ERROR: WiFi not connected. Restarting...");
    delay(5000);
    ESP.restart();
  }
  
  Serial.println("[WiFi] Stabilizing connection...");
  delay(2000);
  
  // Initialize camera
  Serial.println("\n[CAM] Initializing camera...");
  if (!initCamera()) {
    Serial.println("ERROR: Camera init failed");
    while(1) {
      Serial.println("Camera failed. Reset device.");
      delay(5000);
    }
  }
  Serial.printf("Free heap after camera init: %u bytes\n", ESP.getFreeHeap());
  
  // Wait for main ESP32
  Serial.println("\n[Discovery] Waiting 12 seconds for main ESP32...");
  for (int i = 12; i > 0; i--) {
    Serial.printf("%d ", i);
    delay(1000);
  }
  Serial.println();
  
  // Discover main ESP32
  discoverMainESP32();
  
  // Test Gemini API
  Serial.println("\n[API] Testing Gemini connection...");
  testGeminiConnection();
  
  Serial.println("\n=== System Ready ===");
  if (mainESP32_IP.length() > 0) {
    Serial.printf("Main ESP32: %s\n", mainESP32_IP.c_str());
  } else {
    Serial.println("Main ESP32 NOT found - will retry");
  }
  Serial.println("AI detection active\n");
}

unsigned long lastDiscoveryAttempt = 0;
const unsigned long discoveryRetryInterval = 15000;

void loop() {
  // Monitor WiFi health
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Disconnected! Reconnecting...");
    connectWiFi();
    consecutiveFailures = 0;
  }
  
  // Retry discovery if no main IP
  if (WiFi.status() == WL_CONNECTED && mainESP32_IP.length() == 0) {
    if (millis() - lastDiscoveryAttempt >= discoveryRetryInterval) {
      lastDiscoveryAttempt = millis();
      Serial.println("\n[Discovery] Retrying...");
      discoverMainESP32();
    }
  }
  
  // Run detection periodically
  if (WiFi.status() == WL_CONNECTED && mainESP32_IP.length() > 0) {
    if (millis() - lastDetectionTime >= detectionInterval) {
      lastDetectionTime = millis();
      captureAndDetectRat();
    }
  }
  
  delay(100);
}

bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;
  
  if (psramFound()) {
    Serial.println("[CAM] PSRAM found, using VGA resolution");
    config.frame_size = FRAMESIZE_VGA; // 640x480
    config.jpeg_quality = 12;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    Serial.println("[CAM] No PSRAM, using QVGA");
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 15;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }
  
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }
  
  // Optimize for rat detection
  sensor_t * s = esp_camera_sensor_get();
  s->set_brightness(s, 0);
  s->set_contrast(s, 1);
  s->set_saturation(s, 0);
  s->set_sharpness(s, 2);
  s->set_denoise(s, 1);
  s->set_hmirror(s, 0);
  s->set_vflip(s, 0);
  s->set_whitebal(s, 1);
  s->set_awb_gain(s, 1);
  s->set_wb_mode(s, 0);
  s->set_aec2(s, 1);
  s->set_ae_level(s, 0);
  s->set_aec_value(s, 300);
  
  Serial.println("[CAM] ✓ Camera initialized successfully");
  return true;
}

void connectWiFi() {
  Serial.println("\n=== WiFi Connection ===");
  Serial.printf("SSID: '%s'\n", WIFI_SSID);
  
  // Clean shutdown
  WiFi.disconnect(true, true);
  delay(500);
  WiFi.mode(WIFI_OFF);
  delay(1000);
  
  // Configure
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.setSleep(WIFI_PS_NONE);
  WiFi.setHostname("ESP32-Camera");
  delay(500);
  
  int maxAttempts = 3;
  bool connected = false;
  
  for (int attempt = 1; attempt <= maxAttempts && !connected; attempt++) {
    Serial.printf("\n[WiFi] Attempt %d/%d\n", attempt, maxAttempts);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    Serial.print("Connecting");
    for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
      delay(500);
      Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      connected = true;
      Serial.println("\n\n✓✓✓ WiFi Connected! ✓✓✓");
      Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
      Serial.printf("Signal: %d dBm\n", WiFi.RSSI());
      delay(2000);
    } else if (attempt < maxAttempts) {
      Serial.println("\n✗ Failed, resetting WiFi...");
      WiFi.disconnect(true, true);
      delay(2000);
      WiFi.mode(WIFI_OFF);
      delay(1000);
      WiFi.mode(WIFI_STA);
      WiFi.setSleep(WIFI_PS_NONE);
      delay(500);
    }
  }
  
  if (!connected) {
    Serial.println("\n✗✗✗ WiFi Failed ✗✗✗");
    delay(10000);
    ESP.restart();
  }
}

void discoverMainESP32() {
  if (mainESP32_IP.length() > 0) {
    // Verify existing IP
    WiFiClient client;
    client.setTimeout(500);
    if (client.connect(mainESP32_IP.c_str(), 80)) {
      client.stop();
      return;
    }
    Serial.println("[Discovery] Previous IP invalid, rediscovering...");
    mainESP32_IP = "";
  }
  
  Serial.println("\n=== Discovering Main ESP32 ===");
  Serial.println("[Discovery] Listening for UDP beacon...");
  
  // UDP Beacon
  udpListener.begin(UDP_BEACON_PORT);
  
  unsigned long startListen = millis();
  while (millis() - startListen < 8000) {
    int packetSize = udpListener.parsePacket();
    if (packetSize > 0) {
      char packetBuffer[64];
      int len = udpListener.read(packetBuffer, sizeof(packetBuffer) - 1);
      if (len > 0) {
        packetBuffer[len] = '\0';
        
        if (strstr(packetBuffer, "ESP32_MAIN_HERE") != NULL) {
          mainESP32_IP = udpListener.remoteIP().toString();
          Serial.printf("\n[Discovery] ✓ Found via beacon: %s\n", mainESP32_IP.c_str());
          udpListener.stop();
          consecutiveFailures = 0;
          return;
        }
      }
    }
    delay(200);
    Serial.print(".");
  }
  udpListener.stop();
  
  Serial.println("\n[Discovery] No beacon, trying IP scan...");
  
  // IP scan
  IPAddress localIP = WiFi.localIP();
  String subnet = String(localIP[0]) + "." + String(localIP[1]) + "." + String(localIP[2]) + ".";
  int cameraIP = localIP[3];
  
  Serial.print("[Discovery] Scanning nearby IPs");
  for (int offset = -15; offset <= 15; offset++) {
    int testIP = cameraIP + offset;
    if (testIP > 0 && testIP < 255 && testIP != cameraIP) {
      if (tryConnectToMain(subnet + String(testIP))) {
        consecutiveFailures = 0;
        return;
      }
    }
    if (offset % 5 == 0) Serial.print(".");
  }
  
  Serial.println("\n[Discovery] ⚠ Main ESP32 not found");
}

bool tryConnectToMain(String testIP) {
  WiFiClient client;
  client.setTimeout(200);
  
  if (!client.connect(testIP.c_str(), 80)) {
    return false;
  }
  
  client.print("GET / HTTP/1.0\r\nHost: ");
  client.print(testIP);
  client.print("\r\n\r\n");
  
  unsigned long start = millis();
  while (client.available() == 0 && millis() - start < 300) {
    delay(10);
  }
  
  if (client.available()) {
    String response = client.readString();
    client.stop();
    
    if (response.indexOf("ESP32 Main Controller") >= 0) {
      mainESP32_IP = testIP;
      Serial.printf("\n[Discovery] ✓ Found at: %s\n", mainESP32_IP.c_str());
      return true;
    }
  }
  
  client.stop();
  return false;
}

void captureAndDetectRat() {
  // Capture frame
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[Detection] Capture failed, retrying...");
    delay(200);
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("[Detection] ✗ Capture failed");
      return;
    }
  }
  
  Serial.printf("[Detection] Captured %u bytes\n", (unsigned int)fb->len);
  
  // Check heap
  size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < 40000) {
    Serial.printf("[Detection] ✗ Low heap: %u bytes\n", (unsigned int)freeHeap);
    esp_camera_fb_return(fb);
    ratConsecutive = 0;
    return;
  }
  
  // Encode to base64
  Serial.println("[Detection] Encoding to base64...");
  String b64;
  b64.reserve((fb->len * 4 / 3) + 4);
  b64 = base64::encode(fb->buf, fb->len);
  
  // Release frame buffer immediately
  esp_camera_fb_return(fb);
  
  if (b64.length() == 0) {
    Serial.println("[Detection] ✗ Base64 encoding failed");
    return;
  }
  
  Serial.printf("[Detection] Base64: %u chars\n", b64.length());
  
  // Build JSON payload
  String payload;
  payload.reserve(1024 + b64.length());
  payload = "{";
  payload += "\"contents\":[{";
  payload +=   "\"parts\":[";
  payload +=     "{\"text\":\"Is there a rat in this image? Answer YES or NO.\"},";
  payload +=     "{\"inline_data\":{";
  payload +=       "\"mime_type\":\"image/jpeg\",";
  payload +=       "\"data\":\"";
  payload +=         b64;
  payload +=       "\"}";
  payload +=     "}";
  payload +=   "]";
  payload += "}],";
  payload += "\"generationConfig\":{";
  payload +=   "\"temperature\":0.1,";
  payload +=   "\"maxOutputTokens\":5";
  payload += "}";
  payload += "}";
  
  b64 = ""; // Free memory
  
  // Call Gemini API
  HTTPClient http;
  WiFiClientSecure sslClient;
  
  sslClient.setInsecure();
  sslClient.setTimeout(25000);
  sslClient.setHandshakeTimeout(20000);
  
  String url = "https://" + String(geminiHost) + String(geminiPath) + "?key=" + String(GEMINI_API_KEY);
  
  if (!http.begin(sslClient, url)) {
    Serial.println("[Detection] ✗ HTTP begin failed");
    http.end();
    return;
  }
  
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(25000);
  
  Serial.println("[Detection] Sending to Gemini API...");
  int httpCode = http.POST(payload);
  payload = ""; // Free memory
  
  Serial.printf("[Detection] HTTP Code: %d\n", httpCode);
  
  if (httpCode == 200) {
    String response = http.getString();
    http.end();
    
    response.toUpperCase();
    
    // Parse response
    bool hasYes = response.indexOf("YES") >= 0;
    bool hasNo = response.indexOf("NO") >= 0;
    
    bool isRat = false;
    if (hasYes && !hasNo) {
      isRat = true;
    } else if (hasYes && hasNo) {
      int yesPos = response.indexOf("YES");
      int noPos = response.indexOf("NO");
      isRat = (yesPos < noPos);
    }
    
    if (isRat) {
      ratConsecutive++;
      Serial.printf("[Detection] 🐀 RAT DETECTED! (%d/%d consecutive)\n", ratConsecutive, ratRequired);
      
      if (ratConsecutive >= ratRequired) {
        Serial.println("\n[Detection] ✓✓✓ RAT CONFIRMED ✓✓✓");
        Serial.println("Notifying main ESP32...\n");
        notifyMainESP32();
        ratConsecutive = 0;
      }
    } else {
      if (ratConsecutive > 0) {
        Serial.printf("[Detection] NO RAT (streak broken from %d)\n", ratConsecutive);
      } else {
        Serial.println("[Detection] NO RAT");
      }
      ratConsecutive = 0;
    }
  } else {
    Serial.printf("[Detection] ✗ HTTP Error: %d\n", httpCode);
    http.end();
    ratConsecutive = 0;
  }
}

void notifyMainESP32() {
  if (mainESP32_IP.length() == 0) {
    Serial.println("[Notify] No main IP!");
    return;
  }
  
  HTTPClient http;
  String url = "http://" + mainESP32_IP + "/trigger";
  
  http.begin(url);
  http.setTimeout(5000);
  
  Serial.printf("[Notify] Sending trigger to %s\n", url.c_str());
  int httpCode = http.POST("");
  
  if (httpCode == 200) {
    Serial.println("[Notify] ✓ Main ESP32 triggered!");
    consecutiveFailures = 0;
  } else {
    Serial.printf("[Notify] ✗ Failed: %d\n", httpCode);
    consecutiveFailures++;
    
    if (consecutiveFailures >= MAX_FAILURES_BEFORE_REDISCOVER) {
      Serial.println("[Notify] Too many failures, rediscovering...");
      mainESP32_IP = "";
      consecutiveFailures = 0;
    }
  }
  
  http.end();
}

void testGeminiConnection() {
  Serial.println("\n=== Testing Gemini API ===");
  
  // Test DNS
  Serial.print("[1/2] DNS Resolution... ");
  IPAddress geminiIP;
  if (!WiFi.hostByName(geminiHost, geminiIP)) {
    Serial.println("✗ FAILED");
    return;
  }
  Serial.printf("✓ %s\n", geminiIP.toString().c_str());
  
  // Test API
  Serial.print("[2/2] API Test... ");
  
  HTTPClient http;
  WiFiClientSecure sslClient;
  
  sslClient.setInsecure();
  sslClient.setTimeout(15000);
  
  String testPayload = "{\"contents\":[{\"parts\":[{\"text\":\"Hi\"}]}]}";
  String url = "https://" + String(geminiHost) + String(geminiPath) + "?key=" + String(GEMINI_API_KEY);
  
  if (!http.begin(sslClient, url)) {
    Serial.println("✗ Connection failed");
    http.end();
    return;
  }
  
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.POST(testPayload);
  
  if (httpCode == 200) {
    Serial.println("✓");
    Serial.println("\n✓✓✓ Gemini API working! ✓✓✓\n");
  } else {
    Serial.printf("✗ HTTP %d\n", httpCode);
  }
  
  http.end();
}
