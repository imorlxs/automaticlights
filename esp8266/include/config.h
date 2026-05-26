#pragma once

// ── WiFi ─────────────────────────────────────────────────────────────
#define WIFI_SSID      "YOUR_SSID"
#define WIFI_PASSWORD  "YOUR_PASSWORD"

// ── MQTT (Raspberry Pi local IP) ─────────────────────────────────────
#define MQTT_BROKER    "192.168.1.189"
#define MQTT_PORT      1883
#define MQTT_CLIENT_ID "esp8266-actuator"
#define MQTT_KEEPALIVE 15

// ── MQTT topics ───────────────────────────────────────────────────────
#define TOPIC_LIGHT_CMD   "room/light/cmd"    // subscribed — receives ON/OFF
#define TOPIC_LIGHT_STATE "room/light/state"  // published  — confirms state
#define TOPIC_LUX         "room/lux"          // published  — lux reading

// ── GPIO pins ─────────────────────────────────────────────────────────
// Relay module (most common modules are active LOW)
//   RELAY_ON_LEVEL LOW  → relay energises on LOW  (active-low modules)
//   RELAY_ON_LEVEL HIGH → relay energises on HIGH (active-high modules)
#define PIN_RELAY       12    // D6 on D1 Mini / NodeMCU
#define RELAY_ON_LEVEL  LOW

// BH1750 lux sensor uses I2C — default pins on ESP8266:
//   SDA = GPIO4  (D2)
//   SCL = GPIO5  (D1)
// No pin defines needed — Wire library uses them automatically.

// Built-in LED mirrors the light state (active LOW on most ESP8266 boards)
#define PIN_LED         LED_BUILTIN

// ── Timing ────────────────────────────────────────────────────────────
#define LUX_INTERVAL_MS  5000   // how often to sample and publish lux
#define MQTT_RETRY_MS    2000
#define MQTT_SOCKET_TMO  3      // seconds
#define WIFI_RETRY_MS    10000
