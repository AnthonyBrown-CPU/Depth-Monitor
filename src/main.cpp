#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "time.h"
#include "config.h"

// Calibration (linear fit over 10–30 cm measured range; sensor non-linear below ~10 cm)
const float V_FLOOR_MV       = 150.0f; // ADC_6db lower bound — readings unreliable below this (~17 cm)
const float V_MIN_MV         = -24.25f; // extrapolated voltage at 0 cm depth
const float MV_PER_CM        = 10.25f;  // mV per cm

const int   POLL_INTERVAL_MS = 60000;

// -- Hardware ---------------------------------------------------------------
static const int ADC_PIN     = 33;
static const int ADC_SAMPLES = 16;  // readings averaged per measurement

// -- New Relic --------------------------------------------------------------
const char* NR_ENDPOINT      = "https://metric-api.eu.newrelic.com/metric/v1";
const char* METRIC_NAME      = "custom.depth_monitor.water_depth_cm";

// -- NTP --------------------------------------------------------------------
const char* NTP_SERVER       = "pool.ntp.org";

// ---------------------------------------------------------------------------

void connectWiFi() {
    Serial.printf("Connecting to %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\nConnected. IP: %s\n", WiFi.localIP().toString().c_str());
}

void syncNTP() {
    configTime(0, 0, NTP_SERVER);
    Serial.print("Waiting for NTP sync");
    struct tm ti;
    while (!getLocalTime(&ti)) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nNTP synced.");
}

float readDepthCm() {
    long sum = 0;
    for (int i = 0; i < ADC_SAMPLES; i++) {
        sum += analogReadMilliVolts(ADC_PIN);
        delay(2);
    }
    float voltage_mv = sum / (float)ADC_SAMPLES;

    if (voltage_mv < V_FLOOR_MV) {
        Serial.printf("ADC: %.1f mV  ->  below floor, reporting 0\n", voltage_mv);
        return 0.0f;
    }

    float depth_cm = (voltage_mv - V_MIN_MV) / MV_PER_CM;
    Serial.printf("ADC: %.1f mV  ->  Depth: %.1f cm\n", voltage_mv, depth_cm);
    return depth_cm;
}

bool postToNewRelic(float depth_cm) {
    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();
    }

    long long timestamp_ms = (long long)time(nullptr) * 1000LL;

    char payload[256];
    snprintf(payload, sizeof(payload),
        "[{\"metrics\":[{"
        "\"name\":\"%s\","
        "\"type\":\"gauge\","
        "\"value\":%.2f,"
        "\"timestamp\":%lld"
        "}]}]",
        METRIC_NAME, depth_cm, timestamp_ms);

    // Certificate validation omitted for MVP — add a root CA before production deployment
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.begin(client, NR_ENDPOINT);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Api-Key", NR_LICENSE_KEY);

    int  code = http.POST(payload);
    bool ok   = (code == 202);

    if (ok) {
        Serial.printf("New Relic: 202 Accepted\n");
    } else {
        Serial.printf("New Relic: HTTP %d  %s\n", code, http.getString().c_str());
    }

    http.end();
    return ok;
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    analogSetPinAttenuation(ADC_PIN, ADC_6db);  // 0.15 – 1.75V range
    connectWiFi();
    syncNTP();
}

void loop() {
    float depth = readDepthCm();
    postToNewRelic(depth);
    delay(POLL_INTERVAL_MS);
}
