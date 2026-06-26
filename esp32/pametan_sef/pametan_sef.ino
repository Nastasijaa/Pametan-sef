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
const char FIRMWARE_VERSION[] = "smart-safe-2026-06-26-tilt-only-alarm";
const int SERVO_PIN = 13;
const int BUZZER_PIN = 14;
const int RED_LED_PIN = 23;
const int GREEN_LED_PIN = 4;
const int MPU_SDA_PIN = 18;
const int MPU_SCL_PIN = 19;

// Buzzer modul je active HIGH: HIGH = pisti, LOW = cuti.
const int BUZZER_ON_LEVEL = HIGH;
const int BUZZER_OFF_LEVEL = LOW;

// Servo uglovi. Po potrebi ih prilagodi mehanici brave.
const int SERVO_LOCKED_ANGLE = 90;
const int SERVO_UNLOCKED_ANGLE = 0;

// Pragovi alarma. Prilagoditi nakon realnog testiranja senzora.
const float TILT_THRESHOLD_DEG = 45.0;
const int TILT_CONFIRM_SAMPLES = 1;
const unsigned long SENSOR_UPDATE_INTERVAL_MS = 100;
const unsigned long FIREBASE_UPDATE_INTERVAL_MS = 300;
const unsigned long COMMAND_CHECK_INTERVAL_MS = 250;
const unsigned long ALARM_HOLD_MS = 3000;

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
Servo lockServo;
Adafruit_MPU6050 mpu;

bool safeLocked = true;
bool alarmActive = false;
bool alarmSilenced = false;
bool buzzerActiveNow = false;
bool tiltEventArmed = true;
int tiltAlarmSamples = 0;
float movementIntensity = 0.0;
float gyroIntensity = 0.0;
float baselineMovementIntensity = 0.0;
float baselineAx = 0.0;
float baselineAy = 0.0;
float baselineAz = 0.0;
float baselineAccelerationMagnitude = 9.81;
float tiltDegrees = 0.0;
float baselinePitch = 0.0;
float baselineRoll = 0.0;
String alarmReason = "Nema alarma";
unsigned long lastSensorUpdate = 0;
unsigned long lastFirebaseUpdate = 0;
unsigned long lastCommandCheck = 0;
unsigned long alarmHoldUntil = 0;
unsigned long firebasePausedUntil = 0;

float calculateMovementIntensity(float ax, float ay, float az);
float calculateMovementDelta(float ax, float ay, float az);
float calculateTiltFromBaseline(float ax, float ay, float az);
void applyLedState();
void moveServoTo(int angle);

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
  config.timeout.serverResponse = 5000;

  // Anonymous sign-up koristi API key. Ako projekat koristi legacy database secret,
  // moze se zameniti auth.token.uid/token pristupom po dokumentaciji biblioteke.
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase prijava uspesna.");
  } else {
    Serial.printf("Firebase prijava nije uspela: %s\n", config.signer.signupError.message.c_str());
    Serial.println("Nastavljam Firebase u test mode-u. Realtime Database rules moraju dozvoliti read/write.");
    config.signer.test_mode = true;
  }

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

bool canUseFirebase() {
  return WiFi.status() == WL_CONNECTED && Firebase.ready() && millis() > firebasePausedUntil;
}

void pauseFirebase(const char *context) {
  firebasePausedUntil = millis() + 1000;
  Serial.print("Firebase pauza 1s: ");
  Serial.print(context);
  Serial.print(" / ");
  Serial.println(fbdo.errorReason());
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
  baselineAccelerationMagnitude = sqrt(
    (baselineAx * baselineAx) + (baselineAy * baselineAy) + (baselineAz * baselineAz)
  );

  Serial.print("MPU pocetni pitch=");
  Serial.print(baselinePitch);
  Serial.print(" roll=");
  Serial.print(baselineRoll);
  Serial.print(" movement=");
  Serial.println(baselineMovementIntensity);
}

void writeEvent(const String &type, const String &message) {
  if (!canUseFirebase()) {
    return;
  }

  FirebaseJson event;
  event.set("type", type);
  event.set("message", message);
  event.set("timestamp", (double)time(nullptr) * 1000.0);

  if (!Firebase.RTDB.pushJSON(&fbdo, "/events", &event)) {
    pauseFirebase("events");
  }
}

void setSafeStatus(bool locked) {
  safeLocked = locked;
  applyLedState();

  int servoAngle = locked ? SERVO_LOCKED_ANGLE : SERVO_UNLOCKED_ANGLE;
  moveServoTo(servoAngle);
  Serial.print("Servo ugao=");
  Serial.println(servoAngle);

  if (canUseFirebase()) {
    if (!Firebase.RTDB.setString(&fbdo, "/safe/status", locked ? "locked" : "unlocked")) {
      pauseFirebase("safe/status");
    }
  }
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
  pinMode(BUZZER_PIN, OUTPUT);
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

float calculateTiltFromBaseline(float ax, float ay, float az) {
  float currentMagnitude = sqrt((ax * ax) + (ay * ay) + (az * az));

  if (baselineAccelerationMagnitude < 0.1 || currentMagnitude < 0.1) {
    return 0.0;
  }

  float dotProduct = (baselineAx * ax) + (baselineAy * ay) + (baselineAz * az);
  float cosAngle = dotProduct / (baselineAccelerationMagnitude * currentMagnitude);
  cosAngle = constrain(cosAngle, -1.0, 1.0);

  return acos(cosAngle) * 180.0 / PI;
}

float calculateGyroIntensity(float gx, float gy, float gz) {
  return sqrt((gx * gx) + (gy * gy) + (gz * gz));
}

void activateAlarm(const String &reason) {
  alarmReason = reason;
  alarmActive = true;
  alarmHoldUntil = millis() + ALARM_HOLD_MS;
}

void startBuzzerBeep(unsigned long now) {
  buzzerActiveNow = true;
  alarmSilenced = false;
  setBuzzer(true);
}

void applyLedState() {
  if (safeLocked) {
    digitalWrite(RED_LED_PIN, HIGH);
    digitalWrite(GREEN_LED_PIN, LOW);
  } else {
    digitalWrite(RED_LED_PIN, LOW);
    digitalWrite(GREEN_LED_PIN, HIGH);
  }
}

void updateOutputs() {
  buzzerActiveNow = alarmActive && !alarmSilenced;
  applyLedState();
  setBuzzer(buzzerActiveNow);
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
  movementIntensity = max(calculateMovementDelta(ax, ay, az), gyroIntensity);

  // Nagib je ugao izmedju pocetnog ravnog polozaja i trenutnog polozaja.
  tiltDegrees = calculateTiltFromBaseline(ax, ay, az);

  if (tiltDegrees >= TILT_THRESHOLD_DEG) {
    tiltAlarmSamples++;
  } else {
    tiltAlarmSamples = 0;
  }

  bool tiltAlarmNow = tiltAlarmSamples >= TILT_CONFIRM_SAMPLES;
  bool detectedAlarm = tiltAlarmNow;

  if (tiltAlarmNow && tiltEventArmed) {
    startBuzzerBeep(millis());
    tiltEventArmed = false;
  }

  if (tiltDegrees < TILT_THRESHOLD_DEG - 5.0) {
    tiltEventArmed = true;
  }

  if (tiltAlarmNow) {
    alarmReason = "Nagib preko 45 stepeni";
  } else {
    alarmReason = "Nema alarma";
  }

  if (detectedAlarm && !alarmActive) {
    alarmSilenced = false;
    activateAlarm(alarmReason);
    writeEvent("alarm", alarmReason);
  } else if (detectedAlarm) {
    alarmHoldUntil = millis() + ALARM_HOLD_MS;
  } else if (alarmActive && millis() > alarmHoldUntil) {
    alarmActive = false;
    alarmSilenced = false;
    alarmReason = "Nema alarma";
  }

  buzzerActiveNow = alarmActive && !alarmSilenced;

  Serial.print("movement=");
  Serial.print(movementIntensity);
  Serial.print(" tilt=");
  Serial.print(tiltDegrees);
  Serial.print(" gyro=");
  Serial.print(gyroIntensity);
  Serial.print(" tiltAlarm=");
  Serial.print(tiltAlarmNow ? "true" : "false");
  Serial.print(" tiltArmed=");
  Serial.print(tiltEventArmed ? "true" : "false");
  Serial.print(" buzzer=");
  Serial.print(buzzerActiveNow ? "true" : "false");
  Serial.print(" alarmSilenced=");
  Serial.print(alarmSilenced ? "true" : "false");
  Serial.print(" alarm=");
  Serial.print(alarmActive ? "true" : "false");
  Serial.print(" reason=");
  Serial.println(alarmReason);
}

void sendSensorState() {
  if (!canUseFirebase()) {
    return;
  }

  FirebaseJson safeJson;
  safeJson.set("movement", movementIntensity);
  safeJson.set("tilt", tiltDegrees);
  safeJson.set("gyro", gyroIntensity);
  safeJson.set("status", safeLocked ? "locked" : "unlocked");
  safeJson.set("alarm", alarmActive);
  safeJson.set("alarmSilenced", alarmSilenced);
  safeJson.set("buzzer", buzzerActiveNow);
  safeJson.set("alarmReason", alarmReason);
  safeJson.set("lastEvent", alarmReason);

  if (!Firebase.RTDB.updateNode(&fbdo, "/safe", &safeJson)) {
    pauseFirebase("safe update");
  }
}

void handleCommands() {
  if (!canUseFirebase()) {
    return;
  }

  bool lockCommand = false;
  bool unlockCommand = false;
  bool silenceAlarmCommand = false;

  if (Firebase.RTDB.getBool(&fbdo, "/commands/lock")) {
    lockCommand = fbdo.boolData();
  } else {
    pauseFirebase("commands/lock");
  }

  if (Firebase.RTDB.getBool(&fbdo, "/commands/unlock")) {
    unlockCommand = fbdo.boolData();
  } else {
    pauseFirebase("commands/unlock");
  }

  if (Firebase.RTDB.getBool(&fbdo, "/commands/silenceAlarm")) {
    silenceAlarmCommand = fbdo.boolData();
  } else {
    pauseFirebase("commands/silenceAlarm");
  }

  if (lockCommand) {
    Serial.println("Komanda: zakljucaj");
    setSafeStatus(true);
    if (canUseFirebase() && !Firebase.RTDB.setBool(&fbdo, "/commands/lock", false)) {
      pauseFirebase("reset commands/lock");
    }
    if (canUseFirebase() && !Firebase.RTDB.setString(&fbdo, "/safe/lastEvent", "Sef zakljucan komandom")) {
      pauseFirebase("safe/lastEvent lock");
    }
    writeEvent("lock", "Sef je zakljucan iz web aplikacije");
  }

  if (unlockCommand) {
    Serial.println("Komanda: otkljucaj");
    setSafeStatus(false);
    if (canUseFirebase() && !Firebase.RTDB.setString(&fbdo, "/safe/lastEvent", "Sef otkljucan komandom")) {
      pauseFirebase("safe/lastEvent unlock");
    }
    writeEvent("unlock", "Sef je otkljucan iz web aplikacije");
    if (canUseFirebase() && !Firebase.RTDB.setBool(&fbdo, "/commands/unlock", false)) {
      pauseFirebase("reset commands/unlock");
    }
  }

  if (silenceAlarmCommand) {
    Serial.println("Komanda: utisaj alarm");
    alarmSilenced = true;
    buzzerActiveNow = false;
    setBuzzer(false);
    if (canUseFirebase() && !Firebase.RTDB.setBool(&fbdo, "/commands/silenceAlarm", false)) {
      pauseFirebase("reset commands/silenceAlarm");
    }
    if (canUseFirebase() && !Firebase.RTDB.setBool(&fbdo, "/safe/alarmSilenced", true)) {
      pauseFirebase("safe/alarmSilenced");
    }
    if (canUseFirebase() && !Firebase.RTDB.setString(&fbdo, "/safe/lastEvent", "Alarm utisan PIN kodom")) {
      pauseFirebase("safe/lastEvent silence");
    }
    writeEvent("alarm_silenced", "Alarm je utisan ispravnim PIN kodom");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.print("Firmware: ");
  Serial.println(FIRMWARE_VERSION);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  setBuzzer(false);
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(GREEN_LED_PIN, HIGH);
  delay(300);

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

  if (canUseFirebase()) {
    if (!Firebase.RTDB.setBool(&fbdo, "/safe/alarm", false)) {
      pauseFirebase("setup safe/alarm");
    }
    if (canUseFirebase() && !Firebase.RTDB.setBool(&fbdo, "/safe/alarmSilenced", false)) {
      pauseFirebase("setup safe/alarmSilenced");
    }
    if (canUseFirebase() && !Firebase.RTDB.setBool(&fbdo, "/safe/buzzer", false)) {
      pauseFirebase("setup safe/buzzer");
    }
    if (canUseFirebase() && !Firebase.RTDB.setString(&fbdo, "/safe/lastEvent", "Sistem pokrenut")) {
      pauseFirebase("setup lastEvent");
    }
    if (canUseFirebase() && !Firebase.RTDB.setBool(&fbdo, "/commands/lock", false)) {
      pauseFirebase("setup commands/lock");
    }
    if (canUseFirebase() && !Firebase.RTDB.setBool(&fbdo, "/commands/unlock", false)) {
      pauseFirebase("setup commands/unlock");
    }
    if (canUseFirebase() && !Firebase.RTDB.setBool(&fbdo, "/commands/silenceAlarm", false)) {
      pauseFirebase("setup commands/silenceAlarm");
    }
  }
  writeEvent("system", "ESP32 sistem pametnog sefa je pokrenut");
}

void loop() {
  unsigned long now = millis();
  updateOutputs();

  if (now - lastSensorUpdate >= SENSOR_UPDATE_INTERVAL_MS) {
    lastSensorUpdate = now;
    readSensors();
  }

  if (now - lastCommandCheck >= COMMAND_CHECK_INTERVAL_MS) {
    lastCommandCheck = now;
    handleCommands();
  }

  if (now - lastFirebaseUpdate >= FIREBASE_UPDATE_INTERVAL_MS) {
    lastFirebaseUpdate = now;
    sendSensorState();
  }
}
