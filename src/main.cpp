#include <Arduino.h>
#include <memory>

#include "Cache.h"
#include "MessageGateway.h"
#include "R200.h"

#include "config/app_config.h"
#include "gateway/transport_factory.h"
#include "net/connectivity.h"
#include "rfid/heartbeat.h"
#include "rfid/rfid_hw.h"
#include "rfid/tag_processing.h"

// ── Application state ───────────────────────────────────────────────────
R200 rfid;
Cache<kTagCacheCapacity> gate(kTagCooldownMs);
TagProcessorState        tagState;

static uint32_t timeMsProvider() {
  return millis();
}

static MessageGatewayConfig gwCfg(&timeMsProvider, MQTT_NODE_ID);
static MessageGateway       msgGw(createDefaultTransport(), gwCfg);

static unsigned long lastPollTick     = 0;
static unsigned long lastLoopTick     = 0;
static unsigned long lastHeartbeatMs  = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("\nBooting ESP32-WROOM-32 + R200");

  connectivitySetup();

#if SYSTEM_MODE == SYSTEM_MODE_RFID
  setupR200Module(rfid);
#elif SYSTEM_MODE == SYSTEM_MODE_GATEWAY_INTERVAL
  Serial.println("SYSTEM_MODE_GATEWAY_INTERVAL — lector R200 no inicializado (solo telemetría por gateway).");
#else
#error "SYSTEM_MODE must be SYSTEM_MODE_RFID or SYSTEM_MODE_GATEWAY_INTERVAL"
#endif

#if MESSAGE_GATEWAY
#if GATEWAY_USE_MQTT
  Serial.println("[GW] Transport: MQTT — publishes to …/<NODE_ID>/requests (see app_config kMqttHost).");
#else
  Serial.println("[GW] Transport: HTTPS Lambda.");
#endif
  msgGw.begin();
#endif

#if SYSTEM_MODE == SYSTEM_MODE_RFID
  Serial.println("Listo. TAG_LOG = tag aceptado por caché (nuevo o tras cooldown).");
#else
  Serial.println("Listo. Telemetría por MESSAGE_GATEWAY cada intervalo de heartbeat (sin RFID).");
#endif
}

void loop() {
  connectivityLoop();

#if MESSAGE_GATEWAY
  msgGw.loop();
#endif

  const unsigned long now = millis();

  if (now - lastHeartbeatMs >= kHeartbeatIntervalMs) {
    lastHeartbeatMs = now;
#if MESSAGE_GATEWAY
    logHeartbeat(rfid, now, &msgGw);
#else
    logHeartbeat(rfid, now, nullptr);
#endif
  }

#if SYSTEM_MODE == SYSTEM_MODE_RFID
  rfid.loop();

  if ((now - lastPollTick >= kPollIntervalMs) && !rfid.dataAvailable()) {
    rfid.poll();
    lastPollTick = now;
  }

  if (now - lastLoopTick < kMainLoopIntervalMs) return;
  lastLoopTick = now;

  tagProcessorLoop(rfid, gate, msgGw, tagState, now);
#endif
}
