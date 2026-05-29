#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <BLEDevice.h>
#include <BLEAdvertising.h>

#include "config.h"

// ─────────────────────────────────────────────────────────────────────
// Inter-task message queue
// ─────────────────────────────────────────────────────────────────────

struct MqttMessage {
    char  topic[64];
    char  payload[32];
    bool  retain;
};

static QueueHandle_t publishQueue;

// ─────────────────────────────────────────────────────────────────────
// Shared state (written by one task, read by others — bool is atomic on ESP32)
// ─────────────────────────────────────────────────────────────────────

static volatile bool doorOpen      = false;
static volatile bool mqttConnected = false;

// ─────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────

static void updateDoorLED() {
    digitalWrite(PIN_LED_DOOR, (doorOpen && mqttConnected) ? HIGH : LOW);
}

static void enqueueMqtt(const char *topic, const char *payload, bool retain = true) {
    MqttMessage msg;
    strncpy(msg.topic,   topic,   sizeof(msg.topic)   - 1);
    strncpy(msg.payload, payload, sizeof(msg.payload) - 1);
    msg.topic[sizeof(msg.topic) - 1]     = '\0';
    msg.payload[sizeof(msg.payload) - 1] = '\0';
    msg.retain = retain;
    xQueueSend(publishQueue, &msg, pdMS_TO_TICKS(50));
}

// ─────────────────────────────────────────────────────────────────────
// MQTT Task — Core 0 (same core as the WiFi/BLE protocol stack)
// ─────────────────────────────────────────────────────────────────────

static void mqttTask(void *) {
    WiFiClient   wifiClient;
    PubSubClient mqtt(wifiClient);
    mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    mqtt.setKeepAlive(MQTT_KEEPALIVE);
    mqtt.setSocketTimeout(MQTT_SOCKET_TMO);

    unsigned long wifiRetryAt = 0;
    unsigned long mqttRetryAt = 0;

    for (;;) {
        // ── WiFi ──────────────────────────────────────────────────────
        if (WiFi.status() != WL_CONNECTED) {
            mqttConnected = false;
            updateDoorLED();

            unsigned long now = millis();
            if (now >= wifiRetryAt) {
                wifiRetryAt = now + WIFI_RETRY_MS;
                Serial.printf("[WiFi] Connecting to %s…\n", WIFI_SSID);
                WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        // ── MQTT ──────────────────────────────────────────────────────
        if (!mqtt.connected()) {
            mqttConnected = false;
            updateDoorLED();

            unsigned long now = millis();
            if (now >= mqttRetryAt) {
                mqttRetryAt = now + MQTT_RETRY_MS;
                Serial.printf("[MQTT] Connecting to %s:%d… ", MQTT_BROKER, MQTT_PORT);
                if (mqtt.connect(MQTT_CLIENT_ID)) {
                    Serial.printf("OK (uptime %lus)\n", millis() / 1000);
                    mqttConnected = true;
                    updateDoorLED();
                    // Re-assert state after reconnect
                    enqueueMqtt(TOPIC_DOOR, doorOpen ? "OPEN" : "CLOSED");
                } else {
                    Serial.printf("failed rc=%d, retry in %ds\n",
                                  mqtt.state(), MQTT_RETRY_MS / 1000);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // ── Keepalive + drain queue ────────────────────────────────────
        mqtt.loop();

        MqttMessage msg;
        while (xQueueReceive(publishQueue, &msg, 0) == pdTRUE) {
            mqtt.publish(msg.topic, msg.payload, msg.retain);
            Serial.printf("[MQTT] %s -> %s\n", msg.topic, msg.payload);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ─────────────────────────────────────────────────────────────────────
// Sensor Task — Core 1 (application core)
// ─────────────────────────────────────────────────────────────────────

static void sensorTask(void *) {
    bool doorRaw       = (digitalRead(PIN_DOOR_SENSOR) == DOOR_OPEN_LEVEL);
    bool doorConfirmed = doorRaw;
    doorOpen           = doorRaw;
    unsigned long doorChangedAt = 0;

    for (;;) {
        unsigned long now    = millis();
        bool          reading = (digitalRead(PIN_DOOR_SENSOR) == DOOR_OPEN_LEVEL);

        if (reading != doorRaw) {
            doorRaw       = reading;
            doorChangedAt = now;
        } else if (doorRaw != doorConfirmed && (now - doorChangedAt >= DEBOUNCE_MS)) {
            doorConfirmed = doorRaw;
            doorOpen      = doorConfirmed;
            updateDoorLED();
            if (mqttConnected) {
                enqueueMqtt(TOPIC_DOOR, doorOpen ? "OPEN" : "CLOSED");
            }
            Serial.printf("[Door] %s\n", doorOpen ? "OPEN" : "CLOSED");
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ─────────────────────────────────────────────────────────────────────
// Status Task — Core 1
// ─────────────────────────────────────────────────────────────────────

static void statusTask(void *) {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(STATUS_INTERVAL_MS));
        unsigned long upSec = millis() / 1000;
        Serial.println("┌─── Status ───────────────────────────────────");
        Serial.printf( "│ Uptime : %02lum %02lus\n", upSec / 60, upSec % 60);
        Serial.printf( "│ WiFi   : %s  RSSI %d dBm\n",
                       WiFi.status() == WL_CONNECTED
                           ? WiFi.localIP().toString().c_str() : "disconnected",
                       WiFi.RSSI());
        Serial.printf( "│ MQTT   : %s\n", mqttConnected ? "connected" : "disconnected");
        Serial.printf( "│ Door   : %s\n", doorOpen ? "OPEN" : "CLOSED");
        Serial.printf( "│ LED D2 : %s\n", (doorOpen && mqttConnected) ? "ON" : "OFF");
        Serial.println("└──────────────────────────────────────────────");
    }
}

// ─────────────────────────────────────────────────────────────────────
// BLE beacon
// ─────────────────────────────────────────────────────────────────────

static void startBLEBeacon() {
    BLEDevice::init(BLE_DEVICE_NAME);
    BLEAdvertising *pAdv = BLEDevice::getAdvertising();
    pAdv->addServiceUUID(BLEUUID(BLE_SERVICE_UUID));
    pAdv->setScanResponse(true);
    pAdv->setMinPreferred(0x06);
    pAdv->setMaxPreferred(0x12);
    BLEDevice::startAdvertising();
    Serial.printf("[BLE] Advertising as \"%s\" (UUID: %s)\n",
                  BLE_DEVICE_NAME, BLE_SERVICE_UUID);
}

// ─────────────────────────────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);

    pinMode(PIN_DOOR_SENSOR, INPUT_PULLUP);
    pinMode(PIN_LED_DOOR, OUTPUT);
    digitalWrite(PIN_LED_DOOR, LOW);

    publishQueue = xQueueCreate(16, sizeof(MqttMessage));

    // BLE must start before WiFi on shared radio
    startBLEBeacon();
    WiFi.mode(WIFI_STA);

    // Task priorities: 2 = MQTT (higher, time-sensitive), 1 = sensors/status
    xTaskCreatePinnedToCore(mqttTask,   "mqtt",   8192, nullptr, 2, nullptr, 0);
    xTaskCreatePinnedToCore(sensorTask, "sensor", 4096, nullptr, 1, nullptr, 1);
    xTaskCreatePinnedToCore(statusTask, "status", 4096, nullptr, 1, nullptr, 1);

    Serial.println("[Boot] RTOS tasks started");
}

void loop() {
    vTaskDelay(portMAX_DELAY); // loop() is unused — all work is in RTOS tasks
}
