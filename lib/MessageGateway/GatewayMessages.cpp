#include "GatewayMessages.h"

#include <ArduinoJson.h>

const char* GatewayResponse::describe() const {
  switch (status) {
    case 200: return "access allowed";
    case 204: return "access allowed (no body)";
    case 400: return "bad payload — firmware bug";
    case 401: return "node_key missing — firmware bug";
    case 403: return "credentials rejected — re-register";
    case 404: return "tag unknown";
    case 422: return "tag disabled";
    case 500: return "backend error";
    case 503: return "gateway cannot reach the backend";
    default:  return "unexpected status";
  }
}

GatewayResponse GatewayResponse::parse(const String& json) {
  GatewayResponse out;
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return out;
  if (!doc["status"].is<int>()) return out;

  out.status  = doc["status"].as<int>();
  out.message = doc["message"].is<const char*>() ? doc["message"].as<const char*>() : "";
  out.error   = doc["error"].is<const char*>() ? doc["error"].as<const char*>() : "";
  out.valid   = true;
  return out;
}

RegisterCredentials RegisterCredentials::parse(const String& json) {
  RegisterCredentials out;
  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return out;

  const char* nodeId  = doc["node_id"];
  const char* nodeKey = doc["node_key"];
  if (!nodeId || !nodeKey || !*nodeId || !*nodeKey) return out;

  out.nodeId  = nodeId;
  out.nodeKey = nodeKey;
  out.valid   = true;
  return out;
}

namespace messages {

String tagRead(const String& tag, const String& nodeKey, const String& iso8601) {
  JsonDocument doc;
  doc["tag"]      = tag;
  doc["node_key"] = nodeKey;
  // The gateway ignores unknown fields today (it only reads tag and node_key),
  // so the client timestamp travels at no cost and is ready the day the Fog
  // forwards it upstream (gateway finding G1).
  if (!iso8601.isEmpty()) doc["ts"] = iso8601;

  String out;
  serializeJson(doc, out);
  return out;
}

String httpTagEvent(const String& tag, const String& nodeId, const String& iso8601) {
  JsonDocument doc;
  doc["tag"] = tag;
  if (!nodeId.isEmpty()) doc["nodeId"] = nodeId;
  if (!iso8601.isEmpty()) doc["timestamp"] = iso8601;

  String out;
  serializeJson(doc, out);
  return out;
}

String registerRequest(const String& apiKey, const String& mac) {
  JsonDocument doc;
  doc["api_key"] = apiKey;
  doc["mac"]     = mac;

  String out;
  serializeJson(doc, out);
  return out;
}

String telemetry(const NodeTelemetry& t) {
  JsonDocument doc;
  doc["node_id"]         = t.nodeId;
  doc["mac"]             = t.mac;
  doc["firmware"]        = t.firmware;
  doc["mode"]            = t.mode;
  doc["uptime_s"]        = t.uptimeS;
  doc["free_heap"]       = t.freeHeap;
  doc["wifi"]            = t.wifiUp ? "up" : "down";
  doc["ip"]              = t.ip;
  doc["rssi"]            = t.rssi;
  doc["tag_reads"]       = t.tagReads;
  doc["uplink_failures"] = t.uplinkFailures;
  doc["reads_abandoned"] = t.readsAbandoned;
  doc["provisioning"]      = t.provisioning;
  doc["register_attempts"] = t.registerAttempts;

  String out;
  serializeJson(doc, out);
  return out;
}

String presence(const String& mac, const String& nodeId, bool online, const char* firmware) {
  JsonDocument doc;
  doc["mac"]      = mac;
  doc["node_id"]  = nodeId;
  doc["online"]   = online;
  doc["firmware"] = firmware;

  String out;
  serializeJson(doc, out);
  return out;
}

}  // namespace messages
