

#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

// -------------------- CONFIG --------------------
const char* ssid = "INFINITY";      // Replace with your WiFi SSID
const char* password = "11111111";  // Replace with your WiFi password

// Telegram Bot details
#define BOT_TOKEN "token"  // Replace with your bot token
#define CHAT_ID "chatid"

const char* ADMIN_PASSWORD = "admin123"; // change this

// Pins (match your wiring)
const int reedPin   = 4;   // reed switch (use INPUT_PULLUP: reed to GND when closed)
const int buzzerPin = 5;   // buzzer driver pin (via transistor recommended)
const int ledPin    = 2;   // status LED
const int buttonPin = 15;  // silence/ack button (INPUT_PULLUP: pressed = LOW)

// Buzzer polarity: set true if buzzer is active LOW (connect buzzer- to pin, + to Vcc)
const bool BUZZER_ACTIVE_LOW = true;

// -------------------- TIMING & THRESHOLDS --------------------
const unsigned long SILENCE_DURATION_MS  = 5UL * 60UL * 1000UL; // 5 minutes
const unsigned long WIFI_CHECK_INTERVAL  = 10000UL; // 10s
const unsigned long WIFI_TIMEOUT_MS      = 20000UL; // attempt time (ms)
const unsigned long MESSAGE_INTERVAL_MS  = 10000UL; // Telegram repeat interval while open
const unsigned long BEEP_ON_MS           = 800UL;   // beep ON duration
const unsigned long BEEP_OFF_MS          = 700UL;   // beep OFF duration (so cycle ~= 1500ms)
const unsigned long SAMPLING_WINDOW_MS   = 60UL;    // time spent sampling reed (ms)
const int           SAMPLE_COUNT         = 8;       // number of raw samples per window
const int           REQUIRED_CONFIRMS    = 3;       // require 3 consecutive confirmation cycles

const unsigned long BUTTON_DEBOUNCE_MS   = 50UL;

// -------------------- GLOBAL STATE --------------------
WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);
WebServer server(80);

bool systemArmed = false;     // start DISARMED for safety
bool doorOpen = false;        // logical confirmed door state
bool rawDoorOpen = false;     // latest sampled raw state
int  openConfirmCycles  = 0;  // how many consecutive cycles reported OPEN
int  closeConfirmCycles = 0;  // how many consecutive cycles reported CLOSED

// silence
bool silenceActive = false;
unsigned long silenceUntil = 0;

// buzzer
bool buzzerOn = false;
unsigned long lastBuzzerToggle = 0;

// telegram/wifi
unsigned long lastMessageTime = 0;
unsigned long lastWifiCheckTime = 0;

// button
int lastButtonReading = HIGH;
unsigned long lastButtonChangeMs = 0;
bool buttonHandled = false;

// -------------------- HELPERS --------------------
void setBuzzer(bool on) {
  if (BUZZER_ACTIVE_LOW) digitalWrite(buzzerPin, on ? LOW : HIGH);
  else                  digitalWrite(buzzerPin, on ? HIGH : LOW);
}

void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.print("WiFi not connected - attempting reconnect...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
    delay(250);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected: " + WiFi.localIP().toString());
    // optional TLS: client.setCACert(...);
    client.setInsecure(); // convenient for testing (insecure)
  } else {
    Serial.println("\nWiFi reconnect timed out.");
  }
}

bool sendTelegram(const String &txt) {
  if (WiFi.status() != WL_CONNECTED) return false;
  bool ok = bot.sendMessage(String(CHAT_ID), txt, "");
  return ok;
}

// read reed SAMPLE_COUNT times quickly and return number of HIGH reads (open)
int sampleReedReads() {
  int countHigh = 0;
  for (int i = 0; i < SAMPLE_COUNT; ++i) {
    if (digitalRead(reedPin) == HIGH) ++countHigh;
    delay(SAMPLING_WINDOW_MS / SAMPLE_COUNT); // spread samples across the window
  }
  return countHigh;
}

// handle button (silence) - to be called frequently in loop
void handleButton() {
  int r = digitalRead(buttonPin); // INPUT_PULLUP: LOW = pressed
  unsigned long now = millis();

  if (r != lastButtonReading) {
    lastButtonChangeMs = now;
    lastButtonReading = r;
  }

  if ((now - lastButtonChangeMs) > BUTTON_DEBOUNCE_MS) {
    if (r == LOW && !buttonHandled) {
      // button pressed - silence alarm
      buttonHandled = true;
      silenceActive = true;
      silenceUntil = now + SILENCE_DURATION_MS;
      setBuzzer(false);
      digitalWrite(ledPin, LOW);
      Serial.printf("Button pressed -> Silence ON for %lu ms\n", SILENCE_DURATION_MS);
      if (WiFi.status() == WL_CONNECTED) {
        sendTelegram("🔕 Alarm silenced (button) for " + String(SILENCE_DURATION_MS / 60000UL) + " minutes.");
      }
    }
    if (r == HIGH) {
      buttonHandled = false; // ready for next press
    }
  }
}

// -------------------- WEB HANDLERS --------------------
String pageRootHTML();

void handleRoot() {
  server.send(200, "text/html", pageRootHTML());
}

void handleStatus() {
  DynamicJsonDocument doc(512);
  doc["armed"] = systemArmed;
  doc["doorOpen"] = doorOpen;
  doc["rawDoorOpen"] = rawDoorOpen;
  doc["silenced"] = silenceActive;
  unsigned long now = millis();
  if (silenceActive && silenceUntil > now) {
    long msLeft = (long)(silenceUntil - now);
    doc["silenceMinutes"] = (msLeft + 59999) / 60000;
  } else doc["silenceMinutes"] = 0;
  doc["ip"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("N/A");
  doc["telegramOk"] = (WiFi.status() == WL_CONNECTED);
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

bool checkPassword() {
  if (!server.hasArg("password")) return false;
  return server.arg("password") == String(ADMIN_PASSWORD);
}

void handleArm() {
  if (!checkPassword()) {
    server.send(401, "text/plain", "Unauthorized: wrong password");
    return;
  }
  bool force = server.hasArg("force") && server.arg("force") == "1";
  if (!force && rawDoorOpen) {
    server.send(400, "text/plain", "Cannot arm: door appears OPEN. Use ?force=1 to override.");
    Serial.println("Arm blocked: door OPEN; use force to override.");
    return;
  }
  systemArmed = true;
  openConfirmCycles = 0;
  closeConfirmCycles = 0;
  silenceActive = false;
  setBuzzer(false);
  digitalWrite(ledPin, LOW);
  server.send(200, "text/plain", "System armed.");
  Serial.println("System armed via web.");
}

void handleDisarm() {
  if (!checkPassword()) {
    server.send(401, "text/plain", "Unauthorized: wrong password");
    return;
  }
  systemArmed = false;
  setBuzzer(false);
  digitalWrite(ledPin, LOW);
  server.send(200, "text/plain", "System disarmed.");
  Serial.println("System disarmed via web.");
}

void handleSilence() {
  if (!checkPassword()) {
    server.send(401, "text/plain", "Unauthorized: wrong password");
    return;
  }
  silenceActive = true;
  silenceUntil = millis() + SILENCE_DURATION_MS;
  setBuzzer(false);
  digitalWrite(ledPin, LOW);
  server.send(200, "text/plain", "Silenced for " + String(SILENCE_DURATION_MS / 60000UL) + " minutes.");
  Serial.println("Silenced via web.");
}

void handleNotFound() {
  server.send(404, "text/plain", "Not Found");
}

// -------------------- SETUP --------------------
void setup() {
  Serial.begin(115200);
  delay(10);
  Serial.println("\n=== ESP32 Alarm (corrected) ===");

  pinMode(reedPin, INPUT_PULLUP);
  pinMode(buzzerPin, OUTPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);

  setBuzzer(false); // ensure off
  digitalWrite(ledPin, LOW);

  // start WiFi attempt (loop will maintain)
  ensureWiFi();

  // web routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/arm", HTTP_POST, handleArm);
  server.on("/disarm", HTTP_POST, handleDisarm);
  server.on("/silence", HTTP_POST, handleSilence);
  server.onNotFound(handleNotFound);
  server.begin();

  // initial raw read and state
  rawDoorOpen = (digitalRead(reedPin) == HIGH);
  doorOpen = rawDoorOpen;
  Serial.printf("Initial state: door %s, system %s\n", doorOpen ? "OPEN" : "CLOSED", systemArmed ? "ARMED" : "DISARMED");
}

// -------------------- LOOP --------------------
void loop() {
  unsigned long now = millis();

  // web handling
  server.handleClient();

  // wifi maintenance
  if (now - lastWifiCheckTime >= WIFI_CHECK_INTERVAL) {
    lastWifiCheckTime = now;
    if (WiFi.status() != WL_CONNECTED) ensureWiFi();
  }

  // button (silence)
  handleButton();

  // silence expiry
  if (silenceActive && now >= silenceUntil) {
    silenceActive = false;
    setBuzzer(false);
    digitalWrite(ledPin, LOW);
    Serial.println("Silence expired; alerts restored.");
  }

  // sample reed (blocking short time to collect samples)
  int highCount = sampleReedReads(); // number of HIGH reads -> indicates OPEN if majority
  rawDoorOpen = (highCount > (SAMPLE_COUNT / 2));
  // debug
  Serial.printf("Sampled %d/%d high -> rawDoorOpen=%s\n", highCount, SAMPLE_COUNT, rawDoorOpen ? "OPEN" : "CLOSED");

  // confirmation cycles logic (requires REQUIRED_CONFIRMS consecutive cycles)
  if (rawDoorOpen) {
    openConfirmCycles++;
    closeConfirmCycles = 0;
    if (openConfirmCycles >= REQUIRED_CONFIRMS && !doorOpen) {
      doorOpen = true;
      Serial.println(">>> Door confirmed OPEN");
      // Force immediate alarm/beep attempt
      lastBuzzerToggle = 0; // so buzzer toggles immediately below
      lastMessageTime = 0;  // allow immediate send
    }
    // cap counters to avoid overflow
    if (openConfirmCycles > 1000) openConfirmCycles = REQUIRED_CONFIRMS;
  } else {
    closeConfirmCycles++;
    openConfirmCycles = 0;
    if (closeConfirmCycles >= REQUIRED_CONFIRMS && doorOpen) {
      doorOpen = false;
      Serial.println("<<< Door confirmed CLOSED");
      // reset indicators
      setBuzzer(false);
      digitalWrite(ledPin, LOW);
    }
    if (closeConfirmCycles > 1000) closeConfirmCycles = REQUIRED_CONFIRMS;
  }

  // alarm/action logic
  if (systemArmed && doorOpen) {
    if (silenceActive) {
      // blink LED while silenced
      if ((now - lastBuzzerToggle) >= 250) {
        digitalWrite(ledPin, !digitalRead(ledPin));
        lastBuzzerToggle = now;
      }
      // ensure buzzer is off
      setBuzzer(false);
    } else {
      // normal alarm: steady LED and pulsing buzzer
      digitalWrite(ledPin, HIGH);

      // buzzer pulse using lastBuzzerToggle as toggle timer
      unsigned long period = buzzerOn ? BEEP_ON_MS : BEEP_OFF_MS;
      if (now - lastBuzzerToggle >= period) {
        buzzerOn = !buzzerOn;
        setBuzzer(buzzerOn);
        lastBuzzerToggle = now;
        Serial.printf("Buzzer %s\n", buzzerOn ? "ON" : "OFF");
      }

      // Telegram alerts rate-limited
      if (WiFi.status() == WL_CONNECTED && (now - lastMessageTime >= MESSAGE_INTERVAL_MS)) {
        String msg = "🚨 ALERT: Door OPEN while armed!";
        if (sendTelegram(msg)) {
          Serial.println("Telegram alert sent.");
          lastMessageTime = now;
        } else {
          Serial.println("Telegram send failed.");
        }
      }
    }
  } else {
    // not armed or door closed -> ensure off
    setBuzzer(false);
    if (!silenceActive) digitalWrite(ledPin, LOW);
  }

  // a short non-blocking pause
  delay(20);
}

// -------------------- WEB PAGE --------------------
String pageRootHTML() {
  String s = R"rawliteral(
    <!doctype html>
    <html>
    <head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
    <title>ESP32 Alarm Control</title>
    <style>
      body{font-family:Arial;margin:12px;background:#f4f6f8}
      .card{max-width:520px;margin:auto;background:#fff;padding:16px;border-radius:8px;box-shadow:0 2px 8px rgba(0,0,0,0.08)}
      button{padding:10px 14px;margin:6px;border-radius:6px;border:none;cursor:pointer}
      .arm{background:#2ecc71;color:#fff}.disarm{background:#e74c3c;color:#fff}.silence{background:#f39c12;color:#fff}
      input[type=password]{width:100%;padding:8px;margin-top:6px;box-sizing:border-box}
    </style>
    </head>
    <body>
      <div class="card">
        <h2>ESP32 Alarm Control</h2>
        <div>
          <label>Password:</label>
          <input id="pw" type="password" placeholder="admin password">
        </div>
        <div style="margin-top:10px">
          <button class="arm" onclick="postAction('/arm')">Start (Arm)</button>
          <button class="disarm" onclick="postAction('/disarm')">Stop (Disarm)</button>
          <button class="silence" onclick="postAction('/silence')">Silence 5 min</button>
        </div>
        <div id="statusBox" style="margin-top:12px">Loading status...</div>
      </div>

      <script>
        async function getStatus(){
          try {
            const r = await fetch('/status');
            const j = await r.json();
            const sb = document.getElementById('statusBox');
            sb.innerHTML = `
              <b>System:</b> ${j.armed ? '<span style="color:green">ARMED</span>' : '<span style="color:gray">DISARMED</span>'}<br>
              <b>Door:</b> ${j.doorOpen ? '<span style="color:red">OPEN</span>' : '<span style="color:green">CLOSED</span>'}<br>
              <b>Silenced:</b> ${j.silenced ? '<span style="color:orange">YES ('+j.silenceMinutes+' min left)</span>' : 'NO'}<br>
              <b>IP:</b> ${j.ip}<br>
              <b>Telegram:</b> ${j.telegramOk ? 'OK' : 'NOT CONNECTED'}
            `;
          } catch(e) {
            document.getElementById('statusBox').innerText = 'Error fetching status';
          }
        }

        async function postAction(path){
          const pw = document.getElementById('pw').value || '';
          const body = new URLSearchParams();
          body.append('password', pw);
          try {
            const r = await fetch(path, {method:'POST', body: body});
            const txt = await r.text();
            alert(txt);
            getStatus();
          } catch(e) {
            alert('Action failed: ' + e);
          }
        }

        getStatus();
        setInterval(getStatus, 2000);
      </script>
    </body>
    </html>
  )rawliteral";
  return s;
}
