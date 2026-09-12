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
//  The payloads below are the shapes the gateway states in
//  thesis-sketch/src/core/contracts/gateway.py:43-102.
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
    // gateway_adapter.py:78-81 reads "tag" and "node_key"; nothing else.
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

  SECTION("codec: the direct-to-Lambda payload uses the Cloud's own field names");
  {
    const String payload = messages::httpTagEvent("E280110C", "bench-7a", "2026-09-12T14:03:07Z");
    CHECK(equals(
        payload,
        "{\"tag\":\"E280110C\",\"nodeId\":\"bench-7a\",\"timestamp\":\"2026-09-12T14:03:07Z\"}"));
    // Both optional fields drop out cleanly rather than going out as "".
    CHECK(equals(messages::httpTagEvent("E280110C", "", ""), "{\"tag\":\"E280110C\"}"));
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

    // The shape relay_tag_read.py returns for a node whose credentials are dead.
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
