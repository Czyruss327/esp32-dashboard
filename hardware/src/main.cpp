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
    Mode 3 blinks LED 3 (red, G12) + buzzer

  LONG PRESS (hold ~3 s):
    Enters MANUAL mode (modes 1-3 disabled). A confirmation double-beep plays.
    In manual mode every short press toggles ALL 3 LEDs + buzzer ON / OFF.
    Long press again to leave manual mode and return to Idle.

  TEMPERATURE ALERT (Idle and Manual modes):
    If the DHT22 reads TEMP_ALERT_C (40 C) or higher while in Idle or Manual,
    the LEDs light in rotation (LED 1 -> LED 2 -> LED 3 -> repeat) and the
    buzzer beeps on every step as an alarm. It stops when the
    temperature drops to TEMP_CLEAR_C (37 C) or below, then the LEDs/buzzer
    return to the prior state (off in Idle, or the selected state in Manual).

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
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include "secrets.h"

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
uint8_t oledAddress = 0;
unsigned long lastOled = 0;

// ---------- Wi-Fi + dashboard (EDIT THESE) ----------
// The ESP32 only works on 2.4 GHz Wi-Fi.
const String SERVER_BASE = SERVER_BASE_URL;
const String READING_URL = SERVER_BASE + "/api/device/reading";
const String COMMAND_URL = SERVER_BASE + "/api/device/command";
const bool USE_HTTPS = SERVER_BASE.startsWith("https://");
const unsigned long SEND_MS = 5000;  // send every 5 s, and instantly on a mode/alert change

// ---------- Timing ----------
const unsigned long DEBOUNCE_MS   = 40;
const unsigned long LONG_PRESS_MS = 3000;           // hold time for manual mode
const unsigned long BLINK_MS[3]   = {1000, 500, 250};
const unsigned long DHT_MS        = 2000;

// ---------- Temperature alert (Idle and Manual modes) ----------
const float TEMP_ALERT_C = 40.0;                    // alarm starts at/above this
const float TEMP_CLEAR_C = 37.0;                    // alarm stops at/below this
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

// Dashboard commands are received by the network task and applied in loop().
const unsigned long COMMAND_POLL_MS = 1000;
volatile bool commandPending = false;
volatile int pendingMode = IDLE;
volatile bool pendingOutputs = false;
volatile uint32_t pendingVersion = 0;
uint32_t appliedVersion = UINT32_MAX;

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
    digitalWrite(BUZZER, HIGH);
    delay(120);
    digitalWrite(BUZZER, LOW);
    delay(120);
  }
}

void startMode(Mode m) {
  mode = m;
  if (m != IDLE && m != MANUAL) tempAlert = false; // enabled only in Idle/Manual
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

      // Temperature alert is enabled in Idle and Manual, but not blink modes.
      if (mode == IDLE || mode == MANUAL) {
        if (!tempAlert && t >= TEMP_ALERT_C) {
          tempAlert = true;
          lastAlertBlink = millis();
          alertStep = 0;
          alertShowStep(alertStep);
          forceSend = true;
          Serial.println("ALERT: temperature too high!");
        } else if (tempAlert && t <= TEMP_CLEAR_C) {
          tempAlert = false;
          setOutputs(mode == MANUAL && manualOn); // restore Manual state, or stay off in Idle
          forceSend = true;
          Serial.println("Alert cleared");
        }
      }
    }
  }
}

void handleAlert() {
  if ((mode == IDLE || mode == MANUAL) && tempAlert) {
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
           "{\"temperature\":%.1f,\"humidity\":%.1f,\"mode\":\"%s\",\"alert\":%s,\"outputs_on\":%s}",
           lastT, lastH, modeName(), tempAlert ? "true" : "false", manualOn ? "true" : "false");
  outbox = buf;
  outboxReady = true;
}

// Background network task polls dashboard commands and posts sensor readings.
void netTask(void*) {
  unsigned long lastPoll = 0;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      outboxReady = false;
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      continue;
    }

    if (millis() - lastPoll >= COMMAND_POLL_MS) {
      lastPoll = millis();
      WiFiClient plain;
      WiFiClientSecure secure;
      HTTPClient http;
      bool ok = USE_HTTPS ? (secure.setInsecure(), http.begin(secure, COMMAND_URL))
                          : http.begin(plain, COMMAND_URL);
      if (ok) {
        http.setTimeout(2500);
        http.addHeader("Accept", "application/json");
        http.addHeader("X-API-KEY", DEVICE_API_KEY);
        int code = http.GET();
        if (code == HTTP_CODE_OK) {
          JsonDocument doc;
          DeserializationError err = deserializeJson(doc, http.getString());
          uint32_t version = doc["version"] | 0;
          const char* requested = doc["mode"] | "IDLE";
          int requestedMode = IDLE;
          if (!strcmp(requested, "MODE1")) requestedMode = BLINK1;
          else if (!strcmp(requested, "MODE2")) requestedMode = BLINK2;
          else if (!strcmp(requested, "MODE3")) requestedMode = BLINK3;
          else if (!strcmp(requested, "MANUAL")) requestedMode = MANUAL;
          if (!err && version != appliedVersion) {
            pendingMode = requestedMode;
            pendingOutputs = doc["outputs_on"] | false;
            pendingVersion = version;
            commandPending = true;
          }
        } else {
          Serial.printf("Dashboard GET -> %d\n", code);
        }
        http.end();
      }
    }

    if (outboxReady) {
      String body = outbox;
      outboxReady = false;
      WiFiClient plain;
      WiFiClientSecure secure;
      HTTPClient http;
      bool ok = USE_HTTPS ? (secure.setInsecure(), http.begin(secure, READING_URL))
                          : http.begin(plain, READING_URL);
      if (ok) {
        http.setTimeout(4000);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Accept", "application/json");
        http.addHeader("X-API-KEY", DEVICE_API_KEY);
        int code = http.POST(body);
        Serial.printf("Dashboard POST -> %d\n", code);
        http.end();
      }
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// Apply network commands in the main loop so output state has one owner.
void applyDashboardCommand() {
  if (!commandPending) return;
  int requested = pendingMode;
  bool requestedOutputs = pendingOutputs;
  uint32_t version = pendingVersion;
  commandPending = false;
  if (version == appliedVersion) return;
  appliedVersion = version;

  Mode next = (Mode)requested;
  bool enteringOrLeavingManual = (mode == MANUAL) != (next == MANUAL);
  if (enteringOrLeavingManual) confirmBeep();
  startMode(next);
  if (next == MANUAL) {
    manualOn = requestedOutputs;
    if (!tempAlert) setOutputs(manualOn);
  }
  Serial.printf("Applied dashboard command version %lu\n", (unsigned long)version);
}

// Shows the current mode, temperature and humidity on the OLED.
void handleOled() {
  if (!oledOk || millis() - lastOled < 500) return;
  lastOled = millis();
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print("Mode: ");
  if (mode == BLINK1) oled.println("Mode 1");
  else if (mode == BLINK2) oled.println("Mode 2");
  else if (mode == BLINK3) oled.println("Mode 3");
  else if (mode == MANUAL) oled.println("Manual");
  else oled.println("Idle");

  oled.setCursor(0, 16);
  oled.print("Temp: ");
  if (hasReading) { oled.print(lastT, 1); oled.println(" C"); }
  else oled.println("-- C");

  oled.setCursor(0, 32);
  oled.print("Humidity: ");
  if (hasReading) { oled.print(lastH, 1); oled.println(" %"); }
  else oled.println("-- %");

  oled.setCursor(0, 48);
  if (tempAlert) oled.print("HIGH TEMP ALERT");
  else if (!hasReading) oled.print("Waiting for DHT22");
  else oled.print("Sensor connected");
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

  // Scan the configured I2C pins before initializing the OLED. A successful
  // SSD1306 begin() only confirms buffer allocation, not that a display replied.
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  const uint8_t possibleAddresses[] = {0x3C, 0x3D};
  for (uint8_t address : possibleAddresses) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      oledAddress = address;
      break;
    }
  }

  if (oledAddress != 0) {
    Serial.printf("I2C display responded at 0x%02X\n", oledAddress);
    // Wire was already started on the custom ESP32 pins above; keep those pins.
    oledOk = oled.begin(SSD1306_SWITCHCAPVCC, oledAddress, true, false);
    if (oledOk) {
      oled.clearDisplay();
      oled.setTextColor(SSD1306_WHITE);
      oled.setTextSize(1);
      oled.setCursor(0, 0);
      oled.println("ESP32 starting");
      oled.println("OLED bus OK");
      oled.display();
    } else {
      Serial.println("OLED display buffer allocation failed");
    }
  } else {
    Serial.println("No I2C device at 0x3C or 0x3D on GPIO 14/27; check OLED power, SDA, SCL and pin order");
  }

  // Wi-Fi + background network task
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  xTaskCreatePinnedToCore(netTask, "net", 8192, NULL, 1, NULL, 0);

  // Power-on self-test: all three LEDs and buzzer flash/beep once.
  mode = MANUAL;
  setOutputs(true);
  delay(180);
  setOutputs(false);
  mode = IDLE;
  Serial.println("Self-test complete; mode: IDLE");
}

void loop() {
  applyDashboardCommand();
  handleButton();
  handleBlink();
  handleSensor();
  handleAlert();
  handleNetwork();
  handleOled();
}
