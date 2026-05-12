#pragma once

#include <Arduino.h>

#ifndef UID_LEN
#define UID_LEN 12
#endif

#ifndef R200_LINK_TEST
#define R200_LINK_TEST 1
#endif

#ifndef USE_CONTINUOUS_POLL
#define USE_CONTINUOUS_POLL 0
#endif

#ifndef MESSAGE_GATEWAY
#define MESSAGE_GATEWAY 1
#endif

// 0 = HTTPS Lambda only · 1 = MQTT — independent of SYSTEM_MODE_RFID / SYSTEM_MODE_GATEWAY_INTERVAL
#ifndef GATEWAY_USE_MQTT
#define GATEWAY_USE_MQTT 1
#endif

// ── R200 UART (Serial2) ──────────────────────────────────────────────
#define R200_RX_PIN 17
#define R200_TX_PIN 16
#define R200_BAUD   115200

// ── Timing ────────────────────────────────────────────────────────────
static const uint32_t kPollIntervalMs     = 350;
static const uint32_t kMainLoopIntervalMs   = 60;
static const uint32_t kTagCooldownMs        = 5000;
static const uint32_t kGarbageLogIntervalMs = 5000;
static const uint32_t kCacheSkipLogIntervalMs = 3000;
static const uint32_t kHeartbeatIntervalMs  = 5000;

static constexpr uint8_t kTagCacheCapacity = 16;

#include "config/mqtt_node_config.h"

// ── Gateway: MQTT (used when GATEWAY_USE_MQTT is 1) ─────────────────────
// Topics: publish → kMqttTopicPrefix "<node_id>/requests", subscribe → ".../<node_id>/responses"
// <node_id> = MQTT_NODE_ID (scripts/mqtt_node_env.py + env NODE_ID, or edit mqtt_node_config.h)
static constexpr const char kMqttTopicPrefix[] = "bicicletero/esp/";
// LAN IP of the PC running Mosquitto/Docker — same subnet as the ESP32 (see ipconfig on "Wi‑Fi 2" or your active adapter).
// Example: PC 192.168.49.28 + ESP32 192.168.49.x → use 192.168.49.28 here (not localhost). Update if DHCP changes your PC IP.
static constexpr const char kMqttHost[]        = "192.168.49.28";
static constexpr uint16_t   kMqttPort          = 1883;
static constexpr const char kMqttClientId[]    = "esp32-r200";
static constexpr bool       kMqttRetain        = false;
// Leave empty for anonymous broker; set here or prefer secrets for shared repos
static constexpr const char kMqttUser[]        = "";
static constexpr const char kMqttPass[]        = "";

// ── Run mode (what you are building) ───────────────────────────────────
// Values (do not change these lines):
#define SYSTEM_MODE_RFID 0                 // R200 on: tags → MESSAGE_GATEWAY when cache accepts
#define SYSTEM_MODE_GATEWAY_INTERVAL 1    // R200 off: only timed telemetry → MESSAGE_GATEWAY
//
// Pick behaviour: keep exactly one of the two `#define SYSTEM_MODE ...` lines below active.
// (Or skip this block entirely and pass from platformio.ini: build_flags = '-DSYSTEM_MODE=1'
//  which is the same value as SYSTEM_MODE_GATEWAY_INTERVAL.)
#ifndef SYSTEM_MODE
//#define SYSTEM_MODE SYSTEM_MODE_RFID
#define SYSTEM_MODE SYSTEM_MODE_GATEWAY_INTERVAL
#endif

#if SYSTEM_MODE == SYSTEM_MODE_GATEWAY_INTERVAL && !MESSAGE_GATEWAY
#error "SYSTEM_MODE_GATEWAY_INTERVAL requires MESSAGE_GATEWAY=1 (same ingest path as tags)."
#endif
