#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "config.h"

// ─────────────────────────────────────────────────────────────────────
// Globals
// ─────────────────────────────────────────────────────────────────────

WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

bool lightOn = false;

unsigned long mqttLastAttempt = 0;
unsigned long wifiLastAttempt = 0;
unsigned long lastLuxAt       = 0;

// ─────────────────────────────────────────────────────────────────────
// Light control
// ─────────────────────────────────────────────────────────────────────

void applyLightState(bool on) {
    lightOn = on;
    digitalWrite(PIN_RELAY, on ? RELAY_ON_LEVEL : !RELAY_ON_LEVEL);
    // Built-in LED mirrors state (active LOW — invert the logic)
    digitalWrite(PIN_LED, on ? LOW : HIGH);
    Serial.printf("[Light] %s\n", on ? "ON" : "OFF");
}

void publishLightState() {
    mqtt.publish(TOPIC_LIGHT_STATE, lightOn ? "ON" : "OFF", /*retain=*/true);
    Serial.printf("[MQTT] %s -> %s\n", TOPIC_LIGHT_STATE, lightOn ? "ON" : "OFF");
}

// ─────────────────────────────────────────────────────────────────────
// MQTT callback — receives room/light/cmd
// ─────────────────────────────────────────────────────────────────────

void onMessage(char *topic, byte *payload, unsigned int length) {
    String msg;
    for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
    msg.trim();
    Serial.printf("[MQTT] %s <- %s\n", topic, msg.c_str());

    if (String(topic) == TOPIC_LIGHT_CMD) {
        bool wantOn = (msg == "ON");
        if (wantOn != lightOn) {
            applyLightState(wantOn);
            publishLightState();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────
// WiFi
// ─────────────────────────────────────────────────────────────────────

void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;

    unsigned long now = millis();
    if (now - wifiLastAttempt < WIFI_RETRY_MS) return;
    wifiLastAttempt = now;

    Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long deadline = millis() + 10000;
    while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
        delay(250);
        Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] Connected — IP: %s  RSSI: %d dBm\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
        Serial.printf("\n[WiFi] Failed — retry in %ds\n", WIFI_RETRY_MS / 1000);
    }
}

// ─────────────────────────────────────────────────────────────────────
// MQTT
// ─────────────────────────────────────────────────────────────────────

void connectMQTT() {
    if (mqtt.connected()) return;
    if (WiFi.status() != WL_CONNECTED) return;

    unsigned long now = millis();
    if (now - mqttLastAttempt < MQTT_RETRY_MS) return;
    mqttLastAttempt = now;

    Serial.printf("[MQTT] Connecting to %s:%d… ", MQTT_BROKER, MQTT_PORT);
    if (mqtt.connect(MQTT_CLIENT_ID)) {
        Serial.printf("OK (uptime %lus)\n", millis() / 1000);
        mqtt.subscribe(TOPIC_LIGHT_CMD);
        // Re-assert current state on reconnect so coordinator stays in sync
        publishLightState();
    } else {
        Serial.printf("failed (rc=%d) — retry in %ds\n",
                      mqtt.state(), MQTT_RETRY_MS / 1000);
    }
}

// ─────────────────────────────────────────────────────────────────────
// Lux sampling
// ─────────────────────────────────────────────────────────────────────

void sampleAndPublishLux() {
    int raw = analogRead(A0);   // 0 (dark) – 1023 (bright)
    char buf[8];
    itoa(raw, buf, 10);
    mqtt.publish(TOPIC_LUX, buf, /*retain=*/true);
    Serial.printf("[MQTT] %s -> %s (raw ADC)\n", TOPIC_LUX, buf);
}

// ─────────────────────────────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);

    // Relay + LED
    pinMode(PIN_RELAY, OUTPUT);
    pinMode(PIN_LED,   OUTPUT);
    applyLightState(false); // start with light OFF

    // LDR on A0 — no initialisation needed, analogRead works out of the box
    pinMode(A0, INPUT);
    Serial.printf("[LDR] Initial reading: %d\n", analogRead(A0));

    connectWiFi();
    mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    mqtt.setKeepAlive(MQTT_KEEPALIVE);
    mqtt.setSocketTimeout(MQTT_SOCKET_TMO);
    mqtt.setCallback(onMessage);
    connectMQTT();

    Serial.println("[Boot] ESP8266 light-actuator node ready");
}

// ─────────────────────────────────────────────────────────────────────
// Loop
// ─────────────────────────────────────────────────────────────────────

void loop() {
    connectWiFi();
    connectMQTT();
    mqtt.loop();

    unsigned long now = millis();

    // Sample lux every LUX_INTERVAL_MS
    if (now - lastLuxAt >= LUX_INTERVAL_MS) {
        lastLuxAt = now;
        if (mqtt.connected()) sampleAndPublishLux();
    }

    delay(10);
}
