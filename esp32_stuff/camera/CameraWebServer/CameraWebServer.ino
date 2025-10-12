#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>  // Install ArduinoJson (6.x) via Library Manager
#include "mbedtls/base64.h"
// Enable the built-in camera webserver. This adds code size but provides
// the HTTP streaming endpoints. Define this only if you need the web UI.
#define ENABLE_CAMERA_WEBSERVER

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
const int CAMERA_WARMUP_FRAMES = 3;                         // Extra frames to discard before detection
const int CAMERA_WARMUP_DELAY_MS = 60;                      // Delay between warmup frames (ms)

// Common frame size options: FRAMESIZE_QQVGA (160x120), FRAMESIZE_QVGA (320x240),
// FRAMESIZE_VGA (640x480), FRAMESIZE_SVGA (800x600), FRAMESIZE_XGA (1024x768),
// FRAMESIZE_HD (1280x720), FRAMESIZE_UXGA (1600x1200), FRAMESIZE_FHD (1920x1080)
const framesize_t CAMERA_INIT_FRAME_SIZE = FRAMESIZE_UXGA;          // Resolution during sensor init
// Prefer HD (1280x720) runtime frame size for a good balance of quality and
// network size. If PSRAM is present, we use HD; otherwise code will fall back
// to CAMERA_NO_PSRAM_FRAME_SIZE.
const framesize_t CAMERA_RUNTIME_FRAME_SIZE = FRAMESIZE_HD;       // Resolution used after tuning
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
// Last Gemini HTTP status code (set by sendFrameToGemini when non-200)
int gLastGeminiHttpCode = 0;

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

  // Deterministic base64 encoding — avoids any embedded NULs and produces
  // only chars in the set [A-Za-z0-9+/=]. This is small and portable, and
  // avoids surprises from library encoders on embedded platforms.
  static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  size_t outLen = ((length + 2) / 3) * 4;
  String encoded;
  // Reserve to avoid multiple reallocations; on PSRAM-enabled boards this
  // will use PSRAM-backed heap for the String buffer.
  encoded.reserve(outLen + 1);

  size_t i = 0;
  while (i + 2 < length) {
    uint32_t val = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8) |
                   static_cast<uint32_t>(data[i + 2]);
    encoded += table[(val >> 18) & 0x3F];
    encoded += table[(val >> 12) & 0x3F];
    encoded += table[(val >> 6) & 0x3F];
    encoded += table[val & 0x3F];
    i += 3;
  }

  if (i < length) {
    int rem = static_cast<int>(length - i);
    uint32_t val = static_cast<uint32_t>(data[i]) << 16;
    if (rem == 2) val |= static_cast<uint32_t>(data[i + 1]) << 8;

    encoded += table[(val >> 18) & 0x3F];
    encoded += table[(val >> 12) & 0x3F];
    if (rem == 2) {
      encoded += table[(val >> 6) & 0x3F];
      encoded += '=';
    } else {
      // rem == 1
      encoded += '=';
      encoded += '=';
    }
  }

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

  // Validate encoded data: non-empty, not excessively large, and contains only
  // base64 characters. This prevents sending malformed inline_data that causes
  // INVALID_ARGUMENT from the Gemini API.
  const size_t MAX_ENCODED_SIZE = 2 * 1024 * 1024; // 2 MB encoded (adjustable)
  if (encoded.length() == 0) {
    Serial.println("[Gemini] Encoded data is empty");
    return false;
  }
  if (encoded.length() > MAX_ENCODED_SIZE) {
    Serial.printf("[Gemini] Encoded data too large: %u bytes\n", (unsigned int)encoded.length());
    return false;
  }

  // Verify characters are valid base64 (A-Z, a-z, 0-9, +, /, =)
  for (size_t i = 0; i < encoded.length(); ++i) {
    char c = encoded[i];
    bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '+') || (c == '/') || (c == '=');
    if (!ok) {
      Serial.printf("[Gemini] Invalid base64 char at %u: 0x%02x\n", (unsigned int)i, (unsigned char)c);
      return false;
    }
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
    // Record last HTTP code to allow caller to react (e.g. retry at lower res)
    gLastGeminiHttpCode = httpCode;
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
  // Acquire sensor handle early so we can change framesize for fallbacks
  sensor_t *s = esp_camera_sensor_get();

  for (int i = 0; i < CAMERA_WARMUP_FRAMES; ++i) {
    camera_fb_t *tmp = esp_camera_fb_get();
    if (tmp) {
      esp_camera_fb_return(tmp);
    }
    delay(CAMERA_WARMUP_DELAY_MS);
  }
  // Try capturing a valid frame (non-empty) with limited retries to avoid
  // sending empty inline_data which triggers INVALID_ARGUMENT from Gemini.
  const int maxCaptureAttempts = 6;
  camera_fb_t *frame = nullptr;
  for (int attempt = 0; attempt < maxCaptureAttempts; ++attempt) {
    frame = esp_camera_fb_get();
    if (frame && frame->len > 100) { // require >100 bytes to be considered valid
      break;
    }
    if (frame) {
      esp_camera_fb_return(frame);
      frame = nullptr;
    }
    Serial.printf("[Camera] Capture empty or too small (attempt %d/%d), retrying...\n", attempt + 1, maxCaptureAttempts);
    // Exponential backoff: 100ms, 200ms, 400ms, ...
    int backoff = 100 * (1 << (attempt < 6 ? attempt : 5));
    delay(backoff);
    // After half of attempts, try reducing frame size to improve capture reliability
    if (attempt == maxCaptureAttempts / 2) {
      sensor_t *s = esp_camera_sensor_get();
      if (s) {
        Serial.println("[Camera] Lowering frame size temporarily to improve capture reliability");
        s->set_framesize(s, CAMERA_NO_PSRAM_FRAME_SIZE);
      }
    }
  }

  if (!frame) {
    Serial.println("[Camera] Failed to capture a valid frame after retries");
    return false;
  }

  Serial.print("[Camera] Captured frame ");
  Serial.print(frame->width);
  Serial.print("x");
  Serial.print(frame->height);
  Serial.print(" (");
  Serial.print((unsigned int)frame->len);
  Serial.println(" bytes)");

  // Ensure we have a non-empty payload before calling Gemini
  if (frame->len == 0) {
    esp_camera_fb_return(frame);
    Serial.println("[Detection] Frame buffer empty, aborting detection");
    return false;
  }

  String summary;
  bool ok = sendFrameToGemini(frame->buf, frame->len, summary);
  esp_camera_fb_return(frame);

  if (!ok) {
    Serial.println("[Detection] Gemini detection failed");
    // If Gemini returned 400 (commonly Base64 decode failed for inline_data),
    // try adaptive fallbacks: progressively reduce resolution and retry a few
    // times. This helps when the encoded payload is too large or malformed
    // for the API to decode.
    if (gLastGeminiHttpCode == 400 && s) {
      framesize_t fallbackSizes[] = {FRAMESIZE_SVGA, FRAMESIZE_VGA, FRAMESIZE_QVGA};
      framesize_t originalSize = static_cast<framesize_t>(s->status.framesize);
      for (size_t fi = 0; fi < sizeof(fallbackSizes) / sizeof(fallbackSizes[0]); ++fi) {
        framesize_t candidate = fallbackSizes[fi];
        // only try sizes smaller than the current active
        if (candidate >= originalSize) continue;

        Serial.print("[Detection] Retrying with smaller framesize: ");
        logFrameSize(" -> trying", candidate);
        s->set_framesize(s, candidate);
        delay(150); // allow sensor to settle

        camera_fb_t *fb2 = esp_camera_fb_get();
        if (!fb2) {
          Serial.println("[Detection] fallback capture failed (no fb)");
          continue;
        }
        if (fb2->len == 0) {
          Serial.println("[Detection] fallback capture empty");
          esp_camera_fb_return(fb2);
          continue;
        }

        Serial.printf("[Detection] Fallback captured %dx%d (%u bytes)\n", fb2->width, fb2->height, (unsigned int)fb2->len);
        String summary2;
        bool ok2 = sendFrameToGemini(fb2->buf, fb2->len, summary2);
        esp_camera_fb_return(fb2);
        if (ok2) {
          Serial.println("[Detection] Fallback detection succeeded");
          Serial.println(summary2);
          // restore original framesize
          s->set_framesize(s, originalSize);
          gActiveFrameSize = static_cast<framesize_t>(s->status.framesize);
          return true;
        }
        Serial.println("[Detection] Fallback attempt failed");
        // small delay before next fallback
        delay(200);
      }

      // restore original framesize before returning
      s->set_framesize(s, static_cast<framesize_t>(gActiveFrameSize));
    }
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

      Serial.println("[Detection] Rat detected — BLE disabled in this build");
    }
  } else {
    Serial.println("[Detection] Unable to parse reply as JSON");
  }

  return true;
}

// BLE removed: no UUIDs or client helpers

// BLE support removed from camera sketch

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
      // Lower number = better quality. Use higher fidelity when PSRAM available.
      // Set to 6 for high fidelity. If you want even higher quality and can
      // accept larger payloads, use 4.
      config.jpeg_quality = 6;
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
