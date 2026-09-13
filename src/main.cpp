// =============================================================================
//  Edge node — ESP32 + R200 UHF RFID reader.
//
//  Composition root and superloop. Every collaborator is built here and handed
//  its dependencies; none of them reaches for a global. The loop does four
//  things in order: keep the network up, keep the uplink and the registration
//  handshake moving, drive the reader, and report.
//
//  Flow of one tag read:
//      R200 -> TagProcessor (garbage filter + per-UID cooldown)
//           -> MessageGateway -> <node prefix>/<node_id>/requests
//           -> Fog gateway -> Cloud
//           -> <node prefix>/<node_id>/responses -> AccessIndicator
// =============================================================================
#include <Arduino.h>

#include <memory>

#include "Cache.h"
#include "MessageGateway.h"
#include "R200.h"

#include "app/access_indicator.h"
#include "app/telemetry_reporter.h"
#include "config/app_config.h"
#include "gateway/transport_factory.h"
#include "net/connectivity.h"
#include "provisioning/node_registrar.h"
#include "rfid/rfid_hw.h"
#include "rfid/tag_processing.h"
#include "secrets.h"

namespace {

#if SYSTEM_MODE == SYSTEM_MODE_RFID
constexpr const char kModeLabel[] = "rfid";
#else
constexpr const char kModeLabel[] = "interval";
#endif

uint32_t millisProvider() {
  return millis();
}

MessageGateway::Config makeGatewayConfig() {
  MessageGateway::Config config;
  config.nodePrefix        = kNodePrefix;
  config.gatewayPrefix     = kGatewayPrefix;
  config.mac               = net::macAddress();
  config.firmware          = FIRMWARE_VERSION;
  config.responseTimeoutMs = kResponseTimeoutMs;
  // MESSAGE_GATEWAY=0 is serial-only bring-up, and app_config.h promises it
  // "disables the uplink entirely". Storing reads for an uplink that is never
  // opened is not disabling it: the outbox filled on the 17th tag and the
  // console started printing "[GW] outbox full — oldest tag read dropped", an
  // error that is not an error, in the one mode used to diagnose the hardware.
  config.outboxCapacity    = MESSAGE_GATEWAY ? kOutboxCapacity : 0;
  config.unansweredReadRetries = kUnansweredReadRetries;
  config.millisFn          = &millisProvider;
  config.isoTimeFn         = &net::isoTimestamp;
  config.epochMsFn         = &net::epochMillis;
  return config;
}

// Everything the node owns, built once Wi-Fi (and therefore the MAC) is up.
struct Application {
  R200                        reader;
  MessageGateway              gateway;
  provisioning::NodeRegistrar registrar;
  app::AccessIndicator        indicator;
  app::TelemetryReporter      telemetry;
  rfid::TagProcessor          processor;

  Application()
      : gateway(createConfiguredTransport(), makeGatewayConfig()),
        registrar(gateway, NODE_API_KEY, kRegisterMaxAttempts, kRegisterTimeoutMs,
                  kRevalidateIdentityOnBoot),
        indicator(kAccessGrantedPin, kAccessDeniedPin, kAccessDegradedPin, kAccessPulseMs),
        telemetry(gateway, registrar, kTelemetryIntervalMs, kModeLabel, FIRMWARE_VERSION),
        processor(reader, gateway) {}
};

std::unique_ptr<Application> node;

#if SYSTEM_MODE == SYSTEM_MODE_RFID
uint32_t lastPollMs = 0;
uint32_t lastScanMs = 0;
#endif

void wireResponseHandling() {
  node->gateway.setResponseHandler([](const GatewayResponse& response, uint32_t latencyMs) {
    node->indicator.apply(response, latencyMs);
    // 403 means the gateway does not know these credentials any more (its node
    // table was wiped, or the node was revoked). Anything else is a business
    // outcome and must not throw the identity away.
    if (response.status == 403) {
      Serial.println("[APP] credentials rejected — clearing identity and re-registering.");
      node->registrar.reset();
    }
  });
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.printf("\n=== Edge node %s — mode=%s transport=%s ===\n", FIRMWARE_VERSION, kModeLabel,
                GATEWAY_USE_MQTT ? "MQTT (Fog gateway)" : "HTTPS (direct to Lambda)");

  net::begin();

  node = std::unique_ptr<Application>(new Application());
  wireResponseHandling();
  node->indicator.begin();

#if SYSTEM_MODE == SYSTEM_MODE_RFID
  rfid::setupReader(node->reader);
#else
  Serial.println("[APP] interval mode — R200 not initialised, telemetry only.");
#endif

#if MESSAGE_GATEWAY
  // Identity first, transport second. The last will travels inside the CONNECT
  // packet and cannot be corrected afterwards, so a node holding credentials in
  // NVS has to restore them before the broker connection is opened — otherwise
  // its testament says node_id:"" even though it knew its name all along.
#if GATEWAY_USE_MQTT
  // Restores NVS credentials if there are any; otherwise arms the handshake,
  // which the superloop drives once the link is up.
  node->registrar.begin();
#else
  // The direct-to-Lambda path has no registration handshake: it authenticates
  // with the Cloud API key and labels itself with the build-time node id.
  node->gateway.setIdentity(DIRECT_NODE_ID, "");
#endif
  node->gateway.begin();
#else
  Serial.println("[APP] MESSAGE_GATEWAY=0 — uplink disabled, serial output only.");
#endif

  Serial.println("[APP] ready.");
}

void loop() {
  net::loop();
  if (!node) return;

#if MESSAGE_GATEWAY
  node->gateway.loop();
#if GATEWAY_USE_MQTT
  node->registrar.loop();
#endif
#endif
  node->indicator.loop();

#if SYSTEM_MODE == SYSTEM_MODE_RFID
  const uint32_t now = millis();
  node->reader.loop();

  if (now - lastPollMs >= kPollIntervalMs && !node->reader.dataAvailable()) {
    lastPollMs = now;
    node->reader.poll();
  }

  if (now - lastScanMs >= kMainLoopIntervalMs) {
    lastScanMs = now;
    node->processor.loop(now);
  }
#endif

  node->telemetry.loop(node->processor.tagPresent(), node->processor.accepted());
}
