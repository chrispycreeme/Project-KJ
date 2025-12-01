#include <ESP32Servo.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>

// ===== HARDCODED CREDENTIALS - CHANGE THESE =====
const char* WIFI_SSID = "ProjectKJ";
const char* WIFI_PASSWORD = "projectkj";
// ===============================================

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

// Servos
#define SERVO1_PIN 27
#define SERVO2_PIN 26

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

Servo servo1;
Servo servo2;

int startCounter = 0;

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
  servo1.setPeriodHertz(50);
  servo1.attach(SERVO1_PIN, 500, 2400);
  servo2.setPeriodHertz(50);
  servo2.attach(SERVO2_PIN, 500, 2400);
  
  servo1.write(0);
  servo2.write(0);
  
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
  
  Serial.println("\n=== System Ready ===");
  Serial.println("Waiting for camera detection...");
  Serial.println("Type 'start' to test manually\n");
}

void loop() {
  server.handleClient();
  
  // Send beacon
  if (WiFi.status() == WL_CONNECTED && millis() - lastBeaconTime >= beaconInterval) {
    lastBeaconTime = millis();
    sendDiscoveryBeacon();
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
      Serial.println("✓ Reset complete");
    } else if (command == "status") {
      Serial.println("\n=== System Status ===");
      Serial.printf("WiFi: %s\n", WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
      Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
      Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
      Serial.printf("Free Heap: %u bytes\n", ESP.getFreeHeap());
      Serial.printf("Start Counter: %d/3\n", startCounter);
      Serial.println("====================\n");
    }
  }
}

void processStartCommand() {
  startCounter++;
  Serial.printf("\n[START] Trigger received. Count: %d/3\n", startCounter);
  
  beepBuzzer(2, 100);
  trapdoorMotion();
  
  if (startCounter >= 3) {
    Serial.println("\n[TRIGGER] Counter reached 3!");
    Serial.println("Activating CO2 and feed dispenser...");
    
    activateCO2();
    dispenseFeed();
    
    startCounter = 0;
    Serial.println("✓ Counter reset\n");
  }
  
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
  servo1.write(90);
  servo2.write(90);
  delay(FEED_OPEN_TIME);
  
  Serial.println("[FEED] Closing dispenser...");
  servo1.write(0);
  servo2.write(0);
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
