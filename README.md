# Pametan sef / Smart Anti-Theft Safe

Projekat sadrzi web dashboard i ESP32 firmware za pametan sef koji preko Firebase Realtime Database razmenjuje status brave, stanje alarma, ocitavanja senzora i komande za zakljucavanje/otkljucavanje.

## Struktura projekta

```text
web-app/
  index.html
  style.css
  script.js
esp32/
  pametan_sef/
    pametan_sef.ino
esp32-cam/
  pametan_sef_camera/
    pametan_sef_camera.ino
platformio.ini
README.md
```

## Komponente

- ESP32 DevKit
- Servo SG90
- Buzzer
- Crvena LED
- Zelena LED
- MPU6050 akcelerometar/ziroskop
- Digitalni senzor vibracije/udarca
- Firebase Realtime Database
- Opcionalno: ESP32-CAM za live video stream

## Pinovi povezivanja

| Komponenta | ESP32 pin |
| --- | --- |
| Servo SG90 signal | GPIO13 |
| Buzzer signal | GPIO14 |
| Crvena LED | GPIO4 |
| Zelena LED | GPIO23 |
| MPU6050 SDA | GPIO18 |
| MPU6050 SCL | GPIO19 |
| Senzor vibracije DO | GPIO15 |

Napomena: LED diode povezati preko odgovarajucih otpornika. Servo je preporucljivo napajati stabilnim spoljnim 5V napajanjem uz zajednicku masu sa ESP32.

## Firebase struktura

```text
safe/status = "locked" ili "unlocked"
safe/alarm = true/false
safe/movement = broj
safe/tilt = broj
safe/vibration = true/false
safe/lastEvent = tekst
safe/cameraUrl = tekst
commands/lock = true/false
commands/unlock = true/false
events/{id}:
  type
  message
  timestamp
```

## Podesavanje Firebase-a

1. Napravi Firebase projekat.
2. Ukljuci Realtime Database.
3. U Realtime Database rules za testiranje mozes privremeno dozvoliti citanje/pisanje:

```json
{
  "rules": {
    ".read": true,
    ".write": true
  }
}
```

Za produkciju obavezno koristi Firebase Authentication i ogranicena pravila pristupa.

4. U `web-app/script.js` zameni placeholder vrednosti u `firebaseConfig` objektu:

```js
const firebaseConfig = {
  apiKey: "...",
  authDomain: "...",
  databaseURL: "...",
  projectId: "...",
  storageBucket: "...",
  messagingSenderId: "...",
  appId: "..."
};
```

5. U `esp32/pametan_sef/pametan_sef.ino` zameni:

```cpp
#define WIFI_SSID "WIFI_SSID"
#define WIFI_PASSWORD "WIFI_PASSWORD"
#define DATABASE_URL "FIREBASE_HOST_OR_DATABASE_URL"
#define FIREBASE_API_KEY "FIREBASE_AUTH_OR_API_KEY"
```

Ako koristis drugu Firebase biblioteku ili legacy database secret, prilagodi auth deo prema dokumentaciji biblioteke.

## Pokretanje web aplikacije

Web aplikacija nema backend server. Otvori `web-app/index.html` direktno u browseru ili pokreni jednostavan lokalni static server iz foldera projekta:

```bash
cd web-app
python -m http.server 8000
```

Zatim otvori `http://localhost:8000`.

Dashboard u realnom vremenu cita `safe` i `events` iz Firebase-a. Dugmad upisuju komande:

- `Zakljucaj`: `commands/lock = true`, `commands/unlock = false`
- `Otkljucaj`: `commands/unlock = true`, `commands/lock = false`
- `Reset alarm`: `safe/alarm = false`

## Upload koda iz VS Code / PlatformIO

1. Instaliraj VS Code ekstenziju `PlatformIO IDE`.
2. Otvori ovaj folder projekta u VS Code-u.
3. Za glavni ESP32 sef izaberi environment `smart_safe`.
4. Klikni `Upload`, ili u terminalu pokreni:

```bash
pio run -e smart_safe -t upload
```

5. Za ESP32-CAM izaberi environment `esp32_camera`, ili pokreni:

```bash
pio run -e esp32_camera -t upload
```

6. Serial Monitor mozes otvoriti iz PlatformIO taba ili komandom:

```bash
pio device monitor -b 115200
```

Napomena: ESP32-CAM sketch ocekuje da se u `esp32-cam/pametan_sef_camera/` nalaze i fajlovi iz Arduino primera `CameraWebServer`, posebno `camera_pins.h` i `app_httpd.cpp`.

## Upload koda na ESP32 iz Arduino IDE

1. Instaliraj Arduino IDE ili PlatformIO.
2. U Arduino IDE dodaj ESP32 board paket.
3. Instaliraj biblioteke:
   - `Firebase ESP Client` by Mobizt
   - `ESP32Servo`
   - `Adafruit MPU6050`
   - `Adafruit Unified Sensor`
4. Otvori `esp32/pametan_sef/pametan_sef.ino`.
5. Unesi Wi-Fi i Firebase podatke.
6. Izaberi odgovarajuci ESP32 DevKit board i COM port.
7. Klikni Upload.

## Alarm logika

ESP32 cita MPU6050 i digitalni senzor vibracije. Iz MPU6050 se racuna:

- intenzitet pomeranja kao odstupanje ukupnog ubrzanja od gravitacije
- nagib kao veca vrednost izmedju pitch i roll uglova

Ako je sef zakljucan i detektuje se vibracija, veliko pomeranje ili nagib preko praga:

- `safe/alarm` postaje `true`
- `safe/status` ostaje `locked`
- servo ide u zakljucan polozaj
- pali se crvena LED
- gasi se zelena LED
- aktivira se buzzer
- dogadjaj se upisuje u `events`

Kada senzori vise ne prelaze prag, ESP32 vraca `safe/alarm` na `false`. Pragovi su u kodu kao `MOVEMENT_THRESHOLD` i `TILT_THRESHOLD_DEG` i treba ih kalibrisati na stvarnom hardveru.

## Security level i brojac pokusaja

Web aplikacija izracunava `Security level`:

- `LOW`: normalno pomeranje i mali nagib
- `MEDIUM`: srednje pomeranje ili nagib
- `HIGH`: velika vrednost pomeranja, veliki nagib ili aktivna vibracija

Brojac pokusaja obijanja se racuna iz `events` istorije na osnovu alarm dogadjaja.

## ESP32-CAM live video

Polje `Camera stream URL` u dashboardu upisuje adresu u `safe/cameraUrl`. Kada kasnije dodas ESP32-CAM, pokreni camera web server primer na toj plocici i u dashboard unesi stream adresu, na primer:

```text
http://192.168.1.50:81/stream
```

Klik na `Otvori live kameru` otvara stream u novom tabu. ESP32-CAM moze biti zaseban uredjaj na istoj Wi-Fi mrezi; ne treba backend server.
