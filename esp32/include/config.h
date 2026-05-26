#pragma once

// ── WiFi ─────────────────────────────────────────────────────────────
#define WIFI_SSID      "UZO-3FA210"
#define WIFI_PASSWORD  "5831332c45"

// Static IP — skips DHCP negotiation, saves ~1-2 s on every boot.
// Set to your network. Leave STATIC_IP undefined to use DHCP instead.
//#define STATIC_IP      "192.168.1.200"
//#define STATIC_GATEWAY "192.168.1.1"
//#define STATIC_SUBNET  "255.255.255.0"

// ── MQTT (Raspberry Pi local IP) ─────────────────────────────────────
#define MQTT_BROKER    "192.168.1.189"
#define MQTT_PORT      1883
#define MQTT_CLIENT_ID "esp32-entry"
#define MQTT_KEEPALIVE 15   // seconds

// ── MQTT topics ───────────────────────────────────────────────────────
#define TOPIC_DOOR     "room/door"
#define TOPIC_PRESENCE "room/presence"

// ── BLE beacon ────────────────────────────────────────────────────────
// Android scans for this name / UUID to detect room proximity
#define BLE_DEVICE_NAME  "AutoLights-Room"
#define BLE_SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"

// ── LEDs ──────────────────────────────────────────────────────────────
#define PIN_LED_DOOR  2   // D2 — on when door is open

// ── GPIO pins ─────────────────────────────────────────────────────────
// Door: magnetic reed switch, NC type, wired to GND + pull-up enabled.
//   Door CLOSED → pin LOW   |   Door OPEN → pin HIGH
#define PIN_DOOR_SENSOR  25

// mmWave (LD2410 OUT pin): HIGH = presence detected, LOW = absent
#define PIN_MMWAVE_OUT   26

// Define which level means "door is open" (change to LOW for NO switches)
#define DOOR_OPEN_LEVEL  HIGH

// ── Timing ────────────────────────────────────────────────────────────
#define DEBOUNCE_MS      50     // sensor debounce window
#define MQTT_RETRY_MS      2000  // ms between MQTT reconnect attempts
#define MQTT_SOCKET_TMO       3  // seconds before a connect() attempt gives up
#define WIFI_RETRY_MS     10000  // ms between WiFi reconnect attempts
