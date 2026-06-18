/*
  Pametan sef / Smart Anti-Theft Safe
  ESP32 DevKit + Firebase Realtime Database

  Biblioteke:
  - Firebase ESP Client by Mobizt
  - ESP32Servo
  - Adafruit MPU6050
  - Adafruit Unified Sensor
*/

#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <time.h>

// Wi-Fi placeholder podaci.
#define WIFI_SSID "Nastasija"
#define WIFI_PASSWORD "27042004"

// Firebase placeholder podaci.
// Primer DATABASE_URL: https://ime-projekta-default-rtdb.europe-west1.firebasedatabase.app/
#define DATABASE_URL "https://pametan-sef-default-rtdb.europe-west1.firebasedatabase.app/"
#define FIREBASE_API_KEY "AIzaSyAalY6JMgPDSQB88GBPZUiAtMfelzE3puk"

// Pinovi komponenti.
const int SERVO_PIN = 13;
const int BUZZER_PIN = 14;
const int RED_LED_PIN = 4;
const int GREEN_LED_PIN = 23;
const int MPU_SDA_PIN = 18;
const int MPU_SCL_PIN = 19;
const int VIBRATION_PIN = 15;

// Za obican buzzer: HIGH = pisti, LOW = cuti.
// Ako tvoj modul radi obrnuto, zameni ove dve vrednosti.
const int BUZZER_ON_LEVEL = HIGH;
const int BUZZER_OFF_LEVEL = LOW;

// Servo uglovi. Po potrebi ih prilagodi mehanici brave.
const int SERVO_LOCKED_ANGLE = 80;
const int SERVO_UNLOCKED_ANGLE = 10;

// Pragovi alarma. Prilagoditi nakon realnog testiranja senzora.
const float MOVEMENT_THRESHOLD = 3.0;
const float GYRO_THRESHOLD = 1.8;
const float TILT_THRESHOLD_DEG = 50.0;
const unsigned long SENSOR_UPDATE_INTERVAL_MS = 300;
const unsigned long COMMAND_CHECK_INTERVAL_MS = 500;
const unsigned long SHOCK_CHECK_INTERVAL_MS = 20;
const unsigned long ALARM_HOLD_MS = 3000;
const unsigned long SHOCK_REARM_MS = 800;
const unsigned long UNLOCK_BEEP_MS = 500;

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
Servo lockServo;
Adafruit_MPU6050 mpu;

bool safeLocked = true;
bool alarmActive = false;
bool vibrationDetected = false;
bool vibrationLatched = false;
bool movementLatched = false;
volatile bool vibrationInterruptLatched = false;
int vibrationNormalLevel = HIGH;
bool lastVibrationActive = false;
bool vibrationArmed = true;
float movementIntensity = 0.0;
float gyroIntensity = 0.0;
float baselineMovementIntensity = 0.0;
float baselineAx = 0.0;
float baselineAy = 0.0;
float baselineAz = 0.0;
float latchedMovementIntensity = 0.0;
float tiltDegrees = 0.0;
float baselinePitch = 0.0;
float baselineRoll = 0.0;
String alarmReason = "Nema alarma";
unsigned long lastSensorUpdate = 0;
unsigned long lastCommandCheck = 0;
unsigned long lastShockCheck = 0;
unsigned long alarmHoldUntil = 0;
unsigned long beepUntil = 0;
unsigned long vibrationDisplayUntil = 0;
unsigned long vibrationNormalSince = 0;

float calculateMovementIntensity(float ax, float ay, float az);
float calculateMovementDelta(float ax, float ay, float az);
void moveServoTo(int angle);

void IRAM_ATTR onVibrationChange() {
  vibrationInterruptLatched = true;
}

void connectToWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  Serial.println("Skeniram Wi-Fi mreze...");
  int networkCount = WiFi.scanNetworks();
  if (networkCount <= 0) {
    Serial.println("Nijedna Wi-Fi mreza nije pronadjena.");
  } else {
    Serial.println("Pronadjene Wi-Fi mreze:");
    for (int i = 0; i < networkCount; i++) {
      Serial.print("- ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (signal ");
      Serial.print(WiFi.RSSI(i));
      Serial.println(" dBm)");
    }
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Povezivanje na Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Wi-Fi povezan, IP: ");
  Serial.println(WiFi.localIP());

  // NTP vreme se koristi za timestamp u Firebase events istoriji.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

void setupFirebase() {
  config.api_key = FIREBASE_API_KEY;
  config.database_url = DATABASE_URL;

  // Anonymous sign-up koristi API key. Ako projekat koristi legacy database secret,
  // moze se zameniti auth.token.uid/token pristupom po dokumentaciji biblioteke.
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase prijava uspesna.");
  } else {
    Serial.printf("Firebase prijava nije uspela: %s\n", config.signer.signupError.message.c_str());
  }

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

void setupMpu6050() {
  Wire.begin(MPU_SDA_PIN, MPU_SCL_PIN);

  if (!mpu.begin()) {
    Serial.println("MPU6050 nije pronadjen. Proveri SDA/SCL pinove i napajanje.");
    while (true) {
      digitalWrite(RED_LED_PIN, !digitalRead(RED_LED_PIN));
      delay(250);
    }
  }

  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
}

void calculateAngles(float ax, float ay, float az, float &pitch, float &roll) {
  pitch = atan2(ax, sqrt((ay * ay) + (az * az))) * 180.0 / PI;
  roll = atan2(ay, sqrt((ax * ax) + (az * az))) * 180.0 / PI;
}

void calibrateMpuBaseline() {
  float pitchSum = 0.0;
  float rollSum = 0.0;
  float movementSum = 0.0;
  float axSum = 0.0;
  float aySum = 0.0;
  float azSum = 0.0;
  const int sampleCount = 30;

  for (int i = 0; i < sampleCount; i++) {
    sensors_event_t acceleration;
    sensors_event_t gyro;
    sensors_event_t temperature;
    mpu.getEvent(&acceleration, &gyro, &temperature);

    float pitch;
    float roll;
    calculateAngles(
      acceleration.acceleration.x,
      acceleration.acceleration.y,
      acceleration.acceleration.z,
      pitch,
      roll
    );

    pitchSum += pitch;
    rollSum += roll;
    axSum += acceleration.acceleration.x;
    aySum += acceleration.acceleration.y;
    azSum += acceleration.acceleration.z;
    movementSum += calculateMovementIntensity(
      acceleration.acceleration.x,
      acceleration.acceleration.y,
      acceleration.acceleration.z
    );
    delay(30);
  }

  baselinePitch = pitchSum / sampleCount;
  baselineRoll = rollSum / sampleCount;
  baselineMovementIntensity = movementSum / sampleCount;
  baselineAx = axSum / sampleCount;
  baselineAy = aySum / sampleCount;
  baselineAz = azSum / sampleCount;

  Serial.print("MPU pocetni pitch=");
  Serial.print(baselinePitch);
  Serial.print(" roll=");
  Serial.print(baselineRoll);
  Serial.print(" movement=");
  Serial.println(baselineMovementIntensity);
}

void writeEvent(const String &type, const String &message) {
  if (!Firebase.ready()) {
    return;
  }

  FirebaseJson event;
  event.set("type", type);
  event.set("message", message);
  event.set("timestamp", (double)time(nullptr) * 1000.0);

  if (!Firebase.RTDB.pushJSON(&fbdo, "/events", &event)) {
    Serial.printf("Greska pri upisu dogadjaja: %s\n", fbdo.errorReason().c_str());
  }
}

void setSafeStatus(bool locked) {
  safeLocked = locked;
  int servoAngle = locked ? SERVO_LOCKED_ANGLE : SERVO_UNLOCKED_ANGLE;
  moveServoTo(servoAngle);
  Serial.print("Servo ugao=");
  Serial.println(servoAngle);

  if (Firebase.ready()) {
    Firebase.RTDB.setString(&fbdo, "/safe/status", locked ? "locked" : "unlocked");
  }
}

void beep(unsigned long durationMs) {
  beepUntil = millis() + durationMs;
}

void moveServoTo(int angle) {
  if (!lockServo.attached()) {
    lockServo.attach(SERVO_PIN, 500, 2400);
  }

  lockServo.write(angle);
  delay(650);
  lockServo.detach();
}

void setBuzzer(bool enabled) {
  digitalWrite(BUZZER_PIN, enabled ? BUZZER_ON_LEVEL : BUZZER_OFF_LEVEL);
}

float calculateMovementIntensity(float ax, float ay, float az) {
  float totalAcceleration = sqrt((ax * ax) + (ay * ay) + (az * az));
  return abs(totalAcceleration - 9.81) * 10.0;
}

float calculateMovementDelta(float ax, float ay, float az) {
  float dx = ax - baselineAx;
  float dy = ay - baselineAy;
  float dz = az - baselineAz;
  return sqrt((dx * dx) + (dy * dy) + (dz * dz)) * 10.0;
}

float calculateGyroIntensity(float gx, float gy, float gz) {
  return sqrt((gx * gx) + (gy * gy) + (gz * gz));
}

void activateAlarm(const String &reason) {
  alarmReason = reason;
  alarmActive = true;
  alarmHoldUntil = millis() + ALARM_HOLD_MS;
}

void updateShockLatch() {
  unsigned long now = millis();
  bool interruptDetected = vibrationInterruptLatched;
  vibrationInterruptLatched = false;
  bool vibrationActive = digitalRead(VIBRATION_PIN) != vibrationNormalLevel;

  if (!vibrationActive) {
    if (lastVibrationActive) {
      vibrationNormalSince = now;
    }

    if (!vibrationArmed && now - vibrationNormalSince >= SHOCK_REARM_MS) {
      vibrationArmed = true;
    }
  }

  if (vibrationArmed && (interruptDetected || (vibrationActive && !lastVibrationActive))) {
    vibrationLatched = true;
    vibrationArmed = false;
    vibrationDisplayUntil = millis() + ALARM_HOLD_MS;
  }
  lastVibrationActive = vibrationActive;

  if (now - lastShockCheck < SHOCK_CHECK_INTERVAL_MS) {
    return;
  }

  lastShockCheck = now;

  sensors_event_t acceleration;
  sensors_event_t gyro;
  sensors_event_t temperature;
  mpu.getEvent(&acceleration, &gyro, &temperature);

  float sampledMovement = calculateMovementDelta(
    acceleration.acceleration.x,
    acceleration.acceleration.y,
    acceleration.acceleration.z
  );
  float sampledGyro = calculateGyroIntensity(
    gyro.gyro.x,
    gyro.gyro.y,
    gyro.gyro.z
  );

  if (sampledMovement >= MOVEMENT_THRESHOLD || sampledGyro >= GYRO_THRESHOLD) {
    movementLatched = true;
    latchedMovementIntensity = max(latchedMovementIntensity, max(sampledMovement, sampledGyro));
  }
}

void updateOutputs() {
  // Zakljucano = zelena LED, otkljucano = crvena LED.
  digitalWrite(RED_LED_PIN, safeLocked ? LOW : HIGH);
  digitalWrite(GREEN_LED_PIN, safeLocked ? HIGH : LOW);

  if (alarmActive || millis() < beepUntil) {
    setBuzzer(true);
  } else {
    setBuzzer(false);
  }
}

void readSensors() {
  sensors_event_t acceleration;
  sensors_event_t gyro;
  sensors_event_t temperature;
  mpu.getEvent(&acceleration, &gyro, &temperature);

  float ax = acceleration.acceleration.x;
  float ay = acceleration.acceleration.y;
  float az = acceleration.acceleration.z;
  gyroIntensity = calculateGyroIntensity(gyro.gyro.x, gyro.gyro.y, gyro.gyro.z);

  // Intenzitet pomeranja je odstupanje ukupnog ubrzanja od gravitacije.
  movementIntensity = max(
    max(calculateMovementDelta(ax, ay, az), gyroIntensity),
    latchedMovementIntensity
  );
  movementLatched = false;
  latchedMovementIntensity = 0.0;

  // Nagib je promena u odnosu na polozaj pri pokretanju uredjaja.
  float pitch;
  float roll;
  calculateAngles(ax, ay, az, pitch, roll);
  tiltDegrees = max(abs(pitch - baselinePitch), abs(roll - baselineRoll));

  bool shockDetectedNow = vibrationLatched;
  vibrationDetected = shockDetectedNow || millis() < vibrationDisplayUntil;
  vibrationLatched = false;

  bool detectedAlarm = shockDetectedNow;

  if (vibrationDetected) {
    alarmReason = "Detektovana vibracija";
  } else {
    alarmReason = "Nema alarma";
  }

  if (detectedAlarm && !alarmActive) {
    activateAlarm(alarmReason);
    Firebase.RTDB.setBool(&fbdo, "/safe/alarm", true);
    Firebase.RTDB.setString(&fbdo, "/safe/lastEvent", alarmReason);
    writeEvent("alarm", alarmReason);
  } else if (detectedAlarm) {
    alarmHoldUntil = millis() + ALARM_HOLD_MS;
  } else if (alarmActive && millis() > alarmHoldUntil) {
    alarmActive = false;
    Firebase.RTDB.setBool(&fbdo, "/safe/alarm", false);
    Firebase.RTDB.setString(&fbdo, "/safe/lastEvent", "Senzori su normalni");
  }

  Serial.print("movement=");
  Serial.print(movementIntensity);
  Serial.print(" tilt=");
  Serial.print(tiltDegrees);
  Serial.print(" gyro=");
  Serial.print(gyroIntensity);
  Serial.print(" vibration=");
  Serial.print(vibrationDetected ? "true" : "false");
  Serial.print(" vibRaw=");
  Serial.print(digitalRead(VIBRATION_PIN));
  Serial.print(" alarm=");
  Serial.print(alarmActive ? "true" : "false");
  Serial.print(" reason=");
  Serial.println(alarmReason);
}

void sendSensorState() {
  if (!Firebase.ready()) {
    return;
  }

  FirebaseJson safeJson;
  safeJson.set("movement", movementIntensity);
  safeJson.set("tilt", tiltDegrees);
  safeJson.set("gyro", gyroIntensity);
  safeJson.set("vibration", vibrationDetected);
  safeJson.set("status", safeLocked ? "locked" : "unlocked");
  safeJson.set("alarm", alarmActive);
  safeJson.set("alarmReason", alarmReason);

  if (!Firebase.RTDB.updateNode(&fbdo, "/safe", &safeJson)) {
    Serial.printf("Greska pri slanju senzora: %s\n", fbdo.errorReason().c_str());
  }
}

void handleCommands() {
  if (!Firebase.ready()) {
    return;
  }

  bool lockCommand = false;
  bool unlockCommand = false;

  if (Firebase.RTDB.getBool(&fbdo, "/commands/lock")) {
    lockCommand = fbdo.boolData();
  }

  if (Firebase.RTDB.getBool(&fbdo, "/commands/unlock")) {
    unlockCommand = fbdo.boolData();
  }

  if (lockCommand) {
    Serial.println("Komanda: zakljucaj");
    setSafeStatus(true);
    Firebase.RTDB.setBool(&fbdo, "/commands/lock", false);
    Firebase.RTDB.setString(&fbdo, "/safe/lastEvent", "Sef zakljucan komandom");
    writeEvent("lock", "Sef je zakljucan iz web aplikacije");
  }

  if (unlockCommand) {
    Serial.println("Komanda: otkljucaj");
    setSafeStatus(false);
    Firebase.RTDB.setString(&fbdo, "/safe/lastEvent", "Sef otkljucan komandom");
    writeEvent("unlock", "Sef je otkljucan iz web aplikacije");
    beep(UNLOCK_BEEP_MS);
    Firebase.RTDB.setBool(&fbdo, "/commands/unlock", false);
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(VIBRATION_PIN, INPUT_PULLUP);
  digitalWrite(BUZZER_PIN, BUZZER_OFF_LEVEL);
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(GREEN_LED_PIN, HIGH);
  delay(300);
  vibrationNormalLevel = digitalRead(VIBRATION_PIN);
  vibrationNormalSince = millis();
  attachInterrupt(digitalPinToInterrupt(VIBRATION_PIN), onVibrationChange, CHANGE);

  lockServo.setPeriodHertz(50);
  setSafeStatus(true);
  delay(500);
  setSafeStatus(false);
  delay(500);
  setSafeStatus(true);

  setupMpu6050();
  calibrateMpuBaseline();
  connectToWifi();
  setupFirebase();

  Firebase.RTDB.setBool(&fbdo, "/safe/alarm", false);
  Firebase.RTDB.setString(&fbdo, "/safe/lastEvent", "Sistem pokrenut");
  Firebase.RTDB.setBool(&fbdo, "/commands/lock", false);
  Firebase.RTDB.setBool(&fbdo, "/commands/unlock", false);
  writeEvent("system", "ESP32 sistem pametnog sefa je pokrenut");
}

void loop() {
  unsigned long now = millis();

  updateShockLatch();

  if (now - lastSensorUpdate >= SENSOR_UPDATE_INTERVAL_MS) {
    lastSensorUpdate = now;
    readSensors();
    sendSensorState();
  }

  if (now - lastCommandCheck >= COMMAND_CHECK_INTERVAL_MS) {
    lastCommandCheck = now;
    handleCommands();
  }

  updateOutputs();
}
