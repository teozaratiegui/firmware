// MessageGateway: what the node states on the wire across a reconnection, and
// what happens to a tag read the gateway never answers.
#include "test_support.h"

#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "HttpTransport.h"
#include "MessageGateway.h"
#include "fake_transport.h"

namespace {

constexpr const char kPresenceTopic[] = "esp/AA:BB:CC:DD:EE:FF/status";
constexpr const char kRequestsTopic[] = "esp/node-1/requests";
constexpr const char kResponsesTopic[] = "esp/node-1/responses";

// One wall clock, the way the node has one: on the device `ts` and the event id
// are both rendered from the same `time()`, so a test that moves this moves
// both. Keeping them on separate test clocks would have hidden the very thing
// the "a queued read is dated when it was taken" section checks.
//
// It moves independently of millis() because that is exactly the condition a
// read has to survive: the retry leaves later than the read did, and neither
// half of its identity may notice.
constexpr uint64_t kBaseEpochMs = 1789300802000ULL;  // 2026-09-13T12:00:02Z
uint64_t           g_epochMs    = kBaseEpochMs;

uint64_t fakeEpochMs() {
  return g_epochMs;
}

// Counts how many times the node asks for the wall clock, which is how the
// double serialisation of a tag read shows up from the outside: two calls a few
// milliseconds apart can straddle a second boundary, so the payload logged to
// serial was not necessarily the payload that went out.
unsigned g_isoCalls = 0;

String countingIso() {
  g_isoCalls++;
  if (g_epochMs == 0) return String();  // same "no clock, no timestamp" policy as net::
  const std::time_t secs = static_cast<std::time_t>(g_epochMs / 1000);
  std::tm utc{};
  gmtime_r(&secs, &utc);
  char buffer[24];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return String(buffer);
}

/** The "event_id":"..." / "eventId":"..." value in a payload, or "" if absent. */
std::string eventIdOf(const String& payload) {
  for (const char* key : {"\"event_id\":\"", "\"eventId\":\""}) {
    const std::size_t at = payload.s_.find(key);
    if (at == std::string::npos) continue;
    const std::size_t from = at + std::string(key).size();
    return payload.s_.substr(from, payload.s_.find('"', from) - from);
  }
  return std::string();
}

struct Rig {
  FakeTransport*                  transport;
  std::unique_ptr<MessageGateway> gateway;

  explicit Rig(uint8_t outboxCapacity = 4, uint8_t unansweredRetries = 2,
               TransportKind kind = TransportKind::Mqtt) {
    g_millis  = 1000;
    g_epochMs = kBaseEpochMs;

    MessageGateway::Config cfg;
    cfg.nodePrefix        = "esp";
    cfg.gatewayPrefix     = "gw";
    cfg.mac               = "AA:BB:CC:DD:EE:FF";
    cfg.firmware          = "test";
    cfg.responseTimeoutMs = 8000;
    cfg.outboxCapacity    = outboxCapacity;
    cfg.outboxRetryMs     = 2000;
    cfg.unansweredReadRetries = unansweredRetries;
    cfg.isoTimeFn             = &countingIso;
    cfg.epochMsFn             = &fakeEpochMs;

    std::unique_ptr<FakeTransport> owned(new FakeTransport(kind));
    transport = owned.get();
    gateway.reset(new MessageGateway(std::move(owned), cfg));
  }

  void spin(int iterations, uint32_t stepMs = 1000) {
    for (int i = 0; i < iterations; ++i) {
      g_millis += stepMs;
      gateway->loop();
    }
  }

  /** Payload of the last message published on `topic`, or "" if there was none. */
  String lastPayloadOn(const char* topic) const {
    for (size_t i = transport->publications.size(); i > 0; --i) {
      if (transport->publications[i - 1].topic == topic) return transport->publications[i - 1].payload;
    }
    return String();
  }

  /** Drops the link and brings it back, the way a broker restart looks. */
  void bounceLink() {
    transport->setConnected(false);
    spin(2);
    transport->setConnected(true);
    spin(2);
  }
};

bool contains(const String& haystack, const char* needle) {
  return haystack.s_.find(needle) != std::string::npos;
}

}  // namespace

void testGateway() {
  g_serialMuted = true;

  SECTION("Gateway: a reconnect counts as a new link generation");
  {
    Rig rig;
    rig.gateway->begin();
    CHECK(rig.gateway->linkGeneration() == 1);
    rig.spin(5);
    CHECK(rig.gateway->linkGeneration() == 1);  // still up, not a new link
    rig.bounceLink();
    CHECK(rig.gateway->linkGeneration() == 2);
  }

  SECTION("Gateway: presence is re-announced online after every reconnect");
  {
    Rig rig;
    rig.gateway->begin();
    CHECK(rig.transport->countPublishedOn(kPresenceTopic) == 1);
    CHECK(contains(rig.lastPayloadOn(kPresenceTopic), "\"online\":true"));
    CHECK(rig.transport->publications.empty() || rig.transport->publications.back().retain);

    rig.bounceLink();

    // Without this the retained document left by the last will says the node is
    // offline for good, which is exactly the signal the Fog is going to consume.
    CHECK(rig.transport->countPublishedOn(kPresenceTopic) == 2);
    CHECK(contains(rig.lastPayloadOn(kPresenceTopic), "\"online\":true"));
  }

  SECTION("Gateway: the response subscription is re-stated after a reconnect");
  {
    Rig rig;
    rig.gateway->setIdentity("node-1", "key");
    rig.gateway->begin();
    CHECK(rig.transport->uplinkTopic == kRequestsTopic);
    const int before = rig.transport->countSubscribed(kResponsesTopic);
    rig.bounceLink();
    CHECK(rig.transport->countSubscribed(kResponsesTopic) == before + 1);
  }

  SECTION("Gateway: an unanswered read is retried, then abandoned once");
  {
    Rig rig;
    rig.gateway->setIdentity("node-1", "key");
    rig.gateway->begin();

    CHECK(rig.gateway->sendTagRead("E2001122"));
    CHECK(rig.transport->uplinks.size() == 1);

    // The broker is up and the PUBLISH succeeded, so the outbox never saw this
    // read — but the gateway container is down and nobody answers. Before A3 the
    // read was simply dropped here.
    rig.spin(60);

    CHECK(rig.transport->uplinks.size() == 3);  // first try + two retries
    CHECK(rig.gateway->pendingCount() == 0);
    CHECK(rig.gateway->stats().readsAbandoned == 1);
    CHECK(rig.gateway->stats().responsesLost == 3);
  }

  SECTION("Gateway: an answered read is not retried");
  {
    Rig rig;
    rig.gateway->setIdentity("node-1", "key");
    rig.gateway->begin();

    CHECK(rig.gateway->sendTagRead("E2001122"));
    rig.spin(2);
    rig.transport->deliver(kResponsesTopic, "{\"status\":200}");
    rig.spin(60);

    CHECK(rig.transport->uplinks.size() == 1);
    CHECK(rig.gateway->pendingCount() == 0);
    CHECK(rig.gateway->stats().readsAbandoned == 0);
    CHECK(rig.gateway->stats().responsesLost == 0);
    CHECK(rig.gateway->stats().responses == 1);
  }

  SECTION("Gateway: unansweredReadRetries = 0 keeps the old drop-on-timeout policy");
  {
    Rig rig(4, 0);
    rig.gateway->setIdentity("node-1", "key");
    rig.gateway->begin();

    CHECK(rig.gateway->sendTagRead("E2001122"));
    rig.spin(60);

    CHECK(rig.transport->uplinks.size() == 1);
    CHECK(rig.gateway->pendingCount() == 0);
    CHECK(rig.gateway->stats().readsAbandoned == 1);
  }

  SECTION("Gateway: an unregistered node still reports telemetry, keyed by MAC");
  {
    Rig rig;
    rig.gateway->begin();
    CHECK(rig.gateway->telemetryTopic() == "esp/AA:BB:CC:DD:EE:FF/telemetry");

    NodeTelemetry telemetry;
    telemetry.mac      = "AA:BB:CC:DD:EE:FF";
    telemetry.firmware = "test";
    telemetry.mode     = "rfid";
    CHECK(rig.gateway->sendTelemetry(telemetry));
    CHECK(rig.transport->countPublishedOn("esp/AA:BB:CC:DD:EE:FF/telemetry") == 1);
    // Never onto the presence topic: that one is retained and is the last will.
    CHECK(rig.transport->countPublishedOn(kPresenceTopic) == 1);

    rig.gateway->setIdentity("node-1", "key");
    CHECK(rig.gateway->telemetryTopic() == "esp/node-1/telemetry");
    CHECK(rig.gateway->sendTelemetry(telemetry));
    CHECK(rig.transport->countPublishedOn("esp/node-1/telemetry") == 1);
  }

  SECTION("Gateway: telemetry carries the provisioning state and the abandoned count");
  {
    Rig rig;
    rig.gateway->begin();

    NodeTelemetry telemetry;
    telemetry.mac                  = "AA:BB:CC:DD:EE:FF";
    telemetry.firmware             = "test";
    telemetry.mode                 = "rfid";
    telemetry.provisioning         = "gave_up";
    telemetry.registerAttempts     = 5;
    telemetry.readsAbandoned       = 2;
    CHECK(rig.gateway->sendTelemetry(telemetry));

    const String payload = rig.lastPayloadOn("esp/AA:BB:CC:DD:EE:FF/telemetry");
    CHECK(contains(payload, "\"provisioning\":\"gave_up\""));
    CHECK(contains(payload, "\"register_attempts\":5"));
    CHECK(contains(payload, "\"reads_abandoned\":2"));
  }

  SECTION("Gateway: the last will names the node when the identity is known");
  {
    Rig rig;
    // A node booting with credentials in NVS knows its id before it connects,
    // and the will travels inside the CONNECT packet.
    rig.gateway->setIdentity("node-1", "key");
    rig.gateway->begin();
    CHECK(rig.transport->willTopic == kPresenceTopic);
    CHECK(contains(rig.transport->willPayload, "\"node_id\":\"node-1\""));
    CHECK(contains(rig.transport->willPayload, "\"online\":false"));
    CHECK(rig.transport->willRetain);
  }

  SECTION("Gateway: the will is re-stated when the identity arrives later");
  {
    Rig rig;
    rig.gateway->begin();
    CHECK(contains(rig.transport->willPayload, "\"node_id\":\"\""));

    // setLastWill only takes effect on the next CONNECT, so the will has to be
    // refreshed while the node is still up, not at disconnect time.
    rig.gateway->setIdentity("node-1", "key");
    CHECK(contains(rig.transport->willPayload, "\"node_id\":\"node-1\""));
  }

  SECTION("Gateway: the will is refreshed before a reconnect, and dropped on 403");
  {
    Rig rig;
    rig.gateway->setIdentity("node-1", "key");
    rig.gateway->begin();
    const unsigned before = rig.transport->wills;

    rig.transport->setConnected(false);
    rig.spin(2);
    CHECK(rig.transport->wills > before);
    CHECK(contains(rig.transport->willPayload, "\"node_id\":\"node-1\""));

    rig.transport->setConnected(true);
    rig.spin(2);
    rig.gateway->clearIdentity();
    CHECK(contains(rig.transport->willPayload, "\"node_id\":\"\""));
  }

  SECTION("Gateway: a re-registration takes the old response subscription with it");
  {
    Rig rig;
    rig.gateway->setIdentity("node-1", "key");
    rig.gateway->begin();
    CHECK(rig.transport->countSubscribed(kResponsesTopic) >= 1);

    // With clean_session=false an abandoned subscription outlives the node: the
    // broker keeps delivering on it and handleMessage logs every one of them as
    // an unrouted message.
    rig.gateway->setIdentity("node-2", "key2");
    CHECK(rig.transport->didUnsubscribe(kResponsesTopic));
    CHECK(rig.transport->countSubscribed("esp/node-2/responses") >= 1);

    rig.gateway->clearIdentity();
    CHECK(rig.transport->didUnsubscribe("esp/node-2/responses"));
    CHECK(rig.transport->uplinkTopic.isEmpty());
  }

  SECTION("Gateway: setting the same identity again does not unsubscribe anything");
  {
    Rig rig;
    rig.gateway->setIdentity("node-1", "key");
    rig.gateway->begin();
    rig.gateway->setIdentity("node-1", "key");
    CHECK(rig.transport->unsubscribed.empty());
  }

  SECTION("Gateway: the payload handed back is the one that went out, built once");
  {
    Rig rig;
    rig.gateway->setIdentity("node-1", "key");
    rig.gateway->begin();
    g_isoCalls = 0;

    String published;
    CHECK(rig.gateway->sendTagRead("E2001122", &published));
    CHECK(g_isoCalls == 1);
    CHECK(rig.transport->uplinks.size() == 1);
    CHECK(published == rig.transport->uplinks[0].payload);
    CHECK(contains(published, "\"tag\":\"E2001122\""));
    CHECK(contains(published, "\"node_key\":\"key\""));
    CHECK(contains(published, "\"ts\":\"2026-09-13T12:00:02Z\""));
  }

  SECTION("Gateway: a queued read hands back no payload, because none went out");
  {
    Rig rig;
    rig.gateway->setIdentity("node-1", "key");
    rig.gateway->begin();
    rig.transport->setConnected(false);
    rig.spin(1);

    String published = "stale";
    CHECK(!rig.gateway->sendTagRead("E2001122", &published));
    CHECK(published.isEmpty());
    CHECK(rig.gateway->pendingCount() == 1);
  }

  // ── One read on the wire at a time ─────────────────────────────────────────
  // The gateway's answer carries no tag, so the in-flight slot is the only thing
  // pairing a response with the read that caused it. Everything below exists
  // because the outbox used to drain a backlog in consecutive superloop
  // iterations, overwriting that slot on every send.

  SECTION("Gateway: a backlog drains one read at a time, each waiting for its answer");
  {
    Rig rig(8);
    rig.gateway->setIdentity("node-1", "key");
    rig.gateway->begin();
    rig.transport->setConnected(false);
    rig.spin(1);

    rig.gateway->sendTagRead("AAA");
    rig.gateway->sendTagRead("BBB");
    rig.gateway->sendTagRead("CCC");
    CHECK(rig.gateway->pendingCount() == 3);

    rig.transport->setConnected(true);
    rig.spin(6, 5);
    // Only the first one goes out: the other two wait for its answer instead of
    // displacing it. The whole backlog used to be published here in one burst.
    CHECK(rig.transport->uplinks.size() == 1);
    CHECK(contains(rig.transport->uplinks[0].payload, "\"tag\":\"AAA\""));
    CHECK(rig.gateway->pendingCount() == 2);

    rig.transport->deliver(kResponsesTopic, "{\"status\":200}");
    rig.spin(2, 5);
    CHECK(rig.transport->uplinks.size() == 2);
    CHECK(contains(rig.transport->uplinks[1].payload, "\"tag\":\"BBB\""));

    rig.transport->deliver(kResponsesTopic, "{\"status\":200}");
    rig.spin(2, 5);
    CHECK(rig.transport->uplinks.size() == 3);
    CHECK(contains(rig.transport->uplinks[2].payload, "\"tag\":\"CCC\""));
    CHECK(rig.gateway->pendingCount() == 0);
  }

  SECTION("Gateway: every read in a backlog is timed, not just the last one");
  {
    Rig rig(8);
    rig.gateway->setIdentity("node-1", "key");
    rig.gateway->begin();

    std::vector<uint32_t> latencies;
    rig.gateway->setResponseHandler(
        [&latencies](const GatewayResponse&, uint32_t ms) { latencies.push_back(ms); });

    rig.transport->setConnected(false);
    rig.spin(1);
    rig.gateway->sendTagRead("AAA");
    rig.gateway->sendTagRead("BBB");
    rig.transport->setConnected(true);

    // Each read is answered 50 ms after it goes out. The reported round trip has
    // to be 50 ms for both; the burst drain reported one real number and a zero,
    // because the second answer found the slot already emptied by the first.
    for (int i = 0; i < 2; ++i) {
      rig.spin(3, 5);
      g_millis += 50;
      rig.transport->deliver(kResponsesTopic, "{\"status\":200}");
    }
    CHECK(latencies.size() == 2);
    CHECK(latencies.size() == 2 && latencies[0] >= 50 && latencies[0] < 80);
    CHECK(latencies.size() == 2 && latencies[1] >= 50 && latencies[1] < 80);
    CHECK(rig.gateway->stats().lastLatencyMs >= 50);
  }

  SECTION("Gateway: an unanswered backlog retries every read, not only the last");
  {
    Rig rig(8);
    rig.gateway->setIdentity("node-1", "key");
    rig.gateway->begin();
    rig.transport->setConnected(false);
    rig.spin(1);
    rig.gateway->sendTagRead("AAA");
    rig.gateway->sendTagRead("BBB");
    rig.transport->setConnected(true);

    // Nobody ever answers. Both reads must burn their own retry budget and both
    // must be counted; the burst drain retried only the last one and lost the
    // other without incrementing a single counter.
    rig.spin(200, 1000);
    CHECK(rig.gateway->stats().readsAbandoned == 2);
    CHECK(rig.gateway->pendingCount() == 0);

    unsigned a = 0, b = 0;
    for (const FakeTransport::Publication& p : rig.transport->uplinks) {
      if (contains(p.payload, "\"tag\":\"AAA\"")) a++;
      if (contains(p.payload, "\"tag\":\"BBB\"")) b++;
    }
    // One first attempt plus unansweredReadRetries, for each read.
    CHECK(a == 3);
    CHECK(b == 3);
  }

  SECTION("Gateway: a second tag does not displace one still awaiting its answer");
  {
    Rig rig(8);
    rig.gateway->setIdentity("node-1", "key");
    rig.gateway->begin();

    CHECK(rig.gateway->sendTagRead("AAA"));
    // A different UID, so the per-UID cooldown upstream does not space them out.
    CHECK(!rig.gateway->sendTagRead("BBB"));
    CHECK(rig.transport->uplinks.size() == 1);
    CHECK(rig.gateway->pendingCount() == 1);

    rig.transport->deliver(kResponsesTopic, "{\"status\":200}");
    rig.spin(2, 5);
    CHECK(rig.transport->uplinks.size() == 2);
    CHECK(contains(rig.transport->uplinks[1].payload, "\"tag\":\"BBB\""));
  }

  SECTION("Gateway: a retried read keeps the event id it was born with");
  {
    // The point of the id is that the Cloud can tell "this read again" from
    // "another read". If the retry minted a fresh one, the conditional write in
    // the events table would let it through and one physical read would end up
    // as three rows — which is the duplicate count the experiment is trying to
    // measure in the first place.
    Rig rig(4, 2, TransportKind::Http);
    rig.gateway->setIdentity("bench-7a", "");
    rig.gateway->begin();
    rig.transport->setUplinkStatus(0);  // nobody answers

    CHECK(rig.gateway->sendTagRead("E2001122"));
    g_epochMs += 9000;  // the retries leave nine seconds later
    rig.spin(60);

    CHECK(rig.transport->uplinks.size() == 3);
    const std::string first = eventIdOf(rig.transport->uplinks[0].payload);
    CHECK(!first.empty());
    CHECK(eventIdOf(rig.transport->uplinks[1].payload) == first);
    CHECK(eventIdOf(rig.transport->uplinks[2].payload) == first);
  }

  SECTION("Gateway: two reads are two events, and they sort in order");
  {
    Rig rig(4, 0, TransportKind::Http);
    rig.gateway->setIdentity("bench-7a", "");
    rig.gateway->begin();

    CHECK(rig.gateway->sendTagRead("E2001122"));
    g_epochMs += 5000;
    CHECK(rig.gateway->sendTagRead("E2003344"));

    const std::string first  = eventIdOf(rig.transport->uplinks[0].payload);
    const std::string second = eventIdOf(rig.transport->uplinks[1].payload);
    CHECK(first != second);
    // Lexicographic order is chronological order: that is what the 13-digit
    // zero-padded prefix buys, and what the Cloud's range query relies on.
    CHECK(first < second);
    // Two reads inside one second are still two events, still in order.
    CHECK(rig.gateway->sendTagRead("E2005566"));
    CHECK(eventIdOf(rig.transport->uplinks[2].payload) > second);
  }

  SECTION("Gateway: a queued read is dated when it was taken, not when it got through");
  {
    // `ts` has to be pinned to the read exactly as the event id is. It used to
    // be rebuilt inside buildTagPayload, so every retransmission re-stamped it
    // and a read held in the outbox reached the Cloud claiming it happened when
    // it was last retried. scan-tag.js stores `clientTimestamp` verbatim from
    // whichever attempt the conditional write lets in, so the row would have
    // carried the retry time — and the reads that go through the outbox are the
    // whole population the outage experiment is about.
    Rig rig(4, 2, TransportKind::Http);
    rig.gateway->setIdentity("bench-7a", "");
    rig.gateway->begin();
    rig.transport->setUplinkStatus(0);  // nobody answers

    CHECK(rig.gateway->sendTagRead("E2001122"));
    g_epochMs += 8000;  // the retries leave eight and sixteen seconds later
    rig.spin(60);

    CHECK(rig.transport->uplinks.size() == 3);
    CHECK(contains(rig.transport->uplinks[0].payload, "\"timestamp\":\"2026-09-13T12:00:02Z\""));
    CHECK(contains(rig.transport->uplinks[1].payload, "\"timestamp\":\"2026-09-13T12:00:02Z\""));
    CHECK(contains(rig.transport->uplinks[2].payload, "\"timestamp\":\"2026-09-13T12:00:02Z\""));
    // And a read taken *after* the clock moved is dated then, so the pinning is
    // per read and not a clock that stopped.
    rig.transport->setUplinkStatus(200);
    CHECK(rig.gateway->sendTagRead("E2003344"));
    CHECK(contains(rig.transport->uplinks.back().payload,
                   "\"timestamp\":\"2026-09-13T12:00:10Z\""));
  }

  SECTION("Gateway: the event id sorts in order past the tenth read of a second");
  {
    // The sequence is zero padded, and this is why: the sort key is compared as
    // text, so "…-10" filed before "…-9" would put a tag's tenth read of that
    // second ahead of its ninth for good. Two reads of one tag inside a second
    // need a cache eviction to happen at all, but the id has to be orderable
    // whether or not that is rare.
    Rig rig(4, 0, TransportKind::Http);
    rig.gateway->setIdentity("bench-7a", "");
    rig.gateway->begin();

    std::string previous;
    for (int i = 0; i < 12; ++i) {
      char tag[16];
      snprintf(tag, sizeof(tag), "E200%04d", i);
      CHECK(rig.gateway->sendTagRead(tag));
      rig.transport->deliver("esp/node-1/responses", "{\"status\":200}");
      const std::string current = eventIdOf(rig.transport->uplinks.back().payload);
      CHECK(current > previous);  // strictly increasing, every step, same second
      previous = current;
    }
  }

  SECTION("Gateway: the MQTT arm names its reads too");
  {
    // The other sections drive the direct arm because it answers synchronously.
    // This is the productive path, and buildTagPayload picks the field name off
    // the transport, so the gateway's spelling needs its own assertion.
    Rig rig(4, 0, TransportKind::Mqtt);
    rig.gateway->setIdentity("node-1", "key");
    rig.gateway->begin();

    String published;
    CHECK(rig.gateway->sendTagRead("E2001122", &published));
    CHECK(contains(published, "\"event_id\":\""));
    CHECK(!contains(published, "\"eventId\""));  // that spelling belongs to the Lambda
    CHECK(eventIdOf(published) == eventIdOf(rig.transport->uplinks[0].payload));
  }

  SECTION("Gateway: no clock means no event id, not an invented one");
  {
    // Same policy as `ts`. A node that booted before the AP came up has no wall
    // clock, and an id prefixed with a fabricated epoch would sort its events
    // into the wrong place in the tag's history for good.
    Rig rig(4, 0, TransportKind::Http);
    rig.gateway->setIdentity("bench-7a", "");
    rig.gateway->begin();
    g_epochMs = 0;

    CHECK(rig.gateway->sendTagRead("E2001122"));
    CHECK(eventIdOf(rig.transport->uplinks[0].payload).empty());
    // No clock, no timestamp either — one policy, not two.
    CHECK(!contains(rig.transport->uplinks[0].payload, "timestamp"));
  }

  SECTION("Gateway: outboxCapacity = 0 disables store-and-forward safely");
  {
    Rig rig(0);
    rig.gateway->setIdentity("node-1", "key");
    rig.gateway->begin();
    rig.transport->setConnected(false);
    rig.spin(1);

    CHECK(!rig.gateway->sendTagRead("AAA"));
    CHECK(!rig.gateway->sendTagRead("BBB"));
    CHECK(rig.gateway->pendingCount() == 0);
    CHECK(rig.gateway->stats().tagReadsDropped == 2);

    rig.transport->setConnected(true);
    rig.spin(5);
    CHECK(rig.transport->uplinks.empty());
  }

  SECTION("Gateway: hasGatewayCredentials is about the node_key, not the node id");
  {
    Rig rig;
    rig.gateway->begin();
    CHECK(!rig.gateway->hasGatewayCredentials());
    // The direct-to-Lambda path labels itself with a node id and no node_key.
    rig.gateway->setIdentity("001", "");
    CHECK(!rig.gateway->hasGatewayCredentials());
    rig.gateway->setIdentity("node-1", "key");
    CHECK(rig.gateway->hasGatewayCredentials());
  }

  // ── The direct-to-Lambda arm ───────────────────────────────────────────────
  //
  // The A/B bench the thesis leans on has two arms and only one of them used to
  // be instrumented: over HTTP nothing ever reached handleResponse, so the node
  // had no access decision and no measured round trip, and a business answer was
  // read as a failed send.

  SECTION("HttpTransport: a business answer is a delivery, a dead socket is not");
  {
    // The whole point of the split. Everything the Cloud can answer with counts
    // as delivered, including the statuses that mean "no".
    CHECK(HttpTransport::isDelivered(200));
    CHECK(HttpTransport::isDelivered(201));
    CHECK(HttpTransport::isDelivered(204));
    CHECK(HttpTransport::isDelivered(400));
    CHECK(HttpTransport::isDelivered(401));
    CHECK(HttpTransport::isDelivered(404));  // unknown tag
    CHECK(HttpTransport::isDelivered(422));  // denied tag — used to retry forever
    CHECK(HttpTransport::isDelivered(500));
    CHECK(HttpTransport::isDelivered(503));

    // HTTPClient's own error codes are negative, and 0 is "no status at all".
    CHECK(!HttpTransport::isDelivered(0));
    CHECK(!HttpTransport::isDelivered(-1));    // connection refused
    CHECK(!HttpTransport::isDelivered(-11));   // read timeout
  }

  SECTION("Gateway: over HTTP a denied tag is delivered, not retried forever");
  {
    Rig rig(4, 2, TransportKind::Http);
    rig.gateway->setIdentity("node-1", "");  // direct path: an id, no node_key
    rig.gateway->begin();
    rig.transport->setUplinkStatus(422);  // the Cloud's "tag not allowed"

    int      seen    = 0;
    int      status  = 0;
    rig.gateway->setResponseHandler([&](const GatewayResponse& r, uint32_t) {
      seen++;
      status = r.status;
    });

    CHECK(rig.gateway->sendTagRead("E2001122"));
    CHECK(rig.transport->uplinks.size() == 1);
    // A decision is not a transport failure. It used to be counted as one, put
    // back on the outbox and re-POSTed every 2 s forever — and every retry wrote
    // another event row, roughly 43k of them a day for one denied tag.
    CHECK(rig.gateway->stats().uplinkFailures == 0);
    CHECK(rig.gateway->pendingCount() == 0);
    CHECK(rig.gateway->stats().responses == 1);
    CHECK(seen == 1);
    CHECK(status == 422);

    // And it stays delivered: nothing re-sends it on later iterations.
    rig.spin(30);
    CHECK(rig.transport->uplinks.size() == 1);
  }

  SECTION("Gateway: over HTTP the round trip is measured, as on the MQTT arm");
  {
    Rig rig(4, 2, TransportKind::Http);
    rig.gateway->setIdentity("node-1", "");
    rig.gateway->begin();
    rig.transport->setUplinkStatus(200);
    rig.transport->uplinkCostMs = 65;  // the POST blocks for this long

    uint32_t reported = 0;
    rig.gateway->setResponseHandler(
        [&](const GatewayResponse&, uint32_t latencyMs) { reported = latencyMs; });

    CHECK(rig.gateway->sendTagRead("E2001122"));
    // The in-flight slot has to be armed *before* the send, because the answer
    // arrives inside it. Armed after, both of these read zero.
    CHECK(reported == 65);
    CHECK(rig.gateway->stats().lastLatencyMs == 65);
  }

  SECTION("Gateway: over HTTP a transport failure still queues the read");
  {
    Rig rig(4, 2, TransportKind::Http);
    rig.gateway->setIdentity("node-1", "");
    rig.gateway->begin();
    rig.transport->failUplink = true;
    rig.transport->setUplinkStatus(0);  // never got a status back

    CHECK(!rig.gateway->sendTagRead("E2001122"));
    CHECK(rig.gateway->stats().uplinkFailures == 1);
    CHECK(rig.gateway->pendingCount() == 1);
    CHECK(rig.gateway->stats().responses == 0);

    // The slot was armed before the send and has to be released again, or the
    // read that is already on the outbox would also be reported lost 8 s later.
    rig.spin(30);
    CHECK(rig.gateway->stats().responsesLost == 0);
  }

  SECTION("Gateway: the MQTT arm has no synchronous answer to consume");
  {
    Rig rig;
    rig.gateway->setIdentity("node-1", "key");
    rig.gateway->begin();

    CHECK(rig.gateway->sendTagRead("E2001122"));
    // The PUBLISH succeeding says nothing about the decision: that arrives later
    // on the responses topic, so the read is still in flight here.
    CHECK(rig.gateway->stats().responses == 0);
    CHECK(!rig.gateway->sendTagRead("E2003344"));  // slot busy, second read queues
    CHECK(rig.gateway->pendingCount() == 1);
  }

  g_serialMuted = false;
}
