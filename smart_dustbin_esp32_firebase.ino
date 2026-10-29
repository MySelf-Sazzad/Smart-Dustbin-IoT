#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ESP32Servo.h>

// ---------------- WIFI & FIREBASE ----------------
// আপনার Wi-Fi / Hotspot-এর নাম এবং পাসওয়ার্ড দিন:
// (মনে রাখবেন: WPA2 হটস্পটের পাসওয়ার্ড কমপক্ষে ৮ অক্ষরের হতে হবে এবং হটস্পট 2.4 GHz হতে হবে)
const char* WIFI_SSID     = "Ami Gorib"; // মোবাইলে ওয়াইফাইয়ের যে নাম দেখাচ্ছে হুবহু এখানে লিখুন
const char* WIFI_PASSWORD = "12345678";

const char* FIREBASE_URL  = "https://smart-dustbin-iot-1af82-default-rtdb.asia-southeast1.firebasedatabase.app/dustbin.json";

// ---------------- PINS ----------------
#define IR_TOP         34
#define IR_FULL_METAL  35
#define IR_FULL_DRY    32
#define IR_FULL_WET    33
#define METAL_PIN      25
#define RAIN_AO        36
#define SERVO_PIN      19
#define BUZZER         23

const int IN1 = 16;
const int IN2 = 17;
const int IN3 = 21;
const int IN4 = 22;

// ---------------- BIN FULL SENSORS ----------------
// Metal, Dry, Wet তিনটি বাকেটই সক্রিয় (1):
#define FULL_SENSOR_METAL 1
#define FULL_SENSOR_DRY   1
#define FULL_SENSOR_WET   1

// ---------------- SERVO SETTINGS ----------------
const int GATE_CLOSED = 170;
const int GATE_OPEN   = 78;

const unsigned long DROP_MS = 5000;
const unsigned long SETTLE_MS = 1500;
const unsigned long SERVO_MOVE_MS = 900;

// ---------------- STEPPER SETTINGS ----------------
// 8-step half-step sequence: 4096 steps per revolution
const long STEPS_PER_REV = 4096;
const long BIN_STEPS = 1365; // approximately 120 degrees

// Default position = METAL
// DRY = RIGHT, WET = LEFT
const int DRY_DIR = 1;
const int WET_DIR = -1;
const long POS_METAL = 0;

const unsigned int STEP_DELAY_US = 3000;
const unsigned int STEP_START_DELAY_US = 6000;
const long RAMP_STEPS = 80;
const unsigned long SETTLE_AFTER_ROTATE_MS = 250;

const unsigned long RAIN_UPDATE_MS = 500;

// ---------------- 8-STEP SEQUENCE ----------------
// Same sequence as the working test code
const uint8_t seq[8][4] = {
  {1, 0, 0, 0},
  {1, 1, 0, 0},
  {0, 1, 0, 0},
  {0, 1, 1, 0},
  {0, 0, 1, 0},
  {0, 0, 1, 1},
  {0, 0, 0, 1},
  {1, 0, 0, 1}
};

Servo gate;

int seqIndex = 0;
long currentPos = 0;

int cntMetal = 0;
int cntDry = 0;
int cntWet = 0;

unsigned long lastStatus = 0;
bool busy = false;

volatile int rainLiveReading = 0;

// ---------------- FIREBASE SYNC ----------------
void sendToFirebase() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  if (https.begin(client, FIREBASE_URL)) {
    https.addHeader("Content-Type", "application/json");

    String json = "{";
    json += "\"metalCount\":" + String(cntMetal) + ",";
    json += "\"wetCount\":" + String(cntWet) + ",";
    json += "\"dryCount\":" + String(cntDry) + ",";
    json += "\"metalFull\":" + String(binFull(IR_FULL_METAL) ? "true" : "false") + ",";
    json += "\"wetFull\":" + String(binFull(IR_FULL_WET) ? "true" : "false") + ",";
    json += "\"dryFull\":" + String(binFull(IR_FULL_DRY) ? "true" : "false") + ",";
    json += "\"rainReading\":" + String(rainLiveReading) + ",";
    json += "\"updatedAt\":{\".sv\":\"timestamp\"}";
    json += "}";

    int code = https.PATCH(json);
    if (code > 0) {
      Serial.printf("[FIREBASE] Synced (HTTP %d)\n", code);
    }
    https.end();
  }
}

// ---------------- STEPPER CONTROL ----------------
void writeCoils(int index) {
  digitalWrite(IN1, seq[index][0]);
  digitalWrite(IN2, seq[index][1]);
  digitalWrite(IN3, seq[index][2]);
  digitalWrite(IN4, seq[index][3]);
}

void coilsOff() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

long wrapPos(long p) {
  p %= STEPS_PER_REV;
  if (p < 0) p += STEPS_PER_REV;
  return p;
}

void rotateSteps(long count, int dir) {
  if (dir != 1 && dir != -1) return;

  if (count <= 0) return;

  Serial.printf(
    "[STEPPER] Starting: steps=%ld direction=%s position=%ld\n",
    count,
    dir == 1 ? "RIGHT" : "LEFT",
    currentPos
  );

  for (long i = 0; i < count; i++) {
    // Move through the same 8-step sequence as the test
    seqIndex = (seqIndex + dir + 8) % 8;

    writeCoils(seqIndex);
    currentPos = wrapPos(currentPos + dir);

    // Smooth startup and slowdown
    long fromEnd = count - 1 - i;
    long ramp = (i < fromEnd) ? i : fromEnd;

    unsigned long stepDelay = STEP_DELAY_US;

    if (ramp < RAMP_STEPS) {
      stepDelay =
        STEP_START_DELAY_US -
        (unsigned long)(
          (STEP_START_DELAY_US - STEP_DELAY_US) * ramp
        ) / RAMP_STEPS;
    }

    delayMicroseconds(stepDelay);
  }

  coilsOff();

  Serial.printf(
    "[STEPPER] Finished. Position=%ld\n",
    currentPos
  );
}

void moveTo(long target) {
  target = wrapPos(target);

  long clockwise = wrapPos(target - currentPos);
  long counterClockwise = clockwise - STEPS_PER_REV;

  long delta =
    (labs(counterClockwise) < labs(clockwise))
      ? counterClockwise
      : clockwise;

  if (delta == 0) {
    Serial.println("[STEPPER] Already at target position.");
    return;
  }

  Serial.printf(
    "[STEPPER] Returning home: %ld steps, direction=%s\n",
    labs(delta),
    delta > 0 ? "RIGHT" : "LEFT"
  );

  rotateSteps(labs(delta), delta > 0 ? 1 : -1);
}

// ---------------- SERVO ----------------
void openGate() {
  Serial.printf("[GATE] OPEN -> %d degrees\n", GATE_OPEN);
  gate.write(GATE_OPEN);
  delay(SERVO_MOVE_MS);
}

void closeGate() {
  Serial.printf("[GATE] CLOSE -> %d degrees\n", GATE_CLOSED);
  gate.write(GATE_CLOSED);
  delay(SERVO_MOVE_MS);
}

// ---------------- BUZZER ----------------
void beep(int times, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(BUZZER, HIGH);
    delay(onMs);
    digitalWrite(BUZZER, LOW);

    if (i < times - 1) delay(offMs);
  }
}

// ---------------- IR SENSORS ----------------
// LOW = detected
bool irDetected(int pin) {
  int count = 0;

  for (int i = 0; i < 5; i++) {
    if (digitalRead(pin) == LOW) count++;
    delay(5);
  }

  return count >= 3;
}

bool topHasWaste() {
  return irDetected(IR_TOP);
}

bool binFull(int pin) {
  if (pin == IR_FULL_METAL)
    return FULL_SENSOR_METAL && irDetected(pin);

  if (pin == IR_FULL_DRY)
    return FULL_SENSOR_DRY && irDetected(pin);

  if (pin == IR_FULL_WET)
    return FULL_SENSOR_WET && irDetected(pin);

  return false;
}

// ---------------- RAIN MONITOR ----------------
void rainMonitorTask(void *parameter) {
  unsigned long lastRainUpdate = 0;

  while (true) {
    if (millis() - lastRainUpdate >= RAIN_UPDATE_MS) {
      lastRainUpdate = millis();

      int reading = analogRead(RAIN_AO);
      rainLiveReading = reading;

      Serial.printf("[RAIN LIVE] AO reading = %d\n", reading);
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

int readRainAverage() {
  long sum = 0;
  const int samples = 10;

  for (int i = 0; i < samples; i++) {
    sum += analogRead(RAIN_AO);
    delay(10);
  }

  return sum / samples;
}

// ---------------- STATUS ----------------
void printStatus() {
  Serial.printf(
    "[STATUS] Metal:%d full:%d | Dry:%d full:%d | Wet:%d full:%d | Position:%ld | Rain:%d\n",
    cntMetal, binFull(IR_FULL_METAL),
    cntDry, binFull(IR_FULL_DRY),
    cntWet, binFull(IR_FULL_WET),
    currentPos, (int)rainLiveReading
  );
}

// ---------------- PROCESS ONE ITEM ----------------
void processWaste() {
  Serial.println("\n========== NEW WASTE ==========");

  beep(1, 120, 0);
  delay(100);

  // Check metal sensor during the settling period
  bool metalSeen = false;
  unsigned long start = millis();

  while (millis() - start < SETTLE_MS) {
    if (digitalRead(METAL_PIN) == LOW) {
      metalSeen = true;
    }

    delay(10);
  }

  if (digitalRead(METAL_PIN) == LOW) {
    metalSeen = true;
  }

  // Rain sensor decides WET or DRY
  int rainAvg = readRainAverage();

  // Keeping your requested rule:
  // Below 4095 = WET
  // Exactly 4095 = DRY
  bool wetSeen = (rainAvg < 4095);

  const char* name;
  int fullPin;
  int* counter;

  if (metalSeen) {
    name = "METAL";
    fullPin = IR_FULL_METAL;
    counter = &cntMetal;
  } else if (wetSeen) {
    name = "WET";
    fullPin = IR_FULL_WET;
    counter = &cntWet;
  } else {
    name = "DRY";
    fullPin = IR_FULL_DRY;
    counter = &cntDry;
  }

  Serial.println("\n------ CLASSIFICATION ------");
  Serial.printf("CLASSIFICATION: %s\n", name);
  Serial.printf("Metal detected: %s\n",
                metalSeen ? "YES" : "NO");
  Serial.printf("Rain average: %d\n", rainAvg);
  Serial.println("----------------------------");

  // Check whether the selected bin is full
  // (Metal, Dry, Wet যে বাকেটই ফুল হোক না কেন, গেট ওপেন হবে না)
  if (binFull(fullPin)) {
    Serial.printf(
      "[WARNING] %s BIN FULL. Gate stays closed.\n",
      name
    );

    beep(3, 350, 200);

    // Sync to Firebase so dashboard shows FULL
    sendToFirebase();

    unsigned long clearStart = millis();

    while (topHasWaste() &&
           millis() - clearStart < 5000) {
      delay(100);
    }

    busy = false;
    return;
  }

  // ---------------- DESTINATION ----------------
  if (metalSeen) {
    // Metal is the home position: no stepper movement
    Serial.println("[DESTINATION] METAL - staying at home.");
  }
  else if (wetSeen) {
    Serial.printf(
      "[DESTINATION] WET - LEFT, %ld steps\n",
      BIN_STEPS
    );

    rotateSteps(BIN_STEPS, WET_DIR);
  }
  else {
    Serial.printf(
      "[DESTINATION] DRY - RIGHT, %ld steps\n",
      BIN_STEPS
    );

    rotateSteps(BIN_STEPS, DRY_DIR);
  }

  delay(SETTLE_AFTER_ROTATE_MS);

  // ---------------- DROP ITEM ----------------
  openGate();
  delay(DROP_MS);
  closeGate();

  // Return to metal/home position after DRY or WET
  if (!metalSeen) {
    Serial.println("[STEPPER] Returning to METAL home.");
    moveTo(POS_METAL);
  }

  (*counter)++;

  Serial.printf("[SUCCESS] Dropped into %s bin\n", name);
  printStatus();

  // Firebase-এ রিয়েলটাইমে আপডেট পাঠান
  sendToFirebase();

  // Wait for item to leave the top IR sensor
  unsigned long clearStart = millis();

  while (topHasWaste() &&
         millis() - clearStart < 5000) {
    delay(100);
  }

  if (topHasWaste()) {
    Serial.println(
      "[WARNING] Item still detected at top. Check blockage."
    );

    beep(5, 100, 100);
  }

  delay(250);

  Serial.println("========== CYCLE COMPLETE ==========");
  busy = false;
}

// ---------------- WIFI CONNECTION ----------------
void connectWiFi() {
  Serial.print("\nConnecting to Wi-Fi SSID: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  if (strlen(WIFI_PASSWORD) == 0) {
    WiFi.begin(WIFI_SSID);
  } else {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WIFI] Connected! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n[WIFI] Not connected. Will keep retrying in loop.");
  }
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);
  analogReadResolution(12);

  pinMode(IR_TOP, INPUT);
  pinMode(IR_FULL_METAL, INPUT);
  pinMode(IR_FULL_DRY, INPUT);
  pinMode(IR_FULL_WET, INPUT);

  pinMode(METAL_PIN, INPUT_PULLUP);
  pinMode(RAIN_AO, INPUT);

  pinMode(BUZZER, OUTPUT);
  digitalWrite(BUZZER, LOW);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  coilsOff();

  // Start servo
  gate.setPeriodHertz(50);
  gate.attach(SERVO_PIN, 500, 2400);
  gate.write(GATE_CLOSED);

  delay(1200);

  // Start rain monitor task
  xTaskCreatePinnedToCore(
    rainMonitorTask,
    "RainMonitor",
    2048,
    NULL,
    1,
    NULL,
    0
  );

  beep(2, 100, 100);

  // Connect to Wi-Fi
  connectWiFi();
  if (WiFi.status() == WL_CONNECTED) {
    sendToFirebase();
  }

  Serial.println("\n================================");
  Serial.println("       SMART DUSTBIN READY");
  Serial.println("================================");
  Serial.println("Stepper: 8-step sequence");
  Serial.println("DRY = RIGHT");
  Serial.println("WET = LEFT");
  Serial.println("METAL = stay at home");
  Serial.println("IR_TOP = object presence only");
  Serial.println("Rain average < 4095 = WET");
  Serial.println("Rain average = 4095 = DRY");
  Serial.println("Rain readings update every 500 ms");
  Serial.println("IMPORTANT: Platform must start at METAL home.");

  printStatus();
}

// ---------------- MAIN LOOP ----------------
void loop() {
  // Reconnect Wi-Fi if disconnected
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastRetry = 0;
    if (millis() - lastRetry >= 10000) {
      lastRetry = millis();
      Serial.println("[WIFI] Attempting reconnect...");
      if (strlen(WIFI_PASSWORD) == 0) {
        WiFi.begin(WIFI_SSID);
      } else {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      }
    }
  }

  if (millis() - lastStatus >= 5000) {
    lastStatus = millis();
    printStatus();
  }

  // Periodic Firebase update every 3 seconds for rain reading & dashboard sync
  static unsigned long lastFirebaseSync = 0;
  if (!busy && millis() - lastFirebaseSync >= 3000) {
    lastFirebaseSync = millis();
    sendToFirebase();
  }

  if (busy) {
    delay(30);
    return;
  }

  if (!topHasWaste()) {
    delay(30);
    return;
  }

  busy = true;
  processWaste();
}
