// MainSystem.ino
// ESP32 sketch: listens on Serial and Bluetooth for text commands.
// When "start" is received the relay is activated.
// When "stop" is received the relay is deactivated.
// Sends acknowledgements back over Serial.

// ----- Added for Bluetooth -----
#include "BluetoothSerial.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

BluetoothSerial SerialBT;
// -----------------------------

// ----- Configuration -----
const int RELAY_PIN = 23; // change to the GPIO pin connected to the relay module
const bool RELAY_ACTIVE_HIGH = true; // set to false if your relay is active LOW

// Servo configuration
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(ARDUINO_ESP32)
// Use the ESP32-compatible Servo library. Install "ESP32Servo" via Library Manager if missing.
#include <ESP32Servo.h>
#else
#include <Servo.h>
#endif

#include <Arduino.h>

// Note: ESP32 LEDC prototypes are provided by the core headers; do not redeclare them here.
const int SERVO1_PIN = 18; // GPIO for servo 1 (change if needed)
const int SERVO2_PIN = 19; // GPIO for servo 2 (change if needed)

Servo servo1;
Servo servo2;

int servo1Pos = 90; // initial center position
int servo2Pos = 90; // initial center position

// Process sequence configuration
const int SERVO_OPEN_POS = 30; // angle for 'open' (change as needed)
const int SERVO_CLOSE_POS = 150; // angle for 'close'
// Make quick servo motions longer by increasing delay and repeats
const unsigned long SERVO_QUICK_DELAY = 500; // ms between open/close for slower/longer motion
const int SERVO_QUICK_REPEATS = 1; // how many open/close cycles

// Trapdoor motor: increase duration to open/close more fully/slowly
const int TRAPDOOR_MOTOR_OPEN_SPEED = 255; // 0-255 magnitude
const unsigned long TRAPDOOR_MOTOR_DURATION = 500; // ms to hold open/close (longer)

// Helper: quick open/close motion for both servos
void quickServoBlink() {
  for (int r = 0; r < SERVO_QUICK_REPEATS; ++r) {
    servo1.write(SERVO_OPEN_POS);
    servo2.write(SERVO_OPEN_POS);
    delay(SERVO_QUICK_DELAY);
    servo1.write(SERVO_CLOSE_POS);
    servo2.write(SERVO_CLOSE_POS);
    delay(SERVO_QUICK_DELAY);
  }
  // return to center
  servo1.write(servo1Pos);
  servo2.write(servo2Pos);
}

// Helper: open then close trapdoor using motor (forward = open, reverse = close)
void motorTrapdoorOpenClose() {
  // open (forward)
  setMotor(TRAPDOOR_MOTOR_OPEN_SPEED);
  delay(TRAPDOOR_MOTOR_DURATION);
  motorCoast();
  delay(100);

  // close (reverse)
  setMotor(-TRAPDOOR_MOTOR_OPEN_SPEED);
  delay(TRAPDOOR_MOTOR_DURATION);
  motorCoast();
}

// ----- BTS7960 motor driver configuration -----
// Default pins (change to match your wiring)
const int BTS_LPWM_PIN = 25; // LPWM (left/input A)
const int BTS_RPWM_PIN = 26; // RPWM (right/input B)
const int BTS_L_EN_PIN = 27; // L_EN (enable for left)
const int BTS_R_EN_PIN = 14; // R_EN (enable for right)

// PWM configuration (using analogWrite for portability)
// Note: On ESP32 core versions analogWrite is provided;
// ensure BTS_LPWM_PIN/BTS_RPWM_PIN are PWM-capable.

// Current motor state
int motorSpeed = 0; // -255..255

// Process invocation counter
int processCount = 0;

// Initialize BTS7960 pins and PWM
void motorInit() {
  pinMode(BTS_L_EN_PIN, OUTPUT);
  pinMode(BTS_R_EN_PIN, OUTPUT);
  // start disabled (coast)
  digitalWrite(BTS_L_EN_PIN, LOW);
  digitalWrite(BTS_R_EN_PIN, LOW);
  // Use analogWrite for PWM on both platforms for portability.
  pinMode(BTS_LPWM_PIN, OUTPUT);
  pinMode(BTS_RPWM_PIN, OUTPUT);
  analogWrite(BTS_LPWM_PIN, 0);
  analogWrite(BTS_RPWM_PIN, 0);
}

// Set motor speed: -255..255 (negative = reverse)
void setMotor(int speed) {
  motorSpeed = constrain(speed, -255, 255);
  if (motorSpeed == 0) {
    // coast by disabling enables
    digitalWrite(BTS_L_EN_PIN, LOW);
    digitalWrite(BTS_R_EN_PIN, LOW);
    analogWrite(BTS_LPWM_PIN, 0);
    analogWrite(BTS_RPWM_PIN, 0);
    return;
  }

  // enable driver
  digitalWrite(BTS_L_EN_PIN, HIGH);
  digitalWrite(BTS_R_EN_PIN, HIGH);

  int duty = abs(motorSpeed); // 0..255
  if (motorSpeed > 0) {
    // forward: PWM on R channel, L channel zero
    analogWrite(BTS_LPWM_PIN, 0);
    analogWrite(BTS_RPWM_PIN, duty);
  } else {
    // reverse: PWM on L channel, R channel zero
    analogWrite(BTS_LPWM_PIN, duty);
    analogWrite(BTS_RPWM_PIN, 0);
  }
}

// Brake: short both motor terminals -> set both PWM to max and enable
void motorBrake() {
  digitalWrite(BTS_L_EN_PIN, HIGH);
  digitalWrite(BTS_R_EN_PIN, HIGH);
  analogWrite(BTS_LPWM_PIN, 255);
  analogWrite(BTS_RPWM_PIN, 255);
  motorSpeed = 0;
}

// Coast: disable outputs
void motorCoast() {
  digitalWrite(BTS_L_EN_PIN, LOW);
  digitalWrite(BTS_R_EN_PIN, LOW);
  analogWrite(BTS_LPWM_PIN, 0);
  analogWrite(BTS_RPWM_PIN, 0);
  motorSpeed = 0;
}

// Helper to set relay state
void setRelay(bool on) {
  if (RELAY_ACTIVE_HIGH) {
    digitalWrite(RELAY_PIN, on ? HIGH : LOW);
  } else {
    digitalWrite(RELAY_PIN, on ? LOW : HIGH);
  }
}

// ----- New function to handle commands from any source -----
void handleCommand(String cmd) {
  // normalize
  for (unsigned int i = 0; i < cmd.length(); ++i) cmd[i] = tolower(cmd[i]);
  // parse servo commands: they can be like 'servo1 90' or 'servoAll 45'
  if (cmd.startsWith("servo1 ") || cmd.startsWith("servo2 ") || cmd.startsWith("servoall ")) {
    // split into token and value
    int spaceIdx = cmd.indexOf(' ');
    String token = (spaceIdx > 0) ? cmd.substring(0, spaceIdx) : cmd;
    String val = (spaceIdx > 0) ? cmd.substring(spaceIdx + 1) : "";
    val.trim();
    if (val.length() == 0) {
      Serial.println("ERR: missing angle value (0-180)");
    } else {
      int angle = val.toInt();
      if (angle < 0 || angle > 180) {
        Serial.println("ERR: angle out of range (0-180)");
      } else {
        if (token == "servo1") {
          servo1Pos = angle;
          servo1.write(angle);
          Serial.print("OK: servo1 set to "); Serial.println(angle);
        } else if (token == "servo2") {
          servo2Pos = angle;
          servo2.write(angle);
          Serial.print("OK: servo2 set to "); Serial.println(angle);
        } else if (token == "servoall") {
          servo1Pos = angle;
          servo2Pos = angle;
          servo1.write(angle);
          servo2.write(angle);
          Serial.print("OK: both servos set to "); Serial.println(angle);
        }
      }
    }
    return; // handled - exit this loop() iteration
  }

  // parse motor reverse commands first: 'motor rev' or 'motor reverse'
  if (cmd.startsWith("motor rev") || cmd.startsWith("motor reverse")) {
    // optionally with a value: 'motor rev 100' -> set to -100
    int spaceIdx = cmd.indexOf(' ');
    String rest = (spaceIdx >= 0) ? cmd.substring(spaceIdx + 1) : "";
    rest.trim();
    // remove leading 'rev' or 'reverse'
    int subSpace = rest.indexOf(' ');
    String token = (subSpace >= 0) ? rest.substring(0, subSpace) : rest;
    String val = (subSpace >= 0) ? rest.substring(subSpace + 1) : "";
    token.trim(); val.trim();
    if (val.length() == 0) {
      // toggle direction at same magnitude
      if (motorSpeed == 0) {
        Serial.println("ERR: motor is stopped, provide speed to reverse");
      } else {
        setMotor(-motorSpeed);
        Serial.print("OK: motor direction toggled to "); Serial.println(motorSpeed);
      }
      return;
    } else {
      int sp = val.toInt();
      if (sp < 0 || sp > 255) {
        Serial.println("ERR: motor speed must be between 0 and 255 for reverse command");
      } else {
        setMotor(-sp);
        Serial.print("OK: motor set to reverse "); Serial.println(-sp);
      }
      return;
    }
  }

  // parse motor commands: 'motor <speed>' or 'motor brake' / 'motor coast' / 'motor stop'
  if (cmd.startsWith("motor ")) {
    String arg = cmd.substring(6);
    arg.trim();
    if (arg.length() == 0) {
      Serial.println("ERR: missing motor argument");
      return;
    }
    if (arg == "brake") {
      motorBrake();
      Serial.println("OK: motor braked");
      return;
    } else if (arg == "coast" || arg == "stop") {
      motorCoast();
      Serial.println("OK: motor coasting");
      return;
    } else {
      // expect a speed value
      int sp = arg.toInt();
      if (sp < -255 || sp > 255) {
        Serial.println("ERR: motor speed must be between -255 and 255");
      } else {
        setMotor(sp);
        Serial.print("OK: motor set to "); Serial.println(sp);
      }
      return;
    }
  }

  if (cmd == "start") {
    setRelay(true);
    Serial.println("OK: relay activated");
  } else if (cmd == "stop") {
    setRelay(false);
    Serial.println("OK: relay deactivated");
  } else if (cmd == "process") {
    Serial.println("PROCESS: starting sequence");
    // 1) motor trapdoor open/close
    motorTrapdoorOpenClose();
    Serial.println("PROCESS: motor trapdoor open/close done");
    // increment process counter
    processCount++;
    // 2) activate relay only on every 5th process
    if (processCount % 5 == 0) {
      // Turn relay ON, wait 5 seconds, then turn OFF
      setRelay(true);
      Serial.println("PROCESS: relay activated (5th run)");
      delay(5000); // wait 5 seconds while relay is ON
      setRelay(false);
      Serial.println("PROCESS: relay deactivated after 5s");
    } else {
      Serial.print("PROCESS: relay skipped (run "); Serial.print(processCount); Serial.println(")");
    }

    // 3) final servos always run
    quickServoBlink();
    Serial.println("PROCESS: final servos quick motion done");

    Serial.println("PROCESS: sequence complete");
  } else if (cmd == "toggle") {
    // read current state and toggle
    int cur = digitalRead(RELAY_PIN);
    bool on;
    if (RELAY_ACTIVE_HIGH) on = (cur == HIGH);
    else on = (cur == LOW);
    setRelay(!on);
    Serial.print("OK: relay toggled to ");
    Serial.println(!on ? "ON" : "OFF");
  } else if (cmd.length() == 0) {
    // ignore
  } else {
    Serial.print("ERR: unknown command: '");
    Serial.print(cmd);
    Serial.println("' (valid: start, stop, toggle)");
  }
}

// ----- Implementation -----
void setup() {
  // Initialize serial
  Serial.begin(115200);
  while (!Serial) {
    // wait (on some boards) for serial to become available
    delay(10);
  }

  // ----- Added for Bluetooth -----
  SerialBT.begin("MainSystemESP32"); // Set the Bluetooth device name
  Serial.println("Bluetooth device active, waiting for connections.");
  // -----------------------------

  // Initialize relay pin
  pinMode(RELAY_PIN, OUTPUT);
  // Ensure relay starts in OFF state
  if (RELAY_ACTIVE_HIGH) {
    digitalWrite(RELAY_PIN, LOW);
  } else {
    digitalWrite(RELAY_PIN, HIGH);
  }

  // Attach servos and set initial positions
  servo1.attach(SERVO1_PIN);
  servo2.attach(SERVO2_PIN);
  servo1.write(constrain(servo1Pos, 0, 180));
  servo2.write(constrain(servo2Pos, 0, 180));

  // Initialize BTS7960 motor driver
  motorInit();
  Serial.println("MainSystem ready. Send commands via USB Serial or Bluetooth.");
  Serial.println("Servo commands: 'servo1 <angle>', 'servo2 <angle>', 'servoAll <angle>' (0-180)");
  Serial.println("Motor commands: 'motor <speed>' (-255..255), 'motor brake', 'motor coast', 'motor stop'");
  Serial.println("Alternate: 'motor reverse <speed>' or 'motor rev <speed>' to set reverse direction, or 'motor rev' to toggle direction at current speed");
}


void loop() {
  // Check for an incoming line from USB Serial
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) {
      Serial.print("Received USB command: '");
      Serial.print(cmd);
      Serial.println("'");
      handleCommand(cmd);
    }
  }

  // Check for an incoming line from Bluetooth Serial
  if (SerialBT.available()) {
    String cmd = SerialBT.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) {
      Serial.print("Received Bluetooth command: '");
      Serial.print(cmd);
      Serial.println("'");
      handleCommand(cmd);
    }
  }

  // small delay to yield
  delay(10);
}