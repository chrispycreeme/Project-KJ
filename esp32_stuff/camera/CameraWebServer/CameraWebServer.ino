#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>  // Install ArduinoJson (6.x) via Library Manager
#include "mbedtls/base64.h"
// BLE client to send alerts to MainSystem
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// ===========================
// Select camera model in board_config.h
// ===========================
#include "board_config.h"

// ===========================
// Enter your WiFi credentials
// ===========================
const char *ssid = "bawal kumonek";
const char *password = "cdbanluta24";

// ===========================
// Gemini API configuration
// ===========================
const char *geminiApiKey = "AIzaSyB-7fuqQgJlRIOa4TlflXRNq9TLSW2qqWw";  // TODO: set via secure storage for production
const char *geminiModel = "gemini-2.5-flash-lite";

// ===========================
// IR sensor configuration
// ===========================
const int IR_SENSOR_PIN = 14;            // Adjust to match your wiring
const bool IR_ACTIVE_LOW = true;         // Set to false if sensor outputs HIGH when triggered
const unsigned long IR_COOLDOWN_MS = 5000;  // Minimum interval between detections (ms)

// ===========================
// Camera image tuning
// ===========================
const int CAMERA_BRIGHTNESS = 2;   // Range: -2 (darker) to 2 (brighter)
const int CAMERA_CONTRAST = 1;     // Range: -2 (lower contrast) to 2 (higher contrast)
const gainceiling_t CAMERA_GAIN_CEILING = GAINCEILING_64X;  // Allow higher sensor gain in low light
const bool CAMERA_ENABLE_AUTO_EXPOSURE = true;              // Leave auto exposure enabled
const bool CAMERA_ENABLE_AEC2 = true;                       // Use 2nd-order AE for better low-light response
const bool CAMERA_ENABLE_AUTO_GAIN = true;                  // Keep auto gain control enabled
const bool CAMERA_ENABLE_AUTO_WHITE_BALANCE = true;         // Maintain auto white balance
const bool CAMERA_ENABLE_AWB_GAIN = true;                   // Allow gain adjustments for white balance
const bool CAMERA_ENABLE_LENS_CORRECTION = true;            // Helps edge softness when gain is high
const int CAMERA_MANUAL_EXPOSURE = 0;                       // 0 keeps auto; otherwise 0-1200 manual exposure
const int CAMERA_WARMUP_FRAMES = 1;                         // Extra frames to discard before detection
const int CAMERA_WARMUP_DELAY_MS = 60;                      // Delay between warmup frames (ms)

// Common frame size options: FRAMESIZE_QQVGA (160x120), FRAMESIZE_QVGA (320x240),
// FRAMESIZE_VGA (640x480), FRAMESIZE_SVGA (800x600), FRAMESIZE_XGA (1024x768),
// FRAMESIZE_HD (1280x720), FRAMESIZE_UXGA (1600x1200), FRAMESIZE_FHD (1920x1080)
const framesize_t CAMERA_INIT_FRAME_SIZE = FRAMESIZE_UXGA;          // Resolution during sensor init
const framesize_t CAMERA_RUNTIME_FRAME_SIZE = FRAMESIZE_XGA;       // Resolution used after tuning
const framesize_t CAMERA_NO_PSRAM_FRAME_SIZE = FRAMESIZE_VGA;       // Fallback when PSRAM is unavailable

// ===========================
// Detection helpers forward declarations
// ===========================
bool captureAndDetectRats();
bool sendFrameToGemini(const uint8_t *buffer, size_t length, String &outSummary);
String base64Encode(const uint8_t *data, size_t length);
String buildGeminiUrl();

// ===========================
// Runtime state
// ===========================
bool gHasPsram = false;
framesize_t gActiveFrameSize = CAMERA_RUNTIME_FRAME_SIZE;

// ===========================
// IR trigger state
// ===========================
unsigned long lastIrTriggerMs = 0;
int lastIrState = IR_ACTIVE_LOW ? HIGH : LOW;

// ===========================
// Gemini integration helpers
// ===========================

static inline framesize_t minFrameSize(framesize_t lhs, framesize_t rhs) {
  return (lhs < rhs) ? lhs : rhs;
}

static void logFrameSize(const char *label, framesize_t size) {
  int index = static_cast<int>(size);
  if (index >= 0 && index < static_cast<int>(FRAMESIZE_INVALID)) {
    const resolution_info_t &info = resolution[index];
    Serial.printf("%s: %dx%d\n", label, info.width, info.height);
  } else {
    Serial.printf("%s: framesize code %d\n", label, index);
  }
}

String buildGeminiUrl() {
  String url = "https://generativelanguage.googleapis.com/v1beta/models/";
  url += geminiModel;
  url += ":generateContent?key=";
  url += geminiApiKey;
  return url;
}

String base64Encode(const uint8_t *data, size_t length) {
  if (!data || length == 0) {
    return String();
  }

  size_t outputLength = 0;
  int result = mbedtls_base64_encode(nullptr, 0, &outputLength, data, length);
  if (result != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL || outputLength == 0) {
    Serial.printf("[Gemini] Failed to measure base64 length (code %d)\n", result);
    return String();
  }

  unsigned char *buffer = static_cast<unsigned char *>(malloc(outputLength + 1));
  if (!buffer) {
    Serial.println("[Gemini] Base64 malloc failed");
    return String();
  }

  result = mbedtls_base64_encode(buffer, outputLength + 1, &outputLength, data, length);
  if (result != 0) {
    Serial.printf("[Gemini] Base64 encode failed (code %d)\n", result);
    free(buffer);
    return String();
  }

  buffer[outputLength] = '\0';
  String encoded(reinterpret_cast<char *>(buffer));
  free(buffer);

  encoded.replace("\n", "");
  encoded.replace("\r", "");
  return encoded;
}

bool sendFrameToGemini(const uint8_t *buffer, size_t length, String &outSummary) {
  outSummary = "";

  if (!geminiApiKey || strlen(geminiApiKey) == 0) {
    Serial.println("[Gemini] API key is not set");
    return false;
  }

  if (!buffer || length == 0) {
    Serial.println("[Gemini] Empty frame buffer");
    return false;
  }

  String encoded = base64Encode(buffer, length);
  if (encoded.isEmpty()) {
    Serial.println("[Gemini] Failed to base64-encode frame");
    return false;
  }

  Serial.printf("[Gemini] Base64 length: %u (raw %u bytes)\n", encoded.length(), static_cast<unsigned int>(length));

  String payload = "{";
  payload += "\"contents\":[{";
  payload += "\"parts\":[";
  payload += "{\"text\":\"You are a rat detection assistant. Given the image respond ONLY with a JSON object of the form {\\\"rat_detected\\\":bool,\\\"confidence\\\":0-1 number,\\\"notes\\\":string}.\\nDo not include markdown or prose.\"},";
  payload += "{\"inline_data\":{\"mime_type\":\"image/jpeg\",\"data\":\"";
  payload += encoded;
  payload += "\"}}";
  payload += "]}";
  payload += "]}";

  WiFiClientSecure client;
  client.setInsecure();  // TODO: load root certificate for production use

  HTTPClient https;
  String url = buildGeminiUrl();

  Serial.println("[Gemini] Sending request...");
  if (!https.begin(client, url)) {
    Serial.println("[Gemini] Unable to start HTTPS connection");
    return false;
  }

  https.addHeader("Content-Type", "application/json");
  int httpCode = https.POST(payload);

  if (httpCode <= 0) {
    Serial.printf("[Gemini] POST failed, error: %s\n", https.errorToString(httpCode).c_str());
    https.end();
    return false;
  }

  String response = https.getString();
  https.end();

  Serial.printf("[Gemini] HTTP %d\n", httpCode);

  if (httpCode != 200) {
    Serial.println("[Gemini] Request failed: " + response);
    return false;
  }

  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeJson(doc, response);
  if (err) {
    Serial.print("[Gemini] Failed to parse response JSON: ");
    Serial.println(err.c_str());
    return false;
  }

  JsonArray candidates = doc["candidates"].as<JsonArray>();
  if (candidates.isNull() || candidates.size() == 0) {
    Serial.println("[Gemini] No candidates in response");
    return false;
  }

  for (JsonVariant candidate : candidates) {
    JsonArray parts = candidate["content"]["parts"].as<JsonArray>();
    if (parts.isNull()) {
      continue;
    }
    for (JsonVariant part : parts) {
      if (part.containsKey("text")) {
        outSummary = part["text"].as<String>();
        if (!outSummary.isEmpty()) {
          return true;
        }
      }
    }
  }

  Serial.println("[Gemini] No textual response received");
  return false;
}

bool captureAndDetectRats() {
  for (int i = 0; i < CAMERA_WARMUP_FRAMES; ++i) {
    camera_fb_t *tmp = esp_camera_fb_get();
    if (tmp) {
      esp_camera_fb_return(tmp);
    }
    delay(CAMERA_WARMUP_DELAY_MS);
  }

  camera_fb_t *frame = esp_camera_fb_get();
  if (!frame) {
    Serial.println("[Camera] Failed to capture frame");
    return false;
  }

  Serial.printf("[Camera] Captured frame %dx%d (%u bytes)\n", frame->width, frame->height, (unsigned int)frame->len);

  String summary;
  bool ok = sendFrameToGemini(frame->buf, frame->len, summary);
  esp_camera_fb_return(frame);

  if (!ok) {
    Serial.println("[Detection] Gemini detection failed");
    return false;
  }

  Serial.println("[Detection] Raw Gemini reply:");
  Serial.println(summary);

  DynamicJsonDocument analysis(1024);
  if (deserializeJson(analysis, summary) == DeserializationError::Ok) {
    bool ratDetected = analysis["rat_detected"].as<bool>();
    double confidence = analysis["confidence"].as<double>();
    const char *notes = analysis["notes"].as<const char *>();

    Serial.println("[Detection] Parsed result");
    Serial.print("  rat_detected: ");
    Serial.println(ratDetected ? "true" : "false");
    Serial.print("  confidence: ");
    Serial.println(String(confidence, 2));
    if (notes) {
      Serial.print("  notes: ");
      Serial.println(notes);
    }
    // If rat detected, send BLE alert to MainSystem
    if (ratDetected) {
      // Build a small JSON payload
      String payload = "{";
      payload += "\"rat_detected\":true,";
      payload += "\"confidence\":" + String(confidence, 2) + ",";
      payload += "\"notes\":\"" + String(notes ? notes : "") + "\"";
      payload += "}";

      Serial.println("[BLE] Sending alert to MainSystem: "+ payload);
      // Attempt to send alert (best-effort)
      bool sent = false;
      // Wrap in try/catch style minimal timeout
      sent = connectAndSendAlert(payload);
      if (sent) {
        Serial.println("[BLE] Alert sent successfully");
      } else {
        Serial.println("[BLE] Alert failed or MainSystem not reachable");
      }
    }
  } else {
    Serial.println("[Detection] Unable to parse reply as JSON");
  }

  return true;
}

// ---------- BLE client helpers ----------
// UUIDs must match MainSystem.ino
static BLEUUID serviceUUID("12345678-1234-1234-1234-1234567890ab");
static BLEUUID charUUID("abcd1234-5678-90ab-cdef-1234567890ab");

bool connectAndSendAlert(const String &payload) {
  // Initialize BLE if needed
  if (!BLEDevice::getInitialized()) {
    BLEDevice::init("CameraClient");
  }

  BLEScan *pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  const int scanTimeSec = 3;
  Serial.println("[BLE] Scanning for MainSystem...");
  // Note: depending on ESP32 Arduino core version, start() may return a pointer
  // to BLEScanResults. Handle pointer return to be compatible.
  BLEScanResults *results = pBLEScan->start(scanTimeSec, false);

  for (int i = 0; i < results->getCount(); ++i) {
    BLEAdvertisedDevice adv = results->getDevice(i);
    // Match by advertised name or service UUID
    if (adv.haveName() && adv.getName() == "MainSystem") {
      Serial.println("[BLE] Found device by name: " + adv.getName());
    } else if (adv.haveServiceUUID() && adv.isAdvertisingService(serviceUUID)) {
      Serial.println("[BLE] Found device advertising target service");
    } else {
      continue;
    }

    // Try to connect
    BLEAddress addr = adv.getAddress();
    Serial.print("[BLE] Connecting to ");
    Serial.println(addr.toString().c_str());
    BLERemoteCharacteristic *pRemoteChar = nullptr;
    BLEClient *pClient = BLEDevice::createClient();
    if (!pClient->connect(addr)) {
      Serial.println("[BLE] Failed to connect");
      pClient->disconnect();
      delete pClient;
      continue;
    }

    BLERemoteService *pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
      Serial.println("[BLE] Service not found on device");
      pClient->disconnect();
      delete pClient;
      continue;
    }

    pRemoteChar = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteChar == nullptr) {
      Serial.println("[BLE] Characteristic not found");
      pClient->disconnect();
      delete pClient;
      continue;
    }

    // Write payload as bytes without std::string
    const char *dataPtr = payload.c_str();
    size_t dataLen = payload.length();
    bool ok = false;
    if (dataLen > 0) {
      pRemoteChar->writeValue((uint8_t *)dataPtr, dataLen, false);
      ok = true;
    }

    pClient->disconnect();
    delete pClient;
    if (ok) return true;
  }

  Serial.println("[BLE] Scan complete, no suitable device/failed to send");
  return false;
}

void startCameraServer();
void setupLedFlash();

void setup() {
  Serial.begin(115200);
  // Disable verbose core debug output to reduce flash/iram footprint
  // Serial.setDebugOutput(true);
  Serial.println();

  gHasPsram = psramFound();
  framesize_t initFrameSize = gHasPsram ? CAMERA_INIT_FRAME_SIZE
                                        : minFrameSize(CAMERA_INIT_FRAME_SIZE, CAMERA_NO_PSRAM_FRAME_SIZE);
  gActiveFrameSize = gHasPsram ? CAMERA_RUNTIME_FRAME_SIZE
                               : minFrameSize(CAMERA_RUNTIME_FRAME_SIZE, CAMERA_NO_PSRAM_FRAME_SIZE);

  Serial.printf("PSRAM detected: %s\n", gHasPsram ? "yes" : "no");
  logFrameSize("Init frame size", initFrameSize);
  logFrameSize("Target runtime frame size", gActiveFrameSize);

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
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = initFrameSize;
  config.pixel_format = PIXFORMAT_JPEG;  // for streaming
  //config.pixel_format = PIXFORMAT_RGB565; // for face detection/recognition
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = gHasPsram ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  //                      for larger pre-allocated frame buffer.
  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (gHasPsram) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      // Limit the frame size when PSRAM is not available
      config.frame_size = minFrameSize(initFrameSize, CAMERA_NO_PSRAM_FRAME_SIZE);
    }
  } else {
    // Best option for face detection/recognition
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);        // flip it back
    s->set_saturation(s, -2);  // lower the saturation
  }

  s->set_brightness(s, CAMERA_BRIGHTNESS);
  s->set_contrast(s, CAMERA_CONTRAST);

  if (CAMERA_ENABLE_AUTO_GAIN) {
    s->set_gain_ctrl(s, 1);
    s->set_gainceiling(s, CAMERA_GAIN_CEILING);
  } else {
    s->set_gain_ctrl(s, 0);
  }

  if (CAMERA_ENABLE_AEC2) {
    s->set_aec2(s, 1);
  }

  if (CAMERA_MANUAL_EXPOSURE > 0) {
    s->set_exposure_ctrl(s, 0);
    s->set_aec_value(s, CAMERA_MANUAL_EXPOSURE);
  } else {
    s->set_exposure_ctrl(s, CAMERA_ENABLE_AUTO_EXPOSURE ? 1 : 0);
  }

  s->set_whitebal(s, CAMERA_ENABLE_AUTO_WHITE_BALANCE ? 1 : 0);
  s->set_awb_gain(s, CAMERA_ENABLE_AWB_GAIN ? 1 : 0);
  s->set_lenc(s, CAMERA_ENABLE_LENS_CORRECTION ? 1 : 0);

  Serial.printf(
      "Camera tuning applied (brightness=%d, contrast=%d, gain_ceiling=%d, auto_exposure=%s, manual_aec=%d)\n",
      CAMERA_BRIGHTNESS,
      CAMERA_CONTRAST,
      CAMERA_GAIN_CEILING,
      CAMERA_ENABLE_AUTO_EXPOSURE ? "on" : "off",
      CAMERA_MANUAL_EXPOSURE);
  // Apply runtime frame size preference
  if (config.pixel_format == PIXFORMAT_JPEG) {
    s->set_framesize(s, gActiveFrameSize);
  }
  gActiveFrameSize = static_cast<framesize_t>(s->status.framesize);
  logFrameSize("Active frame size", gActiveFrameSize);

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif

// Setup LED FLash if LED pin is defined in camera_pins.h
#if defined(LED_GPIO_NUM)
  setupLedFlash();
#endif

  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  // The full camera webserver is optional (large). Define ENABLE_CAMERA_WEBSERVER
  // if you want the HTTP streaming server. Leaving it off reduces IRAM usage.
#ifdef ENABLE_CAMERA_WEBSERVER
  startCameraServer();
#else
  Serial.println("Camera webserver disabled (define ENABLE_CAMERA_WEBSERVER to enable)");
#endif

  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");

  // Configure IR sensor input
  pinMode(IR_SENSOR_PIN, IR_ACTIVE_LOW ? INPUT_PULLUP : INPUT);
  lastIrState = digitalRead(IR_SENSOR_PIN);
  Serial.printf("IR sensor ready on pin %d (active %s)\n", IR_SENSOR_PIN, IR_ACTIVE_LOW ? "LOW" : "HIGH");
}

void loop() {
  int currentState = digitalRead(IR_SENSOR_PIN);
  bool currentlyActive = IR_ACTIVE_LOW ? (currentState == LOW) : (currentState == HIGH);
  bool previouslyActive = IR_ACTIVE_LOW ? (lastIrState == LOW) : (lastIrState == HIGH);
  unsigned long now = millis();

  if (currentlyActive && !previouslyActive) {
    if (now - lastIrTriggerMs < IR_COOLDOWN_MS) {
      Serial.println("[IR] Trigger ignored due to cooldown");
    } else {
      Serial.println("[IR] Trigger detected. Capturing frame...");
      bool success = captureAndDetectRats();
      if (success) {
        Serial.println("[IR] Detection complete");
      }
      lastIrTriggerMs = now;
    }
  }

  lastIrState = currentState;
  delay(50);
}
