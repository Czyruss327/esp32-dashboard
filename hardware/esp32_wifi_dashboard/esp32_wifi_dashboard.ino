/*
  Battery-powered Mode Blinker + Temp/Humidity Monitor
  Board:   ESP32-WROOM DevKit (3.3 V logic)
  Pins:    Buzzer=G32, Button=G33, LED1=G25, LED2=G26, LED3=G12,
           DHT22 data=G13, OLED SDA=G14, OLED SCL=G27   (all on the LEFT pin column)
  Library: "DHT sensor library" by Adafruit (+ "Adafruit Unified Sensor")

  Slide switch  -> in the battery + line (hardware only)
  Push button   -> G33 to GND (uses internal pull-up)

  SHORT PRESS (normal):
    Idle -> Mode 1 (1 s) -> Mode 2 (0.5 s) -> Mode 3 (0.25 s) -> Idle ...
    Mode 1 blinks LED 1 (green, G25) + buzzer
    Mode 2 blinks LED 2 (yellow, G26) + buzzer
    Mode 3 blinks LED 3 (red, G27) + buzzer

  LONG PRESS (hold ~3 s):
    Enters MANUAL mode (modes 1-3 disabled). A confirmation double-beep plays.
    In manual mode every short press toggles ALL 3 LEDs + buzzer ON / OFF.
    Long press again to leave manual mode and return to Idle.

  TEMPERATURE ALERT (manual mode only):
    If the DHT22 reads TEMP_ALERT_C (50 C) or higher while in manual mode,
    the LEDs light in rotation (LED 1 -> LED 2 -> LED 3 -> repeat) and the
    buzzer beeps on every step as an alarm. It stops when the
    temperature drops to TEMP_CLEAR_C (48 C) or below, then the LEDs/buzzer
    return to whatever state you had set with the button. Leaving manual
    mode (long press) also stops the alarm.

  Temperature/humidity is printed to the Serial Monitor every 2 s.

  DASHBOARD: readings, the current mode and the alert state are also sent over
  Wi-Fi to the Laravel dashboard (every 5 s, and instantly when the mode or alert
  changes). Fill in WIFI_SSID, WIFI_PASS, SERVER_URL and API_KEY below.
*/

#include <DHT.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>   // libraries: "Adafruit SSD1306" + "Adafruit GFX Library"

// ---------- Pins ----------
#define DHTPIN      13
#define DHTTYPE     DHT22      // or DHT11
#define LED_GREEN   25
#define LED_YELLOW  26
#define LED_RED     12     // LED 3. G12 must be LOW at boot: LED + resistor to GND is fine
#define BUZZER      32
#define BUTTON      33
#define I2C_SDA     14     // OLED SDA
#define I2C_SCL     27     // OLED SCL

// ---------- OLED 0.96" 128x64 (I2C, address 0x3C) ----------
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
bool oledOk = false;
unsigned long lastOled = 0;

// ---------- Wi-Fi + dashboard (EDIT THESE) ----------
// The ESP32 only works on 2.4 GHz Wi-Fi.
const char* WIFI_SSID  = "YOUR_WIFI_NAME";
const char* WIFI_PASS  = "YOUR_WIFI_PASSWORD";
const char* SERVER_URL = "http://192.168.1.50:8000/api/device/reading";  // your Laravel server
const char* API_KEY    = "PUT_THE_SAME_DEVICE_API_KEY_AS_IN_.env";
const bool  USE_HTTPS  = false;      // true if SERVER_URL starts with https://
const unsigned long SEND_MS = 5000;  // send every 5 s, and instantly on a mode/alert change

// ---------- Timing ----------
const unsigned long DEBOUNCE_MS   = 40;
const unsigned long LONG_PRESS_MS = 3000;           // hold time for manual mode
const unsigned long BLINK_MS[3]   = {1000, 500, 250};
const unsigned long DHT_MS        = 2000;

// ---------- Temperature alert (manual mode) ----------
const float TEMP_ALERT_C = 50.0;                    // alarm starts at/above this
const float TEMP_CLEAR_C = 48.0;                    // alarm stops at/below this
const unsigned long ALERT_STEP_MS = 200;            // time each LED stays lit
const unsigned long ALERT_BEEP_MS = 100;            // beep length at each step

enum Mode { IDLE, BLINK1, BLINK2, BLINK3, MANUAL };
Mode mode = IDLE;

DHT dht(DHTPIN, DHTTYPE);

// Output state
bool outputsOn = false;
unsigned long lastBlink = 0;
unsigned long lastDht = 0;

// Manual mode / alert state
bool manualOn = false;            // what the button last set in manual mode
bool tempAlert = false;           // true while the over-temperature alarm is active
unsigned long lastAlertBlink = 0;
int alertStep = 0;                // 0 = LED 1, 1 = LED 2, 2 = LED 3

// Hand-off from the main loop to the network task
float lastT = NAN, lastH = NAN;
bool  hasReading = false;
bool  forceSend = false;
unsigned long lastSend = 0;
String outbox = "";
volatile bool outboxReady = false;

// Button state
bool lastReading = HIGH;          // HIGH = released (pull-up)
bool stableState = HIGH;
unsigned long lastChange = 0;
unsigned long pressStart = 0;
bool longHandled = false;

// Which LED(s) are used depends on the current mode:
//   Mode 1 -> LED 1 (green)   Mode 2 -> LED 2 (yellow)
//   Mode 3 -> LED 3 (red)     Manual -> all LEDs
void setOutputs(bool on) {
  outputsOn = on;
  digitalWrite(LED_GREEN,  on && (mode == BLINK1 || mode == MANUAL));
  digitalWrite(LED_YELLOW, on && (mode == BLINK2 || mode == MANUAL));
  digitalWrite(LED_RED,    on && (mode == BLINK3 || mode == MANUAL));
  digitalWrite(BUZZER,     on);
}

// Alarm pattern: light one LED at a time (1 -> 2 -> 3) with a beep
void alertShowStep(int step) {
  digitalWrite(LED_GREEN,  step == 0);
  digitalWrite(LED_YELLOW, step == 1);
  digitalWrite(LED_RED,    step == 2);
  digitalWrite(BUZZER, HIGH);
}

void confirmBeep() {
  for (int i = 0; i < 2; i++) {
    setOutputs(true);  delay(120);
    setOutputs(false); delay(120);
  }
}

void startMode(Mode m) {
  mode = m;
  tempAlert = false;               // alarm only exists in manual mode
  manualOn = false;
  forceSend = true;                // tell the dashboard about the new mode right away
  lastBlink = millis();
  if (m >= BLINK1 && m <= BLINK3) {
    setOutputs(true);              // start with ON right away
  } else {
    setOutputs(false);
  }
  Serial.print("Mode: ");
  Serial.println(m == IDLE ? "IDLE" : m == MANUAL ? "MANUAL" : String(m));
}

void onShortPress() {
  if (mode == MANUAL) {
    manualOn = !manualOn;          // toggle on/off
    if (!tempAlert) setOutputs(manualOn);   // during an alarm, the alarm owns the outputs
  } else if (mode == BLINK3) {
    startMode(IDLE);
  } else {
    startMode((Mode)(mode + 1));   // IDLE->1->2->3
  }
}

void onLongPress() {
  if (mode == MANUAL) {
    confirmBeep();
    startMode(IDLE);
  } else {
    mode = MANUAL;                 // so all 3 LEDs flash with the beep
    confirmBeep();
    startMode(MANUAL);
  }
}

void handleButton() {
  bool reading = digitalRead(BUTTON);
  unsigned long now = millis();

  if (reading != lastReading) {
    lastChange = now;
    lastReading = reading;
  }

  if ((now - lastChange) > DEBOUNCE_MS && reading != stableState) {
    stableState = reading;
    if (stableState == LOW) {              // pressed
      pressStart = now;
      longHandled = false;
    } else {                               // released
      if (!longHandled) onShortPress();
    }
  }

  // Held long enough -> long press (fires while still held)
  if (stableState == LOW && !longHandled && (now - pressStart) >= LONG_PRESS_MS) {
    longHandled = true;
    onLongPress();
  }
}

void handleBlink() {
  if (mode >= BLINK1 && mode <= BLINK3) {
    unsigned long interval = BLINK_MS[mode - BLINK1];
    if (millis() - lastBlink >= interval) {
      lastBlink = millis();
      setOutputs(!outputsOn);
    }
  }
}

void handleSensor() {
  if (millis() - lastDht >= DHT_MS) {
    lastDht = millis();
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (isnan(h) || isnan(t)) {
      Serial.println("Sensor read failed - check wiring");
    } else {
      Serial.print("Temp: ");  Serial.print(t);
      Serial.print(" C  Humidity: "); Serial.print(h);
      Serial.println(" %");
      lastT = t; lastH = h; hasReading = true;

      // Over-temperature alarm (manual mode only)
      if (mode == MANUAL) {
        if (!tempAlert && t >= TEMP_ALERT_C) {
          tempAlert = true;
          lastAlertBlink = millis();
          alertStep = 0;
          alertShowStep(alertStep);
          forceSend = true;
          Serial.println("ALERT: temperature too high!");
        } else if (tempAlert && t <= TEMP_CLEAR_C) {
          tempAlert = false;
          setOutputs(manualOn);    // go back to what the button set
          forceSend = true;
          Serial.println("Alert cleared");
        }
      }
    }
  }
}

void handleAlert() {
  if (mode == MANUAL && tempAlert) {
    unsigned long elapsed = millis() - lastAlertBlink;
    if (elapsed >= ALERT_STEP_MS) {
      lastAlertBlink = millis();
      alertStep = (alertStep + 1) % 3;   // next LED in the rotation
      alertShowStep(alertStep);
    } else if (elapsed >= ALERT_BEEP_MS) {
      digitalWrite(BUZZER, LOW);         // short beep, then silence until next step
    }
  }
}

const char* modeName() {
  switch (mode) {
    case BLINK1: return "MODE1";
    case BLINK2: return "MODE2";
    case BLINK3: return "MODE3";
    case MANUAL: return "MANUAL";
    default:     return "IDLE";
  }
}

// Main loop: prepare the JSON message (fast, never blocks)
void handleNetwork() {
  if (!hasReading || outboxReady) return;
  if (!forceSend && millis() - lastSend < SEND_MS) return;
  lastSend = millis();
  forceSend = false;
  char buf[128];
  snprintf(buf, sizeof(buf),
           "{\"temperature\":%.1f,\"humidity\":%.1f,\"mode\":\"%s\",\"alert\":%s}",
           lastT, lastH, modeName(), tempAlert ? "true" : "false");
  outbox = buf;
  outboxReady = true;
}

// Background task: does the slow HTTP request so LEDs/buzzer/button never lag
void netTask(void*) {
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      outboxReady = false;                       // drop data while offline
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      continue;
    }
    if (outboxReady) {
      String body = outbox;
      outboxReady = false;

      WiFiClient plain;
      WiFiClientSecure secure;
      HTTPClient http;
      bool ok;
      if (USE_HTTPS) { secure.setInsecure(); ok = http.begin(secure, SERVER_URL); }
      else           { ok = http.begin(plain, SERVER_URL); }

      if (ok) {
        http.setTimeout(4000);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Accept", "application/json");
        http.addHeader("X-API-KEY", API_KEY);
        int code = http.POST(body);
        Serial.printf("Dashboard POST -> %d\n", code);
        http.end();
      }
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// Shows mode, temperature, humidity and alert status on the OLED (refreshes twice a second)
void handleOled() {
  if (!oledOk || millis() - lastOled < 500) return;
  lastOled = millis();
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print(modeName());
  oled.setCursor(80, 0);
  oled.print(WiFi.status() == WL_CONNECTED ? "WiFi" : "no WiFi");

  oled.setTextSize(2);
  oled.setCursor(0, 14);
  oled.print("T ");
  if (hasReading) oled.print(lastT, 1); else oled.print("--");
  oled.print(" C");
  oled.setCursor(0, 34);
  oled.print("H ");
  if (hasReading) oled.print(lastH, 1); else oled.print("--");
  oled.print(" %");

  oled.setTextSize(1);
  if (tempAlert) {
    oled.fillRect(0, 54, 128, 10, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
    oled.setCursor(10, 55);
    oled.print("!! HIGH TEMP ALERT !!");
  } else {
    oled.setCursor(0, 56);
    oled.print("Status: normal");
  }
  oled.display();
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_GREEN,  OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_RED,    OUTPUT);
  pinMode(BUZZER,     OUTPUT);
  pinMode(BUTTON, INPUT_PULLUP);
  dht.begin();

  // OLED on custom I2C pins
  Wire.begin(I2C_SDA, I2C_SCL);
  oledOk = oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (oledOk) {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.print("Starting...");
    oled.display();
  } else {
    Serial.println("OLED not found - check SDA/SCL/VCC/GND");
  }

  // Wi-Fi + background network task
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  xTaskCreatePinnedToCore(netTask, "net", 8192, NULL, 1, NULL, 0);

  mode = MANUAL;                  // so all LEDs light during self test
  setOutputs(true); delay(200);   // power-on self test
  setOutputs(false);
  mode = IDLE;
}

void loop() {
  handleButton();
  handleBlink();
  handleSensor();
  handleAlert();
  handleNetwork();
  handleOled();
}
