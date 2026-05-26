#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <BLEDevice.h>
#include <BLEAdvertising.h>

#include "config.h"

// ─────────────────────────────────────────────────────────────────────
// Globals
// ─────────────────────────────────────────────────────────────────────

WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

// Confirmed (debounced) sensor states — published to MQTT on change
bool doorOpen    = false;
bool presPresent = false;

// Raw (un-debounced) readings and their timestamps
bool         doorRaw         = false;
bool         presRaw         = false;
unsigned long doorChangedAt  = 0;
unsigned long presChangedAt  = 0;

// Reconnect back-off timestamps
unsigned long mqttLastAttempt = 0;
unsigned long wifiLastAttempt = 0;

// Previous MQTT connection state — used to detect connect/disconnect events
bool mqttWasConnected = false;

// Periodic status print
unsigned long lastStatusAt = 0;
#define STATUS_INTERVAL_MS 5000

// ─────────────────────────────────────────────────────────────────────
// Periodic status print
// ─────────────────────────────────────────────────────────────────────

void printStatus() {
    unsigned long upSec = millis() / 1000;
    Serial.println("┌─── Status ───────────────────────────────────");
    Serial.printf( "│ Uptime   : %02lum %02lus\n", upSec / 60, upSec % 60);
    Serial.printf( "│ WiFi     : %s  RSSI %d dBm\n",
                   WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "disconnected",
                   WiFi.RSSI());
    Serial.printf( "│ MQTT     : %s (broker %s:%d)\n",
                   mqtt.connected() ? "connected" : "disconnected",
                   MQTT_BROKER, MQTT_PORT);
    Serial.printf( "│ Door     : %s\n", doorOpen    ? "OPEN"    : "CLOSED");
    Serial.printf( "│ Presence : %s\n", presPresent ? "PRESENT" : "ABSENT");
    Serial.printf( "│ LED D2   : %s\n", (doorOpen && mqtt.connected()) ? "ON" : "OFF");
    Serial.println("└──────────────────────────────────────────────");
}

// ─────────────────────────────────────────────────────────────────────
// LED helper
// ─────────────────────────────────────────────────────────────────────

// LED is on only when the door is open AND the broker is reachable
void updateDoorLED() {
    digitalWrite(PIN_LED_DOOR, (doorOpen && mqtt.connected()) ? HIGH : LOW);
}

// ─────────────────────────────────────────────────────────────────────
// MQTT publishing helpers
// ─────────────────────────────────────────────────────────────────────

void publishDoor(bool open) {
    const char *msg = open ? "OPEN" : "CLOSED";
    mqtt.publish(TOPIC_DOOR, msg, /*retain=*/true);
    Serial.printf("[MQTT] %s -> %s\n", TOPIC_DOOR, msg);
}

void publishPresence(bool present) {
    const char *msg = present ? "PRESENT" : "ABSENT";
    mqtt.publish(TOPIC_PRESENCE, msg, /*retain=*/true);
    Serial.printf("[MQTT] %s -> %s\n", TOPIC_PRESENCE, msg);
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
#ifdef STATIC_IP
    IPAddress ip, gw, sn;
    ip.fromString(STATIC_IP);
    gw.fromString(STATIC_GATEWAY);
    sn.fromString(STATIC_SUBNET);
    WiFi.config(ip, gw, sn);
#endif
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    // Block up to 10 s on first boot; non-blocking retries handled in loop()
    unsigned long deadline = millis() + 10000;
    while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
        delay(250);
        Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] Connected — IP: %s  RSSI: %d dBm\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
        Serial.printf("\n[WiFi] Failed — will retry in %d s\n", WIFI_RETRY_MS / 1000);
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
        Serial.printf("OK  (uptime %lus)\n", millis() / 1000);
        // Re-assert retained state so the coordinator is in sync
        publishDoor(doorOpen);
        publishPresence(presPresent);
    } else {
        // PubSubClient state codes: -4=timeout -3=denied -2=unavailable
        //   -1=bad protocol  1=bad client-id  2=unavailable  3=bad creds  5=unauthorised
        Serial.printf("failed (rc=%d) — retry in %ds\n",
                      mqtt.state(), MQTT_RETRY_MS / 1000);
    }
}

// ─────────────────────────────────────────────────────────────────────
// BLE beacon
// ─────────────────────────────────────────────────────────────────────

void startBLEBeacon() {
    BLEDevice::init(BLE_DEVICE_NAME);

    BLEAdvertising *pAdv = BLEDevice::getAdvertising();
    pAdv->addServiceUUID(BLEUUID(BLE_SERVICE_UUID));
    pAdv->setScanResponse(true);
    // Advertise every ~150 ms (units of 0.625 ms → 240 * 0.625 = 150 ms)
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
    delay(500); // let serial settle

    // Sensor pins
    pinMode(PIN_DOOR_SENSOR, INPUT_PULLUP);
    pinMode(PIN_MMWAVE_OUT,  INPUT);

    // LEDs
    pinMode(PIN_LED_DOOR, OUTPUT);
    digitalWrite(PIN_LED_DOOR, LOW);

    // Snapshot initial sensor readings so we don't publish a spurious change
    doorRaw     = (digitalRead(PIN_DOOR_SENSOR) == DOOR_OPEN_LEVEL);
    presRaw     = (digitalRead(PIN_MMWAVE_OUT)  == HIGH);
    doorOpen    = doorRaw;
    presPresent = presRaw;
    updateDoorLED(); // off at boot until MQTT connects

    // BLE first — it takes ~500 ms to initialise and has its own scheduler
    startBLEBeacon();

    // WiFi + MQTT (blocking connect on first boot)
    connectWiFi();
    // NOTE: WiFi.setSleep(false) cannot be used — modem sleep is required
    // for WiFi+BLE coexistence on the ESP32's shared radio.
    mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    mqtt.setKeepAlive(MQTT_KEEPALIVE);
    mqtt.setSocketTimeout(MQTT_SOCKET_TMO); // don't block 15 s on a failed connect
    connectMQTT();

    Serial.println("[Boot] ESP32 entry/identity node ready");
    Serial.printf("       Door: %s | Presence: %s\n",
                  doorOpen ? "OPEN" : "CLOSED",
                  presPresent ? "PRESENT" : "ABSENT");
}

// ─────────────────────────────────────────────────────────────────────
// Loop
// ─────────────────────────────────────────────────────────────────────

void loop() {
    // Keep connections alive
    connectWiFi();
    connectMQTT();
    mqtt.loop(); // handles MQTT keepalive + incoming (none expected here)

    unsigned long now = millis();

    // Update LED immediately on MQTT connect or disconnect
    bool mqttNow = mqtt.connected();
    if (mqttNow != mqttWasConnected) {
        mqttWasConnected = mqttNow;
        Serial.printf("[MQTT] %s\n", mqttNow ? "broker connected" : "broker disconnected");
        updateDoorLED();
    }

    // Periodic status print
    if (now - lastStatusAt >= STATUS_INTERVAL_MS) {
        lastStatusAt = now;
        printStatus();
    }

    // ── Door sensor (NC reed switch, debounce) ────────────────────────
    bool doorRead = (digitalRead(PIN_DOOR_SENSOR) == DOOR_OPEN_LEVEL);
    if (doorRead != doorRaw) {
        // Signal changed — reset debounce timer
        doorRaw        = doorRead;
        doorChangedAt  = now;
    } else if (doorRaw != doorOpen && (now - doorChangedAt >= DEBOUNCE_MS)) {
        // Stable for DEBOUNCE_MS — commit the change
        doorOpen = doorRaw;
        updateDoorLED();
        if (mqtt.connected()) publishDoor(doorOpen);
    }

    // ── mmWave presence sensor (LD2410 OUT, debounce) ─────────────────
    bool presRead = (digitalRead(PIN_MMWAVE_OUT) == HIGH);
    if (presRead != presRaw) {
        presRaw       = presRead;
        presChangedAt = now;
    } else if (presRaw != presPresent && (now - presChangedAt >= DEBOUNCE_MS)) {
        presPresent = presRaw;
        if (mqtt.connected()) publishPresence(presPresent);
    }

    delay(1);
}
