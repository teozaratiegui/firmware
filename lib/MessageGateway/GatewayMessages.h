#pragma once

#include <Arduino.h>

// -----------------------------------------------------------------------------
//  Wire contracts between the node, the Fog gateway and the Cloud Lambda.
//  Every JSON shape the node produces or consumes lives here, so a contract
//  change is a one-file change.
//
//  The Fog side is owned by another repository, so it is cited by the name of
//  the thing rather than by line number: line numbers in someone else's repo
//  drift silently and nobody finds out. The authority is
//  thesis-sketch/doc/node-manual.md, section "Contrato de mensajes".
//
//  Fog (MQTT), node -> gateway, topic <node prefix>/<node_id>/requests:
//      {"tag":"<epc>","node_key":"<key>"[,"ts":"<iso8601>"][,"event_id":"<id>"]}
//      node-manual.md § "Request (nodo publica)"; read by
//      GatewayMqttAdapter._handle_relay into RelayTagReadCommand, which takes
//      only `tag` and `node_key`. `ts` and `event_id` are dropped there and
//      reach nobody today: the Cloud accepts a clientTimestamp and an
//      idempotency key, but the gateway still posts `{"tag"}` alone (finding
//      G1). The node sends both anyway so that closing that gap stays a
//      gateway-only change — in particular `event_id` is already in the shape
//      the Lambda wants for `Idempotency-Key`, so the fix for finding G3 on the
//      Fog side is a pass-through rather than a decision about key formats.
//  Fog (MQTT), gateway -> node, topic <node prefix>/<node_id>/responses:
//      {"status":<int>[,"message":"..."][,"error":"..."]}
//      node-manual.md § "Response (gateway publica)" and § "Tabla de
//      respuestas"; built by NodeResponse in core/contracts/gateway.py.
//  Fog (MQTT), registration on <gateway prefix>/register and
//      <gateway prefix>/register/response/<MAC>:
//      {"api_key":"...","mac":"AA:BB:.."} -> {"node_id":"..","node_key":".."}
//      node-manual.md § "Registro del nodo"; RegisterRequest and
//      RegisterResponse in core/contracts/gateway.py.
//  Cloud (HTTPS), node -> Lambda Function URL (Fog bypassed):
//      {"tag":"<epc>","nodeId":"<id>","timestamp":"<iso8601>","eventId":"<id>"}
//      IaC-multi-tenant-system, the access_control tag-scan handler
//      (src/handlers/tag-scan.js; it also accepts node_id and ts as aliases).
// -----------------------------------------------------------------------------

/** Answer published by the gateway on <node prefix>/<node_id>/responses. */
struct GatewayResponse {
  int    status = 0;
  String message;
  String error;
  bool   valid = false;

  /** 200 and 204 are the two "let them through" outcomes of the contract. */
  bool accessGranted() const { return status == 200 || status == 204; }

  /** Human-readable meaning of the status, per node-manual.md § "Tabla de respuestas". */
  const char* describe() const;

  static GatewayResponse parse(const String& json);
};

/** Credentials the gateway hands out on <gateway prefix>/register/response/<MAC>. */
struct RegisterCredentials {
  String nodeId;
  String nodeKey;
  bool   valid = false;

  static RegisterCredentials parse(const String& json);
};

/**
 * Everything the node reports about itself on the telemetry topic.
 *
 * The measurement fields at the bottom used to exist only as a `Serial.printf`
 * in TelemetryReporter. That made every number the experiment needs — round
 * trip, queue depth, losses — reachable only over a USB cable, so a node
 * mounted on a door reported nothing measurable and a run had to be babysat
 * next to the laptop. They are counters the node already keeps; putting them on
 * the wire costs four lines and about 80 bytes per message, every 30 s.
 */
struct NodeTelemetry {
  String   nodeId;
  String   mac;
  String   ip;
  bool     wifiUp        = false;
  uint32_t uptimeS       = 0;
  uint32_t freeHeap      = 0;
  int32_t  rssi          = 0;
  uint32_t tagReads      = 0;
  uint32_t uplinkFailures = 0;
  const char* mode       = "";
  const char* firmware   = "";
  // Registration state, so a node that never got an identity is still visible
  // upstream instead of only on a serial console nobody is watching.
  const char* provisioning    = "";
  uint8_t     registerAttempts = 0;
  // Reads that went out and were never answered, after the retry budget.
  uint32_t    readsAbandoned  = 0;

  // ── What a measurement run reads ──────────────────────────────────────────
  // Distinct tags accepted by the debounce. This is "how many tags the reader
  // actually read"; `tagReads` counts transmission *attempts*, so one read that
  // is retried twice adds three to it and one to this.
  uint32_t tagsAccepted   = 0;
  // Reads waiting in the outbox right now — the depth of an outage.
  uint32_t queued         = 0;
  // Reads the outbox had to drop because it was full, or had no capacity.
  uint32_t dropped        = 0;
  // Answers parsed, and answers that never came before the timeout. A read is
  // only definitively lost when it also runs out of retries: that is
  // `readsAbandoned`.
  uint32_t responses      = 0;
  uint32_t responsesLost  = 0;
  // Round trip of the last answered read, in milliseconds. One sample, not a
  // distribution: the series is rebuilt off-node from these messages.
  uint32_t lastLatencyMs  = 0;
  // Whether NTP has answered. It decides whether `ts` and `event_id` are on the
  // wire at all, so a run that finds neither can tell a clock problem from a
  // contract problem.
  bool     clockSynced    = false;
};

namespace messages {

/**
 * Fog contract: one tag read.
 *
 * `iso8601` and `eventId` may both be empty when NTP has not synced, and then
 * they are left out of the message rather than filled with something invented.
 */
String tagRead(const String& tag, const String& nodeKey, const String& iso8601,
               const String& eventId = String());

/** Cloud contract: one tag event posted straight to the Lambda Function URL. */
String httpTagEvent(const String& tag, const String& nodeId, const String& iso8601,
                    const String& eventId = String());

/** Fog contract: registration request keyed by MAC. */
String registerRequest(const String& apiKey, const String& mac);

String telemetry(const NodeTelemetry& t);

/** Retained presence document; `online=false` is also used as the last will. */
String presence(const String& mac, const String& nodeId, bool online, const char* firmware);

}  // namespace messages
