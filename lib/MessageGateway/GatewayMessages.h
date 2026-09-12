#pragma once

#include <Arduino.h>

// -----------------------------------------------------------------------------
//  Wire contracts between the node, the Fog gateway and the Cloud Lambda.
//  Every JSON shape the node produces or consumes lives here, so a contract
//  change is a one-file change.
//
//  Fog (MQTT), node -> gateway, topic <node prefix>/<node_id>/requests:
//      {"tag":"<epc>","node_key":"<key>"[,"ts":"<iso8601>"]}
//      thesis-sketch/src/infrastructure/mqtt/gateway_adapter.py:78-81
//  Fog (MQTT), gateway -> node, topic <node prefix>/<node_id>/responses:
//      {"status":<int>[,"message":"..."][,"error":"..."]}
//      thesis-sketch/src/core/contracts/gateway.py:43-59
//  Fog (MQTT), registration on <gateway prefix>/register and
//      <gateway prefix>/register/response/<MAC>:
//      {"api_key":"...","mac":"AA:BB:.."} -> {"node_id":"..","node_key":".."}
//      thesis-sketch/src/core/contracts/gateway.py:75-102
//  Cloud (HTTPS), node -> Lambda Function URL (Fog bypassed):
//      {"tag":"<epc>","nodeId":"<id>","timestamp":"<iso8601>"}
// -----------------------------------------------------------------------------

/** Answer published by the gateway on <node prefix>/<node_id>/responses. */
struct GatewayResponse {
  int    status = 0;
  String message;
  String error;
  bool   valid = false;

  /** 200 and 204 are the two "let them through" outcomes of the contract. */
  bool accessGranted() const { return status == 200 || status == 204; }

  /** Human-readable meaning of the status, per doc/node-manual.md. */
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

/** Everything the node reports about itself on the telemetry topic. */
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
};

namespace messages {

/** Fog contract: one tag read. `iso8601` may be empty when NTP has not synced. */
String tagRead(const String& tag, const String& nodeKey, const String& iso8601);

/** Cloud contract: one tag event posted straight to the Lambda Function URL. */
String httpTagEvent(const String& tag, const String& nodeId, const String& iso8601);

/** Fog contract: registration request keyed by MAC. */
String registerRequest(const String& apiKey, const String& mac);

String telemetry(const NodeTelemetry& t);

/** Retained presence document; `online=false` is also used as the last will. */
String presence(const String& mac, const String& nodeId, bool online, const char* firmware);

}  // namespace messages
