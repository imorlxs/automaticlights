#pragma once

// ── WiFi ─────────────────────────────────────────────────────────────
#define WIFI_SSID      "S25 de Isaac"
#define WIFI_PASSWORD  "IsaacMorales"

// ── MQTT (Raspberry Pi local IP) ─────────────────────────────────────
#define MQTT_BROKER    "10.103.27.40"
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

// Photoresistor (LDR) — wired as voltage divider on the only ADC pin.
//   VCC → LDR → A0 → 10kΩ resistor → GND
// Publishes raw ADC value 0–1023 (0 = dark, 1023 = bright).
// Set LUX_TH in the RPi config.py to match (e.g. 600 means "room is bright enough").

// Built-in LED mirrors the light state (active LOW on most ESP8266 boards)
#define PIN_LED         LED_BUILTIN

// ── Timing ────────────────────────────────────────────────────────────
#define LUX_INTERVAL_MS  500   // how often to sample and publish lux
#define MQTT_RETRY_MS    2000
#define MQTT_SOCKET_TMO  3      // seconds
#define WIFI_RETRY_MS    1000
