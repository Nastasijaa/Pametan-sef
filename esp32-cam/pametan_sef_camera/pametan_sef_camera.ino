/*
  Pametan sef - ESP32-CAM stream

  Ovo je poseban sketch za ESP32-CAM. Ne uploaduje se na isti ESP32 DevKit
  koji kontrolise sef, vec na odvojenu ESP32-CAM plocicu.

  Vazno:
  Ovaj sketch koristi fajlove iz Arduino primera:
  File -> Examples -> ESP32 -> Camera -> CameraWebServer

  Potrebni su i fajlovi:
  - camera_pins.h
  - app_httpd.cpp

  Najlaksi postupak:
  1. U Arduino IDE otvori File -> Examples -> ESP32 -> Camera -> CameraWebServer.
  2. Izaberi CAMERA_MODEL_AI_THINKER.
  3. Unesi isti Wi-Fi SSID i password kao dole.
  4. Uploaduj na ESP32-CAM.
*/

#include "esp_camera.h"
#include <WiFi.h>

#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

const char* ssid = "Nastasija";
const char* password = "27042004";

void startCameraServer();

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

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
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);

  if (err != ESP_OK) {
    Serial.printf("Greska pri pokretanju kamere: 0x%x", err);
    return;
  }

  WiFi.begin(ssid, password);
  Serial.print("Povezivanje na Wi-Fi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("Wi-Fi povezan!");
  Serial.print("IP adresa kamere: ");
  Serial.println(WiFi.localIP());

  startCameraServer();

  Serial.println("Kamera je spremna!");
  Serial.print("Otvori u browseru: http://");
  Serial.println(WiFi.localIP());
  Serial.print("Stream link: http://");
  Serial.print(WiFi.localIP());
  Serial.println(":81/stream");
}

void loop() {
  delay(10000);
}
