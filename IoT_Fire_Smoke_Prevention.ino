#define BLYNK_TEMPLATE_ID "TMPL3uWmoGOQM"
#define BLYNK_TEMPLATE_NAME "IOT FIRE AND SMOKE PREVENTION"
#define BLYNK_AUTH_TOKEN "YOUR_BLYNK_AUTH_TOKEN"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <DHT.h>
#include "driver/gpio.h"

const char WIFI_SSID[] = "YOUR_WIFI_SSID";
const char WIFI_PASS[] = "YOUR_WIFI_PASSWORD";

const uint8_t PIN_MQ2       = 34;
const uint8_t PIN_FLAME     = 27;
const uint8_t PIN_DHT       = 26;
const uint8_t PIN_LED_RED   = 14;
const uint8_t PIN_LED_GREEN = 18;
const uint8_t PIN_BUZZER    = 13;
const uint8_t PIN_RELAY     = 25;

#define FLAME_ACTIVE_LEVEL  LOW
#define RELAY_ACTIVE_LOW    1
#define BUZZER_ACTIVE_HIGH  0
#define LED_ACTIVE_HIGH     0
#define DHT_TYPE            DHT22
#define EXTRA_RULES         0
#define AI_TRIGGER          0
#define ALLOW_MANUAL_PUMP   0

const int AI_ALERT_RISK = 75;
#if BUZZER_ACTIVE_HIGH
  #define BUZZER_ON_LEVEL  HIGH
  #define BUZZER_OFF_LEVEL LOW
#else
  #define BUZZER_ON_LEVEL  LOW
  #define BUZZER_OFF_LEVEL HIGH
#endif
#if LED_ACTIVE_HIGH
  #define LED_ON_LEVEL  HIGH
  #define LED_OFF_LEVEL LOW
#else
  #define LED_ON_LEVEL  LOW
  #define LED_OFF_LEVEL HIGH
#endif

const int   SMOKE_RISE_THRESHOLD = 600;
const int   SMOKE_HYST           = 150;
const float TEMP_LIMIT_C         = 50.0;
const float TEMP_HYST_C          = 3.0;
const unsigned long MQ2_WARMUP_MS    = 60000;
const unsigned long BASE_CAL_MS      = 10000;
const unsigned long SMOKE_CONFIRM_MS = 3000;
const unsigned long TEMP_CONFIRM_MS  = 5000;
const unsigned long FLAME_IGNORE_MS  = 5000;
const unsigned long SAMPLE_MS          = 50;
const unsigned long DHT_MS             = 2500;
const unsigned long FLAME_CLEAR_MS     = 5000;
const unsigned long RECOVERY_MS        = 5000;
const unsigned long MANUAL_PUMP_MAX_MS = 300000;
const unsigned long NOTIFY_COOLDOWN_MS = 30000;
const unsigned long BLYNK_SEND_MS      = 2000;
const unsigned long PRINT_MS           = 2000;
const unsigned long WIFI_RETRY_MS      = 15000;
const unsigned long BLYNK_RETRY_MS     = 15000;
const unsigned long RATE_WINDOW_MS     = 30000;
const unsigned long RELAY_REFRESH_MS   = 200;
const uint8_t FLAME_CONFIRM_SAMPLES = 3;
const uint8_t DHT_FAIL_LIMIT = 5;

DHT dht(PIN_DHT, DHT_TYPE);

bool fireActive = false;
const char* reason = "";
int smokeRaw = 0, smoke = 0;
float smokeBase = 0;
long baseSum = 0;
int baseCount = 0;
bool baselineReady = false;
float temperature = NAN, humidity = NAN;
uint8_t dhtFails = 0;
bool dhtFault = false;
uint8_t flameCount = 0;
bool flameConfirmed = false;
unsigned long lastFlameSeen = 0;
bool smokeHigh = false, tempHigh = false;
unsigned long smokeHighSince = 0, tempHighSince = 0;
float tempRate = 0, tempRef = NAN;
unsigned long tempRefTime = 0;
float aiRiskF = 0;
int aiRisk = 0;
bool safeRunning = false;
unsigned long safeSince = 0;
bool pumpOn = false, manualPump = false;
unsigned long manualPumpStart = 0;
int relayActiveLow = RELAY_ACTIVE_LOW;
bool notifyPending = false, notifySentOnce = false;
unsigned long lastNotify = 0;
char notifyMsg[96];
bool statusDirty = true, v5Dirty = false, v5Resync = false;
unsigned long v5ResyncAt = 0, ignoreV5Until = 0;
bool wasWifi = false;
unsigned long bootMs = 0, lastSample = 0, lastDht = 0, lastBlynk = 0, lastPrint = 0;
unsigned long lastWifiTry = 0, lastBlynkTry = 0, lastRelayRefresh = 0;

void setPump(bool on) {
  pumpOn = on;
  uint8_t onLvl = relayActiveLow ? LOW : HIGH;
  uint8_t offLvl = relayActiveLow ? HIGH : LOW;
  uint8_t lvl = on ? onLvl : offLvl;
  digitalWrite(PIN_RELAY, lvl);
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, lvl);
}

void refreshRelay(unsigned long now) {
  if (now - lastRelayRefresh < RELAY_REFRESH_MS) return;
  lastRelayRefresh = now;
  uint8_t onLvl = relayActiveLow ? LOW : HIGH;
  uint8_t offLvl = relayActiveLow ? HIGH : LOW;
  digitalWrite(PIN_RELAY, pumpOn ? onLvl : offLvl);
}

void setBuzzer(bool on)   { digitalWrite(PIN_BUZZER, on ? BUZZER_ON_LEVEL : BUZZER_OFF_LEVEL); }
void setRedLed(bool on)   { digitalWrite(PIN_LED_RED, on ? LED_ON_LEVEL : LED_OFF_LEVEL); }
void setGreenLed(bool on) { digitalWrite(PIN_LED_GREEN, on ? LED_ON_LEVEL : LED_OFF_LEVEL); }

void handleSerialCmd() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '1') {
#if ALLOW_MANUAL_PUMP
      manualPump = true;
      manualPumpStart = millis();
      Serial.println("TEST: pump ON");
#else
      Serial.println("TEST: manual pump disabled (ALLOW_MANUAL_PUMP=0)");
#endif
    } else if (c == '0') {
      manualPump = false;
      Serial.println("TEST: pump OFF");
    } else if (c == 'l' || c == 'L') {
      relayActiveLow = !relayActiveLow;
      setPump(pumpOn);
      Serial.printf("TEST: relayActiveLow=%d\n", relayActiveLow);
    } else if (c == 's' || c == 'S') {
      Serial.printf("SETTINGS: relayActiveLow=%d pumpOn=%d relayPinLevel=%d\n",
                    relayActiveLow, pumpOn ? 1 : 0, digitalRead(PIN_RELAY));
    }
  }
}

void sampleInputs(unsigned long now) {
  bool raw = (digitalRead(PIN_FLAME) == FLAME_ACTIVE_LEVEL);
  if (now - bootMs < FLAME_IGNORE_MS) raw = false;
  if (raw) {
    if (flameCount < 250) flameCount++;
    lastFlameSeen = now;
  } else if (flameCount > 0) flameCount--;
  if (!flameConfirmed && flameCount >= FLAME_CONFIRM_SAMPLES) {
    flameConfirmed = true;
    statusDirty = true;
    Serial.println("[FLAME] Flame CONFIRMED");
  } else if (flameConfirmed && (now - lastFlameSeen >= FLAME_CLEAR_MS)) {
    flameConfirmed = false;
    flameCount = 0;
    statusDirty = true;
    Serial.println("[FLAME] Flame cleared");
  }

  long sum = 0;
  for (int i = 0; i < 8; i++) sum += analogRead(PIN_MQ2);
  smokeRaw = (int)(sum / 8);
  if (now - bootMs < MQ2_WARMUP_MS) {
    smoke = 0;
  } else if (!baselineReady) {
    baseSum += smokeRaw;
    baseCount++;
    smoke = 0;
    if (now - bootMs >= MQ2_WARMUP_MS + BASE_CAL_MS && baseCount > 0) {
      smokeBase = (float)baseSum / (float)baseCount;
      baselineReady = true;
      Serial.printf("[MQ2] Clean-air baseline = %d\n", (int)smokeBase);
    }
  } else {
    if (smokeRaw < smokeBase) smokeBase += (smokeRaw - smokeBase) * 0.01f;
    int rel = smokeRaw - (int)smokeBase;
    smoke = rel < 0 ? 0 : rel;
    if (!smokeHigh && smoke >= SMOKE_RISE_THRESHOLD) {
      smokeHigh = true;
      smokeHighSince = now;
    } else if (smokeHigh && smoke < (SMOKE_RISE_THRESHOLD - SMOKE_HYST)) {
      smokeHigh = false;
    }
  }
}

void readDht(unsigned long now) {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (isnan(t) || isnan(h)) {
    if (dhtFails < 250) dhtFails++;
    Serial.printf("[DHT] Read failed (%u/%u)\n", dhtFails, DHT_FAIL_LIMIT);
    if (dhtFails >= DHT_FAIL_LIMIT && !dhtFault) {
      dhtFault = true;
      tempHigh = false;
      tempRate = 0;
      tempRef = NAN;
      Serial.println("[DHT] SENSOR FAULT: check wiring (GPIO26, power, pull-up)");
    }
    return;
  }
  dhtFails = 0;
  if (dhtFault) { dhtFault = false; Serial.println("[DHT] Sensor recovered"); }
  temperature = t;
  humidity = h;
  if (!tempHigh && t > TEMP_LIMIT_C) { tempHigh = true; tempHighSince = now; }
  else if (tempHigh && t < (TEMP_LIMIT_C - TEMP_HYST_C)) tempHigh = false;
  if (isnan(tempRef)) {
    tempRef = t;
    tempRefTime = now;
  } else if (now - tempRefTime >= RATE_WINDOW_MS) {
    float mins = (now - tempRefTime) / 60000.0f;
    tempRate = (t - tempRef) / mins;
    tempRef = t;
    tempRefTime = now;
  }
}

void calcAiRisk() {
  float score = 0;
  if (flameConfirmed) score += 50;
  if (baselineReady) {
    float s = (float)smoke / (float)(SMOKE_RISE_THRESHOLD * 1.5f);
    if (s > 1) s = 1;
    score += s * 30.0f;
  }
  if (!dhtFault && tempRate > 1.0f) {
    float r = (tempRate - 1.0f) / 7.0f;
    if (r > 1) r = 1;
    score += r * 15.0f;
  }
  if (!dhtFault && !isnan(temperature) && temperature > 35.0f) {
    float a = (temperature - 35.0f) / 20.0f;
    if (a > 1) a = 1;
    score += a * 10.0f;
  }
  if (score > 100) score = 100;
  if (score > aiRiskF) aiRiskF = score;
  else aiRiskF = aiRiskF * 0.98f + score * 0.02f;
  aiRisk = (int)(aiRiskF + 0.5f);
}

void evaluate(unsigned long now) {
  calcAiRisk();
  bool trigger = flameConfirmed;
  bool aiTrig = false;
#if EXTRA_RULES
  bool smokeConf = smokeHigh && (now - smokeHighSince >= SMOKE_CONFIRM_MS);
  bool tempConf = tempHigh && !dhtFault && (now - tempHighSince >= TEMP_CONFIRM_MS);
  if (smokeConf && tempConf) trigger = true;
#endif
#if AI_TRIGGER
  if (aiRisk >= AI_ALERT_RISK) { aiTrig = true; trigger = true; }
#endif
  if (trigger && !fireActive) {
    fireActive = true;
    safeRunning = false;
    statusDirty = true;
    reason = flameConfirmed ? "FLAME DETECTED" :
             (aiTrig ? "AI RISK HIGH" : "SMOKE + HIGH TEMPERATURE");
    snprintf(notifyMsg, sizeof(notifyMsg), "FIRE! %s (risk %d%%). Pump ON.", reason, aiRisk);
    notifyPending = true;
    Serial.printf("[STATE] >>> FIRE: %s (AI risk %d%%)\n", reason, aiRisk);
  } else if (fireActive) {
    bool safe = !flameConfirmed;
#if EXTRA_RULES
    safe = safe && !smokeHigh && !tempHigh;
#endif
#if AI_TRIGGER
    safe = safe && (aiRisk < AI_ALERT_RISK - 15);
#endif
    if (safe) {
      if (!safeRunning) { safeRunning = true; safeSince = now; }
      else if (now - safeSince >= RECOVERY_MS) {
        fireActive = false;
        safeRunning = false;
        statusDirty = true;
        notifyPending = false;
        Serial.println("[STATE] >>> NORMAL: fire gone, system restored");
      }
    } else safeRunning = false;
  }
}

void applyOutputs(unsigned long now) {
  setGreenLed(!fireActive);
  setRedLed(fireActive);
  setBuzzer(fireActive);
#if ALLOW_MANUAL_PUMP
  bool wantPump = fireActive || manualPump;
#else
  bool wantPump = fireActive;
#endif
  if (wantPump != pumpOn) {
    setPump(wantPump);
    statusDirty = true;
    v5Dirty = true;
    Serial.println(wantPump ? "[PUMP] ON" : "[PUMP] OFF");
  }
  refreshRelay(now);
}

void handleManualTimeout(unsigned long now) {
  if (manualPump && (now - manualPumpStart >= MANUAL_PUMP_MAX_MS)) {
    manualPump = false;
    Serial.println("[PUMP] Manual pump auto-off (time limit)");
  }
}

void processNotifications(unsigned long now) {
  if (!notifyPending || !Blynk.connected()) return;
  if (!notifySentOnce || (now - lastNotify >= NOTIFY_COOLDOWN_MS)) {
    Blynk.logEvent("iot_fire_and_smoke_prevention", notifyMsg);
    Serial.printf("[BLYNK] Notification sent: %s\n", notifyMsg);
    notifyPending = false;
    notifySentOnce = true;
    lastNotify = now;
  }
}

BLYNK_CONNECTED() {
  ignoreV5Until = millis() + 2500;
  v5ResyncAt = millis() + 2600;
  v5Resync = true;
  statusDirty = true;
  Serial.println("[BLYNK] Connected");
}

BLYNK_WRITE(V5) {
  int v = param.asInt();
  if ((long)(ignoreV5Until - millis()) > 0) {
    Serial.println("[BLYNK] V5 ignored during connect window");
    return;
  }
#if ALLOW_MANUAL_PUMP
  if (v) {
    manualPump = true;
    manualPumpStart = millis();
    Serial.println("[BLYNK] Manual pump: ON");
  } else {
    manualPump = false;
    Serial.println(fireActive ? "[BLYNK] Manual OFF ignored: FIRE protection keeps pump ON" :
                              "[BLYNK] Manual pump: OFF");
  }
#else
  (void)v;
  manualPump = false;
  Serial.println("[BLYNK] Manual pump disabled: pump runs only on fire");
#endif
  v5Dirty = true;
  statusDirty = true;
}

void sendBlynk(unsigned long now) {
  if (!dhtFault && !isnan(temperature)) {
    Blynk.virtualWrite(V0, temperature);
    Blynk.virtualWrite(V1, humidity);
  }
  Blynk.virtualWrite(V2, smoke);
  Blynk.virtualWrite(V3, flameConfirmed ? 1 : 0);
  Blynk.virtualWrite(V4, fireActive ? 1 : 0);
  Blynk.virtualWrite(V7, aiRisk);
  if (v5Dirty) { Blynk.virtualWrite(V5, pumpOn ? 1 : 0); v5Dirty = false; }
  statusDirty = false;
  lastBlynk = now;
}

void maintainConnectivity(unsigned long now) {
  if (WiFi.status() != WL_CONNECTED) {
    if (wasWifi) { wasWifi = false; Serial.println("[WIFI] Lost. Local protection continues."); }
    if (now - lastWifiTry >= WIFI_RETRY_MS) {
      lastWifiTry = now;
      Serial.println("[WIFI] Reconnecting...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
    return;
  }
  if (!wasWifi) {
    wasWifi = true;
    Serial.print("[WIFI] Connected, IP: ");
    Serial.println(WiFi.localIP());
  }
  if (Blynk.connected()) Blynk.run();
  else if (now - lastBlynkTry >= BLYNK_RETRY_MS) {
    lastBlynkTry = now;
    Serial.println("[BLYNK] Connecting...");
    if (!Blynk.connect(2000)) Serial.println("[BLYNK] Connect failed, will retry");
  }
}

void printStatus() {
  char t[16], h[16];
  if (dhtFault || isnan(temperature)) { strcpy(t, "ERR"); strcpy(h, "ERR"); }
  else {
    snprintf(t, sizeof(t), "%.1fC", temperature);
    snprintf(h, sizeof(h), "%.0f%%", humidity);
  }
  Serial.printf("[%s] Smoke=%d (raw %d) | Temp=%s (%.1fC/min) | Hum=%s | Flame=%d | AI Risk=%d%% | Red=%s Green=%s Buzzer=%s | Pump=%s%s (relayPin=%d) | WiFi=%s Blynk=%s\n",
    fireActive ? "FIRE" : "NORMAL", smoke, smokeRaw, t, tempRate, h,
    flameConfirmed ? 1 : 0, aiRisk,
    fireActive ? "ON" : "OFF", fireActive ? "OFF" : "ON",
    fireActive ? "ON" : "OFF", pumpOn ? "ON" : "OFF",
    (manualPump && !fireActive) ? "(manual)" : "",
    digitalRead(PIN_RELAY),
    WiFi.status() == WL_CONNECTED ? "OK" : "DOWN",
    Blynk.connected() ? "OK" : "DOWN");
}

void setup() {
  setPump(false);
  gpio_set_drive_capability((gpio_num_t)PIN_RELAY, GPIO_DRIVE_CAP_3);
  bootMs = millis();
  pinMode(PIN_BUZZER, OUTPUT); setBuzzer(false);
  pinMode(PIN_LED_RED, OUTPUT); setRedLed(false);
  pinMode(PIN_LED_GREEN, OUTPUT); setGreenLed(true);
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=== IoT Fire & Smoke Prevention System (AI risk + rules) ===");
  Serial.println("Test keys: 1=pump ON, 0=pump OFF, l=toggle active LOW/HIGH, s=settings");
  Serial.printf("Relay: relayActiveLow=%d (pin GPIO%d, driven always, wire pump on COM+NO)\n",
                relayActiveLow, PIN_RELAY);
  pinMode(PIN_FLAME, INPUT_PULLUP);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_MQ2, ADC_11db);
  dht.begin();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Blynk.config(BLYNK_AUTH_TOKEN);
  bootMs = millis();
  setPump(false);
  Serial.println("Startup: MQ-2 warming up 60 s, then clean-air learning 10 s...");
}

void loop() {
  unsigned long now = millis();
  maintainConnectivity(now);
  if (now - lastSample >= SAMPLE_MS) { lastSample = now; sampleInputs(now); evaluate(now); }
  if (now - lastDht >= DHT_MS) { lastDht = now; readDht(now); }
  handleSerialCmd();
  handleManualTimeout(now);
  applyOutputs(now);
  processNotifications(now);
  if (Blynk.connected()) {
    if (v5Resync && (long)(now - v5ResyncAt) >= 0) {
      v5Resync = false;
      v5Dirty = true;
    }
    if (statusDirty || v5Dirty || (now - lastBlynk >= BLYNK_SEND_MS)) sendBlynk(now);
  }
  if (now - lastPrint >= PRINT_MS) { lastPrint = now; printStatus(); }
}
