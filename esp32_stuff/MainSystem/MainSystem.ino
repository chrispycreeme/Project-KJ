/*
  MainSystem.ino
  BLE GATT server for receiving detection alerts from camera ESP32.
  - Advertises service UUID 12345678-1234-1234-1234-1234567890ab
  - Characteristic UUID abcd1234-5678-90ab-cdef-1234567890ab (write)
  When the characteristic is written, the payload is printed to Serial.
*/

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

// Service and characteristic UUIDs (change if you like)
#define SERVICE_UUID "12345678-1234-1234-1234-1234567890ab"
#define CHAR_UUID    "abcd1234-5678-90ab-cdef-1234567890ab"

class AlertCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    // BLECharacteristic::getValue() can return an Arduino String on some cores.
    // Use Arduino String to avoid conversion issues.
    String value = pCharacteristic->getValue().c_str();
    Serial.print("[BLE] Received write (");
    Serial.print(value.length());
    Serial.println(" bytes):");
    if (value.length() > 0) {
      Serial.println(value);
    }
  }
};

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("[MainSystem] Starting BLE server...");

  BLEDevice::init("MainSystem");
  BLEServer *pServer = BLEDevice::createServer();

  BLEService *pService = pServer->createService(SERVICE_UUID);
  BLECharacteristic *pCharacteristic = pService->createCharacteristic(
      CHAR_UUID,
      BLECharacteristic::PROPERTY_WRITE
  );

  pCharacteristic->setCallbacks(new AlertCallbacks());
  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->start();

  Serial.println("[MainSystem] BLE server started and advertising as 'MainSystem'");
}

void loop() {
  // Nothing to do in loop other than keep running and react to BLE writes
  delay(1000);
}
