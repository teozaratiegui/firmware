// -----------------------------------------------------------------------------
//  Host stand-in for GatewayMessages.cpp.
//
//  The real implementation is built on ArduinoJson, which the native suite
//  deliberately does not pull in — the point of test/native/ is that it needs
//  nothing but a C++17 compiler. What is under test here is what MessageGateway
//  and NodeRegistrar *do* (which topic, retained or not, how many times, in what
//  order), not how the JSON is spelled.
//
//  The parsers below are naive but format-compatible with the real ones, so the
//  payloads the tests write look exactly like what the gateway publishes. The
//  builders emit the same keys as the real ones for the same reason.
//
//  Not covered here, on purpose: ArduinoJson's own behaviour, and the real
//  parsers' handling of malformed input beyond "no status field".
// -----------------------------------------------------------------------------
#include "GatewayMessages.h"

#include <cstdlib>
#include <string>

namespace {

/** Value of a string field, or "" when absent. Accepts the real JSON spelling. */
std::string stringField(const std::string& json, const char* key) {
  const std::string needle = std::string("\"") + key + "\"";
  const size_t      at     = json.find(needle);
  if (at == std::string::npos) return "";
  const size_t colon = json.find(':', at + needle.size());
  if (colon == std::string::npos) return "";
  const size_t open = json.find('"', colon);
  if (open == std::string::npos) return "";
  const size_t close = json.find('"', open + 1);
  if (close == std::string::npos) return "";
  return json.substr(open + 1, close - open - 1);
}

bool intField(const std::string& json, const char* key, int& out) {
  const std::string needle = std::string("\"") + key + "\"";
  const size_t      at     = json.find(needle);
  if (at == std::string::npos) return false;
  const size_t colon = json.find(':', at + needle.size());
  if (colon == std::string::npos) return false;
  size_t digit = colon + 1;
  while (digit < json.size() && (json[digit] == ' ' || json[digit] == '-')) digit++;
  if (digit >= json.size() || json[digit] < '0' || json[digit] > '9') return false;
  out = std::atoi(json.c_str() + colon + 1);
  return true;
}

String quoted(const char* key, const String& value) {
  return String("\"") + key + "\":\"" + value + "\"";
}

}  // namespace

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
  int             status = 0;
  if (!intField(json.s_, "status", status)) return out;

  out.status  = status;
  out.message = String(stringField(json.s_, "message"));
  out.error   = String(stringField(json.s_, "error"));
  out.valid   = true;
  return out;
}

RegisterCredentials RegisterCredentials::parse(const String& json) {
  RegisterCredentials out;
  const std::string   nodeId  = stringField(json.s_, "node_id");
  const std::string   nodeKey = stringField(json.s_, "node_key");
  if (nodeId.empty() || nodeKey.empty()) return out;

  out.nodeId  = String(nodeId);
  out.nodeKey = String(nodeKey);
  out.valid   = true;
  return out;
}

namespace messages {

String tagRead(const String& tag, const String& nodeKey, const String& iso8601,
               const String& eventId) {
  String out = String("{") + quoted("tag", tag) + "," + quoted("node_key", nodeKey);
  if (!iso8601.isEmpty()) out = out + "," + quoted("ts", iso8601);
  if (!eventId.isEmpty()) out = out + "," + quoted("event_id", eventId);
  return out + "}";
}

String httpTagEvent(const String& tag, const String& nodeId, const String& iso8601,
                    const String& eventId) {
  String out = String("{") + quoted("tag", tag);
  if (!nodeId.isEmpty()) out = out + "," + quoted("nodeId", nodeId);
  if (!iso8601.isEmpty()) out = out + "," + quoted("timestamp", iso8601);
  if (!eventId.isEmpty()) out = out + "," + quoted("eventId", eventId);
  return out + "}";
}

String registerRequest(const String& apiKey, const String& mac) {
  return String("{") + quoted("api_key", apiKey) + "," + quoted("mac", mac) + "}";
}

String telemetry(const NodeTelemetry& t) {
  String out = String("{") + quoted("node_id", t.nodeId) + "," + quoted("mac", t.mac) + "," +
               quoted("firmware", String(t.firmware)) + "," + quoted("mode", String(t.mode)) +
               ",\"uptime_s\":" + String(static_cast<unsigned long>(t.uptimeS)) +
               ",\"free_heap\":" + String(static_cast<unsigned long>(t.freeHeap)) +
               "," + quoted("wifi", String(t.wifiUp ? "up" : "down")) + "," +
               quoted("ip", t.ip) + ",\"rssi\":" + String(static_cast<int>(t.rssi)) +
               ",\"tag_reads\":" + String(static_cast<unsigned long>(t.tagReads)) +
               ",\"uplink_failures\":" + String(static_cast<unsigned long>(t.uplinkFailures)) +
               ",\"reads_abandoned\":" + String(static_cast<unsigned long>(t.readsAbandoned)) +
               "," + quoted("provisioning", String(t.provisioning)) +
               ",\"register_attempts\":" + String(static_cast<unsigned long>(t.registerAttempts)) +
               ",\"tags_accepted\":" + String(static_cast<unsigned long>(t.tagsAccepted)) +
               ",\"queued\":" + String(static_cast<unsigned long>(t.queued)) +
               ",\"dropped\":" + String(static_cast<unsigned long>(t.dropped)) +
               ",\"responses\":" + String(static_cast<unsigned long>(t.responses)) +
               ",\"responses_lost\":" + String(static_cast<unsigned long>(t.responsesLost)) +
               ",\"last_rtt_ms\":" + String(static_cast<unsigned long>(t.lastLatencyMs)) +
               "," + quoted("clock", String(t.clockSynced ? "ntp" : "unset"));
  return out + "}";
}

String presence(const String& mac, const String& nodeId, bool online, const char* firmware) {
  return String("{") + quoted("mac", mac) + "," + quoted("node_id", nodeId) + ",\"online\":" +
         (online ? "true" : "false") + "," + quoted("firmware", String(firmware)) + "}";
}

}  // namespace messages
