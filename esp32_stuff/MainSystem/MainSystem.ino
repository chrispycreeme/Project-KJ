#include <ESP32Servo.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ===== HARDCODED CREDENTIALS - CHANGE THESE =====
const char* WIFI_SSID = "ProjectKJ";
const char* WIFI_PASSWORD = "projectkj";
const char* FIREBASE_PROJECT_ID = "project-kj-ae694";
const char* FIREBASE_API_KEY = "AIzaSyDrMR1B5WWZUcugFYYOtjZBsmGWhtL3O88";
// ===============================================

// Firestore configuration
const char* firestoreHost = "firestore.googleapis.com";
const int firestorePort = 443;
String documentPath = "trap_controller/primary"; // Collection/Document

// UDP Beacon
WiFiUDP udpBeacon;
const int UDP_BEACON_PORT = 4210;
const char* BEACON_MSG = "ESP32_MAIN_HERE";
unsigned long lastBeaconTime = 0;
const unsigned long beaconInterval = 1000;

// Web Server
WebServer server(80);

// BTS7960 Motor Driver
#define R_EN 19
#define RPWM 18
#define L_EN 5
#define LPWM 17

// Relay for CO2
#define RELAY_PIN 16

// Servo (feed dispenser)
#define SERVO_PIN 27

// LEDs
#define GREEN_LED_PIN 12
#define RED_LED_PIN 14

// Buzzer
#define BUZZER_PIN 25

// PWM
#define PWM_FREQ 1000
#define PWM_RESOLUTION 8
#define PWM_CHANNEL_RPWM 0
#define PWM_CHANNEL_LPWM 1

Servo feedServo;

int startCounter = 0;
bool activateTrap = false;
bool co2Release = false;
bool openFeedDispenser = false;
int cyclesNeededForCO2 = 4;
int cycleCompleted = 0;
int ratDetectedCount = 0;

// Firestore sync timing
unsigned long lastFirestoreSync = 0;
const unsigned long firestoreSyncInterval = 5000; // Sync every 5 seconds

// Timing (milliseconds)
unsigned long TRAPDOOR_OPEN_TIME   = 500;
unsigned long TRAPDOOR_CLOSE_TIME  = 500;
unsigned long TRAPDOOR_STOP_PAUSE  = 500;
unsigned long CO2_RELAY_ON_TIME    = 10000;
unsigned long CO2_RELAY_OFF_PAUSE  = 500;
unsigned long FEED_OPEN_TIME       = 2000;
unsigned long FEED_CLOSE_TIME      = 1000;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP32 Main Controller ===");
  Serial.println("Rat Trap System v3.0 (Simplified)");
  Serial.println("====================================");
  
  // Initialize pins
  pinMode(R_EN, OUTPUT);
  pinMode(L_EN, OUTPUT);
  digitalWrite(R_EN, HIGH);
  digitalWrite(L_EN, HIGH);
  
  ledcAttach(RPWM, PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(LPWM, PWM_FREQ, PWM_RESOLUTION);
  
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  digitalWrite(GREEN_LED_PIN, LOW);
  digitalWrite(RED_LED_PIN, HIGH);
  
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  
  ESP32PWM::allocateTimer(2);
  feedServo.setPeriodHertz(50);
  feedServo.attach(SERVO_PIN, 500, 2400);
  
  feedServo.write(0);
  
  // Connect to WiFi
  Serial.println("\n[WiFi] Connecting...");
  connectToWiFi();
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Failed! Restarting...");
    delay(5000);
    ESP.restart();
  }
  
  setupWebServer();
  
  // Start UDP beacon
  udpBeacon.begin(UDP_BEACON_PORT);
  Serial.println("[UDP] Beacon started on port 4210");
  
  // Initialize Firestore document
  Serial.println("\n[Firestore] Initializing document...");
  initializeFirestoreDocument();
  
  Serial.println("\n=== System Ready ===");
  Serial.println("Waiting for camera detection...");
  Serial.println("Syncing with Firestore every 5 seconds");
  Serial.println("Type 'start' to test manually\n");
}

void loop() {
  server.handleClient();
  
  // Send beacon
  if (WiFi.status() == WL_CONNECTED && millis() - lastBeaconTime >= beaconInterval) {
    lastBeaconTime = millis();
    sendDiscoveryBeacon();
  }
  
  // Sync with Firestore
  if (WiFi.status() == WL_CONNECTED && millis() - lastFirestoreSync >= firestoreSyncInterval) {
    lastFirestoreSync = millis();
    syncWithFirestore();
  }
  
  // Monitor WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Disconnected! Reconnecting...");
    connectToWiFi();
  }
  
  // Serial commands
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    command.toLowerCase();
    
    if (command == "start") {
      processStartCommand();
    } else if (command == "reset") {
      Serial.println("[CMD] Resetting counter...");
      startCounter = 0;
      cycleCompleted = 0;
      updateFirestore();
      Serial.println("✓ Reset complete");
    } else if (command == "status") {
      Serial.println("\n=== System Status ===");
      Serial.printf("WiFi: %s\n", WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
      Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
      Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
      Serial.printf("Free Heap: %u bytes\n", ESP.getFreeHeap());
      Serial.printf("Start Counter: %d/3\n", startCounter);
      Serial.printf("Cycle Completed: %d\n", cycleCompleted);
      Serial.printf("Rats Detected: %d\n", ratDetectedCount);
      Serial.println("====================\n");
    } else if (command == "fetch") {
      Serial.println("[CMD] Fetching Firestore data...");
      fetchFirestoreData();
    }
  }
}

void processStartCommand() {
  startCounter++;
  ratDetectedCount++;
  Serial.printf("\n[START] Trigger received. Count: %d/3\n", startCounter);
  
  beepBuzzer(2, 100);
  trapdoorMotion();
  
  if (startCounter >= 3) {
    Serial.println("\n[TRIGGER] Counter reached 3!");
    cycleCompleted++;
    
    Serial.printf("Cycles completed: %d/%d\n", cycleCompleted, cyclesNeededForCO2);
    
    if (cycleCompleted >= cyclesNeededForCO2) {
      Serial.println("Activating CO2 and feed dispenser...");
      activateCO2();
      dispenseFeed();
      cycleCompleted = 0; // Reset cycle counter
    } else {
      Serial.println("Cycle complete, waiting for more detections...");
    }
    
    startCounter = 0;
    Serial.println("✓ Counter reset\n");
  }
  
  // Update Firestore
  updateFirestore();
  
  Serial.println("Ready for next trigger\n");
}

void trapdoorMotion() {
  Serial.println("[TRAP] Opening trapdoor...");
  motorForward(255);
  delay(TRAPDOOR_OPEN_TIME);
  motorStop();
  delay(TRAPDOOR_STOP_PAUSE);
  
  Serial.println("[TRAP] Closing trapdoor...");
  motorReverse(255);
  delay(TRAPDOOR_CLOSE_TIME);
  motorStop();
  delay(TRAPDOOR_STOP_PAUSE);
  
  Serial.println("[TRAP] ✓ Complete");
}

void activateCO2() {
  Serial.println("[CO2] Opening solenoid...");
  digitalWrite(RELAY_PIN, HIGH);
  delay(CO2_RELAY_ON_TIME);
  
  Serial.println("[CO2] Closing solenoid...");
  digitalWrite(RELAY_PIN, LOW);
  delay(CO2_RELAY_OFF_PAUSE);
  
  Serial.println("[CO2] ✓ Complete");
}

void dispenseFeed() {
  Serial.println("[FEED] Opening dispenser...");
  feedServo.write(90);
  delay(FEED_OPEN_TIME);
  
  Serial.println("[FEED] Closing dispenser...");
  feedServo.write(0);
  delay(FEED_CLOSE_TIME);
  
  Serial.println("[FEED] ✓ Complete");
}

void motorForward(int speed) {
  ledcWrite(LPWM, 0);
  ledcWrite(RPWM, speed);
}

void motorReverse(int speed) {
  ledcWrite(RPWM, 0);
  ledcWrite(LPWM, speed);
}

void motorStop() {
  ledcWrite(RPWM, 0);
  ledcWrite(LPWM, 0);
}

void connectToWiFi() {
  Serial.println("\n=== WiFi Connection ===");
  Serial.printf("SSID: '%s'\n", WIFI_SSID);
  
  WiFi.disconnect(true, true);
  delay(500);
  WiFi.mode(WIFI_OFF);
  delay(1000);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n\n✓ WiFi Connected!");
    Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Signal: %d dBm\n", WiFi.RSSI());
    
    digitalWrite(GREEN_LED_PIN, HIGH);
    digitalWrite(RED_LED_PIN, LOW);
  } else {
    Serial.println("\n\n✗ WiFi Failed");
    
    digitalWrite(GREEN_LED_PIN, LOW);
    digitalWrite(RED_LED_PIN, HIGH);
    
    WiFi.disconnect(true, true);
    delay(200);
    WiFi.mode(WIFI_OFF);
  }
}

void setupWebServer() {
  server.on("/trigger", HTTP_POST, []() {
    Serial.println("[HTTP] Trigger received from camera!");
    processStartCommand();
    server.send(200, "text/plain", "OK");
  });
  
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/plain", "ESP32 Main Controller - Rat Trap System");
  });
  
  server.begin();
  Serial.println("[HTTP] Server started on port 80");
  Serial.println("====================================");
  Serial.printf("MAIN ESP32 IP: %s\n", WiFi.localIP().toString().c_str());
  Serial.println("====================================");
}

void sendDiscoveryBeacon() {
  IPAddress broadcastIP = WiFi.localIP();
  broadcastIP[3] = 255;
  
  udpBeacon.beginPacket(broadcastIP, UDP_BEACON_PORT);
  udpBeacon.print(BEACON_MSG);
  udpBeacon.endPacket();
}

void beepBuzzer(int times, int duration) {
  for (int i = 0; i < times; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(duration);
    digitalWrite(BUZZER_PIN, LOW);
    if (i < times - 1) delay(duration);
  }
}

// ===== FIRESTORE FUNCTIONS =====

void initializeFirestoreDocument() {
  Serial.println("[Firestore] Creating/updating initial document...");
  
  // Create initial document structure
  String jsonPayload = "{\"fields\":{";
  jsonPayload += "\"activate_trap\":{\"booleanValue\":false},";
  jsonPayload += "\"co2_release\":{\"booleanValue\":false},";
  jsonPayload += "\"open_feed_dispenser\":{\"booleanValue\":false},";
  jsonPayload += "\"cycle_completed\":{\"integerValue\":\"0\"},";
  jsonPayload += "\"cycles_needed_for_co2_release\":{\"integerValue\":\"4\"},";
  jsonPayload += "\"rat_detected_count\":{\"integerValue\":\"0\"},";
  jsonPayload += "\"updated_at\":{\"timestampValue\":\"" + getCurrentTimestamp() + "\"}";
  jsonPayload += "}}";
  
  if (sendFirestoreRequest("PATCH", jsonPayload)) {
    Serial.println("[Firestore] ✓ Document initialized");
  } else {
    Serial.println("[Firestore] ✗ Failed to initialize");
  }
}

void syncWithFirestore() {
  // Fetch current state from Firestore
  fetchFirestoreData();
  
  // Check if we need to activate trap based on Firestore flags
  if (activateTrap) {
    Serial.println("\n[Firestore] activate_trap flag detected!");
    processStartCommand();
    
    // Reset the flag in Firestore
    activateTrap = false;
    String resetPayload = "{\"fields\":{\"activate_trap\":{\"booleanValue\":false}}}";
    sendFirestoreRequest("PATCH", resetPayload);
  }
  
  if (co2Release) {
    Serial.println("\n[Firestore] co2_release flag detected!");
    activateCO2();
    
    // Reset the flag
    co2Release = false;
    String resetPayload = "{\"fields\":{\"co2_release\":{\"booleanValue\":false}}}";
    sendFirestoreRequest("PATCH", resetPayload);
  }
  
  if (openFeedDispenser) {
    Serial.println("\n[Firestore] open_feed_dispenser flag detected!");
    dispenseFeed();
    
    // Reset the flag
    openFeedDispenser = false;
    String resetPayload = "{\"fields\":{\"open_feed_dispenser\":{\"booleanValue\":false}}}";
    sendFirestoreRequest("PATCH", resetPayload);
  }
}

void fetchFirestoreData() {
  Serial.println("[Firestore] Fetching document...");
  
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(10000);
  
  String url = "/v1/projects/" + String(FIREBASE_PROJECT_ID) + "/databases/(default)/documents/" + documentPath;
  
  if (!client.connect(firestoreHost, firestorePort)) {
    Serial.println("[Firestore] ✗ Connection failed");
    return;
  }
  
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + firestoreHost + "\r\n" +
               "Connection: close\r\n\r\n");
  
  // Wait for response
  unsigned long timeout = millis();
  while (client.connected() && !client.available() && millis() - timeout < 5000) {
    delay(10);
  }
  
  // Skip headers
  while (client.available()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") break;
  }
  
  // Read JSON response
  String response = "";
  while (client.available()) {
    response += client.readString();
  }
  client.stop();
  
  if (response.length() > 0) {
    Serial.println("[Firestore] ✓ Data received");
    parseFirestoreResponse(response);
  } else {
    Serial.println("[Firestore] ✗ No data received");
  }
}

void parseFirestoreResponse(String json) {
  // Parse the Firestore JSON response
  // Looking for patterns like: "activate_trap":{"booleanValue":true}
  
  if (json.indexOf("\"activate_trap\"") > 0) {
    int pos = json.indexOf("\"activate_trap\"");
    String sub = json.substring(pos, pos + 100);
    activateTrap = sub.indexOf("true") > 0;
  }
  
  if (json.indexOf("\"co2_release\"") > 0) {
    int pos = json.indexOf("\"co2_release\"");
    String sub = json.substring(pos, pos + 100);
    co2Release = sub.indexOf("true") > 0;
  }
  
  if (json.indexOf("\"open_feed_dispenser\"") > 0) {
    int pos = json.indexOf("\"open_feed_dispenser\"");
    String sub = json.substring(pos, pos + 100);
    openFeedDispenser = sub.indexOf("true") > 0;
  }
  
  if (json.indexOf("\"cycles_needed_for_co2_release\"") > 0) {
    int pos = json.indexOf("\"cycles_needed_for_co2_release\"");
    String sub = json.substring(pos, pos + 150);
    int valPos = sub.indexOf("\"integerValue\":\"") + 16;
    int endPos = sub.indexOf("\"", valPos);
    if (valPos > 15 && endPos > valPos) {
      String valStr = sub.substring(valPos, endPos);
      cyclesNeededForCO2 = valStr.toInt();
    }
  }
  
  Serial.printf("[Firestore] Parsed - activate_trap: %d, co2: %d, feed: %d, cycles_needed: %d\n", 
                activateTrap, co2Release, openFeedDispenser, cyclesNeededForCO2);
}

void updateFirestore() {
  Serial.println("[Firestore] Updating document...");
  
  String jsonPayload = "{\"fields\":{";
  jsonPayload += "\"cycle_completed\":{\"integerValue\":\"" + String(cycleCompleted) + "\"},";
  jsonPayload += "\"rat_detected_count\":{\"integerValue\":\"" + String(ratDetectedCount) + "\"},";
  jsonPayload += "\"updated_at\":{\"timestampValue\":\"" + getCurrentTimestamp() + "\"}";
  jsonPayload += "}}";
  
  if (sendFirestoreRequest("PATCH", jsonPayload)) {
    Serial.println("[Firestore] ✓ Document updated");
  } else {
    Serial.println("[Firestore] ✗ Update failed");
  }
}

bool sendFirestoreRequest(String method, String jsonPayload) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(10000);
  
  if (!client.connect(firestoreHost, firestorePort)) {
    Serial.println("[Firestore] ✗ Connection failed");
    return false;
  }
  
  String url = "/v1/projects/" + String(FIREBASE_PROJECT_ID) + "/databases/(default)/documents/" + documentPath;
  
  client.print(String(method) + " " + url + " HTTP/1.1\r\n" +
               "Host: " + firestoreHost + "\r\n" +
               "Content-Type: application/json\r\n" +
               "Content-Length: " + String(jsonPayload.length()) + "\r\n" +
               "Connection: close\r\n\r\n" +
               jsonPayload);
  
  // Wait for response
  unsigned long timeout = millis();
  while (client.connected() && !client.available() && millis() - timeout < 5000) {
    delay(10);
  }
  
  bool success = false;
  if (client.available()) {
    String response = client.readStringUntil('\n');
    success = response.indexOf("200") >= 0;
  }
  
  client.stop();
  return success;
}

String getCurrentTimestamp() {
  // Return ISO 8601 timestamp
  // For now, use a placeholder - in production, you'd use NTP to get real time
  return "2025-01-01T00:00:00Z";
}
