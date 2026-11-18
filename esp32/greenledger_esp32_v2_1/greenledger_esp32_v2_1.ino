/*
 * GreenLedger — ESP32 Sensor Node v2.1
 * =====================================
 * Sensors  : Soil Moisture (GPIO34) + DHT11 (GPIO15)
 * EC sensor removed — not available in current prototype
 * Sends    : ML carbon estimate + estimated credits to ThingSpeak every 30s
 *
 * Wiring:
 *   Soil Moisture AOUT  →  GPIO34
 *   Soil Moisture VCC   →  3.3V
 *   Soil Moisture GND   →  GND
 *   DHT11 DATA          →  GPI15
 *   DHT11 VCC           →  3.3V
 *   DHT11 GND           →  GND
 *
 * Libraries needed (Arduino Library Manager):
 *   DHT sensor       by Adafruit
 *   Adafruit Unified Sensor
 *
 * Board: DOIT ESP32 DEVKIT V1
 * Upload Speed: 115200
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include "secrets.h"

/* Keep WiFi credentials and the ThingSpeak Write API Key in secrets.h. */
const char* THINGSPEAK_URL = "https://api.thingspeak.com/update";

const unsigned long SEND_INTERVAL = 30000UL; // 30 seconds
const unsigned long WIFI_RETRY_INTERVAL = 10000UL;
const unsigned long WIFI_CONNECT_TIMEOUT = 20000UL;
/* ════════════════════════════════════════════════════════ */

/* ── Pin definitions ── */
#define SOIL_PIN    34
#define DHT_PIN     15
#define DHT_TYPE    DHT11
#define LED_PIN     2

/* ── Soil moisture calibration ── */
// Step 1: Hold sensor in dry air → note RAW value → set as SOIL_DRY
// Step 2: Put sensor in water   → note RAW value → set as SOIL_WET
const int SOIL_DRY = 3239;  // adjust after calibration
const int SOIL_WET = 1525;  // adjust after calibration

DHT dht(DHT_PIN, DHT_TYPE);
unsigned long lastSendAttempt = 0;
unsigned long lastWiFiAttempt = 0;
unsigned long wifiAttemptStartedAt = 0;
bool wifiConnecting = false;

/* ════════════════════════════════════════════════════════ */
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== GreenLedger ESP32 Sensor Node v2.1 ===");
  Serial.println("Destination: GreenLedger ML → ThingSpeak carbon results");
  Serial.println("Sensors: Soil Moisture + DHT11");
  Serial.println("==========================================\n");

  pinMode(LED_PIN, OUTPUT);
  analogReadResolution(12);
  dht.begin();
  delay(2000); // DHT11 needs 2 seconds to stabilise

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  startWiFiConnection();
}

void loop() {
  maintainWiFiConnection();

  unsigned long now = millis();
  if (lastSendAttempt == 0 || now - lastSendAttempt >= SEND_INTERVAL) {
    // Schedule the next attempt even when offline, instead of retrying every loop.
    lastSendAttempt = now;

    float moisture, temp, humidity, socProxy;

    if (!readSensors(moisture, temp, humidity, socProxy)) {
      Serial.println("[ERROR] Sensor read failed — check wiring");
      blink(3, 300);
      delay(5000);
      return;
    }

    printReadings(moisture, temp, humidity, socProxy);

    float co2Tons = 0.0f;
    int credits = 0;
    bool hasModelEstimate = getModelEstimate(
      moisture, temp, co2Tons, credits
    );

    if (hasModelEstimate) {
      Serial.print("[ML] Estimated CO2: "); Serial.print(co2Tons, 3);
      Serial.print(" tonnes; credits: "); Serial.println(credits);
    } else {
      Serial.println("[ML] No credit estimate; sensor fields will still upload");
    }

    if (sendToThingSpeak(moisture, temp, humidity, socProxy,
                         hasModelEstimate, credits)) {
      blink(2, 150);
    } else {
      Serial.println("[ERROR] ThingSpeak upload failed — retrying in 30s");
      blink(5, 80);
    }
  }
  delay(500);
}

/* ── Read sensors ── */
bool readSensors(float& moisture, float& temp, float& humidity,
                 float& socProxy) {

  // Soil moisture — average 10 ADC readings to reduce noise
  long adc_sum = 0;
  for (int i = 0; i < 10; i++) {
    adc_sum += analogRead(SOIL_PIN);
    delay(10);
  }
  int adc_avg = adc_sum / 10;
  moisture = map(adc_avg, SOIL_DRY, SOIL_WET, 0, 100);
  moisture = constrain(moisture, 0.0f, 100.0f);

  Serial.print("[ADC] Raw: "); Serial.print(adc_avg);
  Serial.print(" → Moisture: "); Serial.print(moisture, 1); Serial.println("%");

  // DHT11 — retry up to 3 times
  for (int attempt = 0; attempt < 3; attempt++) {
    humidity = dht.readHumidity();
    temp     = dht.readTemperature();

    if (!isnan(humidity) && !isnan(temp)) {
      // Retain the existing Field 4 moisture-based SOC proxy.
      // It is not a measured soil organic carbon value.
      socProxy = 0.18f + 1.24f * (moisture / 100.0f);
      socProxy = constrain(socProxy, 0.3f, 5.0f);
      return true;
    }
    Serial.print("[DHT] Read failed, attempt ");
    Serial.println(attempt + 1);
    delay(2000);
  }

  Serial.println("[DHT] All 3 attempts failed — check wiring on GPIO15");
  return false;
}

/* ── Request an estimate from the GreenLedger Random Forest API ── */
bool getModelEstimate(float moisture, float temperature, float& co2Tons,
                      int& credits) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[ML] Skipped: WiFi is offline");
    return false;
  }
  if (String(AI_ESTIMATE_URL).startsWith("https://YOUR_") ||
      FARM_NDVI < 0.0f || FARM_NDVI > 1.0f ||
      FARM_AREA_HECTARES <= 0.0f || FARM_SOIL_ORGANIC_CARBON <= 0.0f) {
    Serial.println("[ML] Configure API URL and farm inputs in secrets.h");
    return false;
  }

  StaticJsonDocument<384> requestDoc;
  requestDoc["ndvi"] = FARM_NDVI;
  requestDoc["area_hectares"] = FARM_AREA_HECTARES;
  requestDoc["soil_organic_carbon"] = FARM_SOIL_ORGANIC_CARBON;
  requestDoc["temperature_avg"] = temperature;
  requestDoc["soil_moisture_avg"] = moisture;
  requestDoc["monitoring_months"] = FARM_MONITORING_MONTHS;
  requestDoc["practice"] = FARM_PRACTICE;
  requestDoc["crop_type"] = FARM_CROP_TYPE;

  String body;
  serializeJson(requestDoc, body);

  WiFiClientSecure client;
  // See the ThingSpeak request below: TLS is encrypted, but server validation
  // should be enabled with a CA certificate before production use.
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  if (!http.begin(client, AI_ESTIMATE_URL)) {
    Serial.println("[ML] Could not start API request");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  String response = http.getString();
  http.end();

  if (code != HTTP_CODE_OK) {
    Serial.print("[ML] API HTTP code: "); Serial.println(code);
    Serial.println("[ML] Estimate unavailable; sensor data will still upload");
    return false;
  }

  StaticJsonDocument<512> responseDoc;
  DeserializationError parseError = deserializeJson(responseDoc, response);
  if (parseError || responseDoc["co2_tons"].isNull() ||
      responseDoc["credits"].isNull()) {
    Serial.println("[ML] Invalid estimate response; sensor data will still upload");
    return false;
  }

  co2Tons = responseDoc["co2_tons"].as<float>();
  credits = responseDoc["credits"].as<int>();
  return true;
}

/* ── Upload existing sensor fields and the optional credit estimate ── */
bool sendToThingSpeak(float moisture, float temp, float humidity,
                      float socProxy, bool hasModelEstimate, int credits) {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Not connected — skipping send");
    return false;
  }

  if (String(WIFI_SSID).startsWith("YOUR_") ||
      String(WIFI_PASSWORD).startsWith("YOUR_") ||
      String(THINGSPEAK_WRITE_API_KEY).startsWith("YOUR_")) {
    Serial.println("[Config] Fill in WiFi and ThingSpeak values in secrets.h");
    return false;
  }

  // Preserve current channel fields 1–4 and add credit count as Field 5.
  String body = "field1=" + String(moisture, 1) +
                "&field2=" + String(temp, 1) +
                "&field3=" + String(humidity, 1) +
                "&field4=" + String(socProxy, 2);
  if (hasModelEstimate) {
    body += "&field5=" + String(credits);
  }

  WiFiClientSecure client;
  // TLS encrypts the request. For production, replace this with setCACert()
  // and a maintained CA certificate so the server identity is verified.
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(client, THINGSPEAK_URL)) {
    Serial.println("[ThingSpeak] Could not start HTTP request");
    return false;
  }
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("THINGSPEAKAPIKEY", THINGSPEAK_WRITE_API_KEY);
  int code = http.POST(body);
  String resp = http.getString();
  http.end();

  Serial.print("[ThingSpeak] HTTP code: "); Serial.println(code);
  Serial.print("[ThingSpeak] Entry response: "); Serial.println(resp);

  // ThingSpeak returns the new entry ID; 0 means the update was rejected
  // (often because the channel key is wrong or the update rate is too high).
  if (code == HTTP_CODE_OK && resp.toInt() > 0) {
    Serial.println("[OK] Reading saved to ThingSpeak");
    return true;
  }

  return false;
}

/* ── Start a single WiFi connection attempt ── */
void startWiFiConnection() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  wifiAttemptStartedAt = millis();
  lastWiFiAttempt = wifiAttemptStartedAt;
  wifiConnecting = true;
}

/* ── Monitor WiFi and retry without restarting an active connection ── */
void maintainWiFiConnection() {
  unsigned long now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    if (wifiConnecting) {
      wifiConnecting = false;
      Serial.println("\n[WiFi] Connected!");
      Serial.print("[WiFi] IP address: ");
      Serial.println(WiFi.localIP());
      blink(3, 200);
    }
    return;
  }

  if (wifiConnecting && now - wifiAttemptStartedAt >= WIFI_CONNECT_TIMEOUT) {
    Serial.println("\n[WiFi] Connection timed out; will retry shortly");
    WiFi.disconnect();
    wifiConnecting = false;
    lastWiFiAttempt = now;
  }

  if (!wifiConnecting && now - lastWiFiAttempt >= WIFI_RETRY_INTERVAL) {
    startWiFiConnection();
  }
}

/* ── LED blink helper ── */
void blink(int n, int ms) {
  for (int i = 0; i < n; i++) {
    digitalWrite(LED_PIN, HIGH); delay(ms);
    digitalWrite(LED_PIN, LOW);  delay(ms);
  }
}

/* ── Print readings to Serial Monitor ── */
void printReadings(float m, float t, float h, float socProxy) {
  Serial.println("─────────────────────────────");
  Serial.print("  Soil Moisture : "); Serial.print(m,   1); Serial.println(" %");
  Serial.print("  Temperature   : "); Serial.print(t,   1); Serial.println(" °C");
  Serial.print("  Humidity      : "); Serial.print(h,   1); Serial.println(" %");
  Serial.print("  Estimated SOC : "); Serial.print(socProxy, 2); Serial.println(" % (proxy)");
  Serial.println("─────────────────────────────");
}
