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
#define WIFI_SSID "WIFI_SSID"
#define WIFI_PASSWORD "WIFI_PASSWORD"

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

// Servo uglovi. Po potrebi ih prilagodi mehanici brave.
const int SERVO_LOCKED_ANGLE = 10;
const int SERVO_UNLOCKED_ANGLE = 95;

// Pragovi alarma. Prilagoditi nakon realnog testiranja senzora.
const float MOVEMENT_THRESHOLD = 22.0;
const float TILT_THRESHOLD_DEG = 25.0;
const unsigned long SENSOR_UPDATE_INTERVAL_MS = 1000;
const unsigned long COMMAND_CHECK_INTERVAL_MS = 500;
const unsigned long BUZZER_ALARM_MS = 160;

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
Servo lockServo;
Adafruit_MPU6050 mpu;

bool safeLocked = true;
bool alarmActive = false;
bool vibrationDetected = false;
float movementIntensity = 0.0;
float tiltDegrees = 0.0;
unsigned long lastSensorUpdate = 0;
unsigned long lastCommandCheck = 0;
unsigned long lastBuzzerToggle = 0;
bool buzzerState = false;

void connectToWifi() {
  WiFi.mode(WIFI_STA);
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
  lockServo.write(locked ? SERVO_LOCKED_ANGLE : SERVO_UNLOCKED_ANGLE);

  if (Firebase.ready()) {
    Firebase.RTDB.setString(&fbdo, "/safe/status", locked ? "locked" : "unlocked");
  }
}

void updateOutputs() {
  if (alarmActive) {
    digitalWrite(RED_LED_PIN, HIGH);
    digitalWrite(GREEN_LED_PIN, LOW);
    lockServo.write(SERVO_LOCKED_ANGLE);
    safeLocked = true;

    if (millis() - lastBuzzerToggle >= BUZZER_ALARM_MS) {
      lastBuzzerToggle = millis();
      buzzerState = !buzzerState;
      digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
    }
    return;
  }

  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(GREEN_LED_PIN, HIGH);
  digitalWrite(BUZZER_PIN, LOW);
  buzzerState = false;
}

void readSensors() {
  sensors_event_t acceleration;
  sensors_event_t gyro;
  sensors_event_t temperature;
  mpu.getEvent(&acceleration, &gyro, &temperature);

  float ax = acceleration.acceleration.x;
  float ay = acceleration.acceleration.y;
  float az = acceleration.acceleration.z;

  // Intenzitet pomeranja je odstupanje ukupnog ubrzanja od gravitacije.
  float totalAcceleration = sqrt((ax * ax) + (ay * ay) + (az * az));
  movementIntensity = abs(totalAcceleration - 9.81) * 10.0;

  // Nagib racunamo iz odnosa X/Y osa prema Z osi.
  float pitch = atan2(ax, sqrt((ay * ay) + (az * az))) * 180.0 / PI;
  float roll = atan2(ay, sqrt((ax * ax) + (az * az))) * 180.0 / PI;
  tiltDegrees = max(abs(pitch), abs(roll));

  vibrationDetected = digitalRead(VIBRATION_PIN) == HIGH;

  bool detectedAlarm = vibrationDetected ||
                       movementIntensity >= MOVEMENT_THRESHOLD ||
                       tiltDegrees >= TILT_THRESHOLD_DEG;

  if (detectedAlarm && !alarmActive) {
    alarmActive = true;
    safeLocked = true;
    lockServo.write(SERVO_LOCKED_ANGLE);
    Firebase.RTDB.setBool(&fbdo, "/safe/alarm", true);
    Firebase.RTDB.setString(&fbdo, "/safe/status", "locked");
    Firebase.RTDB.setString(&fbdo, "/safe/lastEvent", "Detektovan pokusaj obijanja");
    writeEvent("alarm", "Detektovan udarac, veliko pomeranje ili nagib sefa");
  } else if (!detectedAlarm && alarmActive) {
    alarmActive = false;
    Firebase.RTDB.setBool(&fbdo, "/safe/alarm", false);
    Firebase.RTDB.setString(&fbdo, "/safe/lastEvent", "Senzori su normalni");
  }
}

void sendSensorState() {
  if (!Firebase.ready()) {
    return;
  }

  FirebaseJson safeJson;
  safeJson.set("movement", movementIntensity);
  safeJson.set("tilt", tiltDegrees);
  safeJson.set("vibration", vibrationDetected);
  safeJson.set("status", safeLocked ? "locked" : "unlocked");
  safeJson.set("alarm", alarmActive);
  safeJson.set("lastEvent", alarmActive ? "Alarm aktivan" : "Sef radi normalno");

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
    setSafeStatus(true);
    Firebase.RTDB.setBool(&fbdo, "/commands/lock", false);
    Firebase.RTDB.setString(&fbdo, "/safe/lastEvent", "Sef zakljucan komandom");
    writeEvent("lock", "Sef je zakljucan iz web aplikacije");
  }

  if (unlockCommand) {
    if (!alarmActive) {
      setSafeStatus(false);
      Firebase.RTDB.setString(&fbdo, "/safe/lastEvent", "Sef otkljucan komandom");
      writeEvent("unlock", "Sef je otkljucan iz web aplikacije");
    } else {
      Firebase.RTDB.setString(&fbdo, "/safe/lastEvent", "Otkljucavanje odbijeno dok je alarm aktivan");
      writeEvent("blocked_unlock", "Otkljucavanje odbijeno jer je alarm aktivan");
    }

    Firebase.RTDB.setBool(&fbdo, "/commands/unlock", false);
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(VIBRATION_PIN, INPUT);

  lockServo.setPeriodHertz(50);
  lockServo.attach(SERVO_PIN, 500, 2400);
  setSafeStatus(true);

  setupMpu6050();
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
