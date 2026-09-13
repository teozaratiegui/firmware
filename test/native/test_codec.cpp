// =============================================================================
//  The real wire codec, built against the real ArduinoJson.
//
//  The main suite links a hand-written stand-in for GatewayMessages.cpp
//  (gateway_codec_stub.cpp) so it needs nothing but a compiler. That stand-in
//  parses by substring search, which is close enough to drive the behavioural
//  tests but says nothing about what the firmware actually does on the wire —
//  and the wire contract with the Fog gateway is the single thing most likely to
//  break the integration.
//
//  This binary closes that gap: it compiles lib/MessageGateway/GatewayMessages.cpp
//  itself, against ArduinoJson, and asserts the exact bytes the node publishes
//  and how it reads what the gateway publishes back. It is built only when
//  ArduinoJson can be found (see run.sh), so `./test/native/run.sh` keeps
//  working on a machine that has never run PlatformIO.
//
//  The payloads below are the shapes thesis-sketch/doc/node-manual.md states
//  in its "Contrato de mensajes" section — cited by section rather than by line
//  number, because that file lives in another repository.
// =============================================================================
#include "test_support.h"

#include <string>

#include <ArduinoJson.h>

#include "GatewayMessages.h"

// This binary has its own main, so it carries the counters the harness expects.
// Everything else it needs from the Arduino core comes from arduino_stub.cpp,
// the same one the main suite links.
int g_failures = 0;
int g_checks   = 0;

namespace {

bool contains(const String& haystack, const char* needle) {
  return haystack.s_.find(needle) != std::string::npos;
}

bool equals(const String& value, const char* expected) {
  return value.s_ == expected;
}

}  // namespace

int main() {
  std::printf("Edge firmware — wire codec against real ArduinoJson\n\n");

  // ── What the node publishes ────────────────────────────────────────────────

  SECTION("codec: a tag read is exactly the shape the gateway parses");
  {
    // GatewayMqttAdapter._handle_relay reads "tag" and "node_key"; nothing else.
    const String payload = messages::tagRead("E280110C", "a1b2c3", "2026-09-12T14:03:07Z");
    CHECK(equals(payload,
                 "{\"tag\":\"E280110C\",\"node_key\":\"a1b2c3\",\"ts\":\"2026-09-12T14:03:07Z\"}"));
  }

  SECTION("codec: no clock means no ts, not a fabricated one");
  {
    // The node omits the field until NTP has answered rather than passing off a
    // millis()-relative number as wall-clock time.
    const String payload = messages::tagRead("E280110C", "a1b2c3", "");
    CHECK(equals(payload, "{\"tag\":\"E280110C\",\"node_key\":\"a1b2c3\"}"));
    CHECK(!contains(payload, "ts"));
  }

  SECTION("codec: a tag read carries the event id the Fog will need for G3");
  {
    // The gateway reads neither field today, and both travel anyway: the day it
    // forwards an idempotency key, the key is already there and already in the
    // shape the Lambda wants, so that fix stays a pass-through.
    const String payload =
        messages::tagRead("E280110C", "a1b2c3", "2026-09-12T14:03:07Z", "1789300802000#DDEEFF-41");
    CHECK(equals(payload,
                 "{\"tag\":\"E280110C\",\"node_key\":\"a1b2c3\","
                 "\"ts\":\"2026-09-12T14:03:07Z\",\"event_id\":\"1789300802000#DDEEFF-41\"}"));
    // And it is omitted, not emptied, when the clock never answered.
    CHECK(!contains(messages::tagRead("E280110C", "a1b2c3", "", ""), "event_id"));
  }

  SECTION("codec: the direct-to-Lambda payload uses the Cloud's own field names");
  {
    const String payload = messages::httpTagEvent("E280110C", "bench-7a", "2026-09-12T14:03:07Z",
                                                  "1789300802000#DDEEFF-41");
    CHECK(equals(payload,
                 "{\"tag\":\"E280110C\",\"nodeId\":\"bench-7a\","
                 "\"timestamp\":\"2026-09-12T14:03:07Z\","
                 "\"eventId\":\"1789300802000#DDEEFF-41\"}"));
    // Every optional field drops out cleanly rather than going out as "".
    CHECK(equals(messages::httpTagEvent("E280110C", "", "", ""), "{\"tag\":\"E280110C\"}"));
  }

  SECTION("codec: the event id is in the shape the Lambda can sort on");
  {
    // domain/event.js uses the id verbatim as the DynamoDB sort key when it
    // matches /^\d{13}#.+$/ — a fixed-width epoch is what makes a range query
    // over one tag mean a time range. A node id that failed this shape would
    // still deduplicate, but its events would fall out of every time window.
    const String payload =
        messages::httpTagEvent("E280110C", "bench-7a", "", "1789300802000#DDEEFF-41");
    JsonDocument doc;
    CHECK(deserializeJson(doc, payload.c_str()) == DeserializationError::Ok);
    const std::string id = doc["eventId"].as<const char*>();
    CHECK(id.size() > 14 && id[13] == '#');
    CHECK(id.find_first_not_of("0123456789") == 13);
  }

  SECTION("codec: a registration request is keyed by MAC");
  {
    const String payload = messages::registerRequest("shared-key", "AA:BB:CC:DD:EE:FF");
    CHECK(equals(payload, "{\"api_key\":\"shared-key\",\"mac\":\"AA:BB:CC:DD:EE:FF\"}"));
  }

  SECTION("codec: presence carries a JSON boolean, not the string \"false\"");
  {
    const String gone = messages::presence("AA:BB:CC:DD:EE:FF", "node-1", false, "0.2.0");
    CHECK(contains(gone, "\"online\":false"));
    CHECK(!contains(gone, "\"online\":\"false\""));
    const String here = messages::presence("AA:BB:CC:DD:EE:FF", "node-1", true, "0.2.0");
    CHECK(contains(here, "\"online\":true"));
  }

  SECTION("codec: telemetry numbers are numbers");
  {
    NodeTelemetry t;
    t.nodeId   = "node-1";
    t.mac      = "AA:BB:CC:DD:EE:FF";
    t.uptimeS  = 1234;
    t.freeHeap = 220000;
    t.rssi     = -67;
    t.wifiUp   = true;
    t.provisioning      = "registered";
    t.registerAttempts  = 3;
    const String payload = messages::telemetry(t);
    CHECK(contains(payload, "\"uptime_s\":1234"));
    CHECK(contains(payload, "\"free_heap\":220000"));
    CHECK(contains(payload, "\"rssi\":-67"));
    CHECK(contains(payload, "\"register_attempts\":3"));
    CHECK(contains(payload, "\"wifi\":\"up\""));
    CHECK(contains(payload, "\"provisioning\":\"registered\""));
  }

  SECTION("codec: telemetry carries the numbers a measurement run reads");
  {
    // These used to exist only as a Serial.printf, which put every quantity the
    // experiment needs behind a USB cable.
    //
    // Nothing consumes this message yet — the gateway has no telemetry handler
    // (finding G7), and the reduction scripts in docs/validacion/ read the
    // serial line, not this JSON. So these names are pinned here rather than by
    // a downstream parser failing, and they are deliberately the serial line's
    // names where the two overlap.
    NodeTelemetry t;
    t.nodeId        = "node-1";
    t.tagsAccepted  = 41;
    t.tagReads      = 43;   // three attempts for one retried read
    t.queued        = 2;
    t.dropped       = 1;
    t.responses     = 39;
    t.responsesLost = 2;
    t.lastLatencyMs = 184;
    t.clockSynced   = true;

    const String payload = messages::telemetry(t);
    CHECK(contains(payload, "\"tags_accepted\":41"));
    CHECK(contains(payload, "\"tag_reads\":43"));
    CHECK(contains(payload, "\"queued\":2"));
    CHECK(contains(payload, "\"dropped\":1"));
    CHECK(contains(payload, "\"responses\":39"));
    CHECK(contains(payload, "\"responses_lost\":2"));
    CHECK(contains(payload, "\"last_rtt_ms\":184"));
    CHECK(contains(payload, "\"clock\":\"ntp\""));

    t.clockSynced = false;
    CHECK(contains(messages::telemetry(t), "\"clock\":\"unset\""));
  }

  SECTION("codec: telemetry still fits in the MQTT buffer");
  {
    // PubSubClient publishes nothing at all — silently — past its buffer. What
    // it compares against that buffer is not the payload but the whole packet:
    // MQTT_MAX_HEADER_SIZE + 2 + strlen(topic) + payload. Asserting on the
    // payload alone would leave the topic's worth of bytes unguarded, which is
    // the part that grows when the topic prefix is renamed. Telemetry is the
    // largest message the node sends and it just grew by seven fields, so this
    // is the guard rail.
    //
    // MqttTransport::Config::bufferSize, and the longest topic this message can
    // go out on — the MAC form, used until the node has a node_id.
    NodeTelemetry t;
    t.nodeId       = "node-3f9a1c04";
    t.mac          = "AA:BB:CC:DD:EE:FF";
    t.ip           = "192.168.49.75";
    t.firmware     = "0.2.0";
    t.mode         = "rfid";
    t.provisioning = "registered";
    t.uptimeS = t.freeHeap = t.tagReads = t.uplinkFailures = 4294967295u;
    t.readsAbandoned = t.tagsAccepted = t.queued = t.dropped = 4294967295u;
    t.responses = t.responsesLost = t.lastLatencyMs = 4294967295u;

    constexpr unsigned kMqttBufferSize    = 1024;
    constexpr unsigned kMqttMaxHeaderSize = 5;
    const std::string  topic = "bicicletero/esp/AA:BB:CC:DD:EE:FF/telemetry";
    const unsigned     overhead =
        kMqttMaxHeaderSize + 2 + static_cast<unsigned>(topic.size());
    CHECK(messages::telemetry(t).length() + overhead < kMqttBufferSize);
  }

  SECTION("codec: a value that needs escaping is escaped, not truncated");
  {
    // No real EPC contains a quote, but a builder that concatenated strings by
    // hand would emit invalid JSON here and the gateway would drop the read.
    const String payload = messages::tagRead("A\"B\\C", "k", "");
    CHECK(contains(payload, "\\\"") && contains(payload, "\\\\"));
    // And it round-trips: what the gateway parses is the tag the node read.
    const String rebuilt = payload;
    CHECK(contains(rebuilt, "{\"tag\":\"A\\\"B\\\\C\""));
  }

  // ── What the node reads back ───────────────────────────────────────────────

  SECTION("codec: a gateway answer is parsed with its optional fields");
  {
    const GatewayResponse allowed = GatewayResponse::parse("{\"status\": 200}");
    CHECK(allowed.valid);
    CHECK(allowed.status == 200);
    CHECK(allowed.accessGranted());
    CHECK(allowed.message.isEmpty());
    CHECK(allowed.error.isEmpty());

    // The shape RelayTagRead returns for a node whose credentials are dead.
    const GatewayResponse denied =
        GatewayResponse::parse("{\"status\": 403, \"error\": \"unauthorized\"}");
    CHECK(denied.valid);
    CHECK(denied.status == 403);
    CHECK(!denied.accessGranted());
    CHECK(equals(denied.error, "unauthorized"));

    const GatewayResponse upstream = GatewayResponse::parse(
        "{\"status\": 503, \"message\": \"Backend returned unexpected status 418\", "
        "\"error\": \"upstream_error\"}");
    CHECK(upstream.valid);
    CHECK(upstream.status == 503);
    CHECK(equals(upstream.message, "Backend returned unexpected status 418"));
    CHECK(equals(upstream.error, "upstream_error"));
  }

  SECTION("codec: a message carrying escapes survives the round trip");
  {
    const GatewayResponse response =
        GatewayResponse::parse("{\"status\":422,\"message\":\"tag \\\"X1\\\" disabled\"}");
    CHECK(response.valid);
    CHECK(equals(response.message, "tag \"X1\" disabled"));
  }

  SECTION("codec: an answer the node cannot trust is rejected, not guessed at");
  {
    // Every one of these used to be indistinguishable from a real 200 to a
    // parser that scans for a substring.
    CHECK(!GatewayResponse::parse("").valid);
    CHECK(!GatewayResponse::parse("not json at all").valid);
    CHECK(!GatewayResponse::parse("{\"status\":").valid);
    CHECK(!GatewayResponse::parse("{\"error\":\"timeout\"}").valid);
    // A status that arrives as a string is a contract violation, not a 200.
    CHECK(!GatewayResponse::parse("{\"status\":\"200\"}").valid);
    // And a field that merely mentions the word is not the field.
    CHECK(!GatewayResponse::parse("{\"note\":\"status 200 was expected\"}").valid);
  }

  SECTION("codec: only 200 and 204 open the door");
  {
    // The gateway normalises the Lambda's 201 to 200 before it answers
    // (RelayTagRead, and node-manual.md § "Tabla de respuestas"), so a
    // 201 arriving here means the contract was bypassed and is not a grant.
    // 422 is the one that matters most: it is how a *refused* tag is reported
    // since the Cloud moved DENY off 403, and 403 now means the node's own
    // credentials are dead.
    GatewayResponse r;
    r.status = 200;
    CHECK(r.accessGranted());
    r.status = 204;
    CHECK(r.accessGranted());
    r.status = 201;
    CHECK(!r.accessGranted());
    for (int refused : {400, 401, 403, 404, 422, 500, 503}) {
      r.status = refused;
      CHECK(!r.accessGranted());
    }
    // A response that never parsed carries status 0 and must not grant either.
    CHECK(!GatewayResponse().accessGranted());
    CHECK(!GatewayResponse::parse("garbage").accessGranted());
  }

  SECTION("codec: describe() names every status the contract can produce");
  {
    GatewayResponse r;
    const int  known[] = {200, 204, 400, 401, 403, 404, 422, 500, 503};
    for (int status : known) {
      r.status = status;
      CHECK(std::string(r.describe()) != "unexpected status");
    }
    r.status = 418;
    CHECK(std::string(r.describe()) == "unexpected status");
  }

  SECTION("codec: credentials are adopted only when both fields are usable");
  {
    const RegisterCredentials issued =
        RegisterCredentials::parse("{\"node_id\": \"node-3f2a91bc\", \"node_key\": \"deadbeef\"}");
    CHECK(issued.valid);
    CHECK(equals(issued.nodeId, "node-3f2a91bc"));
    CHECK(equals(issued.nodeKey, "deadbeef"));

    // Half a credential is worse than none: the node would publish reads the
    // gateway answers 401/403 to, forever.
    CHECK(!RegisterCredentials::parse("{\"node_id\":\"node-1\"}").valid);
    CHECK(!RegisterCredentials::parse("{\"node_key\":\"k\"}").valid);
    CHECK(!RegisterCredentials::parse("{\"node_id\":\"\",\"node_key\":\"k\"}").valid);
    CHECK(!RegisterCredentials::parse("{\"node_id\":\"n\",\"node_key\":\"\"}").valid);
    CHECK(!RegisterCredentials::parse("{\"node_id\":1,\"node_key\":2}").valid);
    CHECK(!RegisterCredentials::parse("garbage").valid);
  }

  SECTION("codec: what the node publishes is what a JSON parser reads back");
  {
    // The loop that matters end to end: build a read the way the firmware does,
    // then read it the way the gateway does.
    const String payload = messages::tagRead("E280110C", "a1b2c3", "2026-09-12T14:03:07Z");
    JsonDocument doc;
    CHECK(deserializeJson(doc, payload.c_str()) == DeserializationError::Ok);
    CHECK(std::string(doc["tag"] | "") == "E280110C");
    CHECK(std::string(doc["node_key"] | "") == "a1b2c3");
    CHECK(std::string(doc["ts"] | "") == "2026-09-12T14:03:07Z");
  }

  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
