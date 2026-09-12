// Registration handshake: the attempt budget, what renews it, and what happens
// to credentials restored from NVS.
#include "test_support.h"

#include <memory>

#include "MessageGateway.h"
#include "fake_transport.h"
#include "provisioning/node_registrar.h"
#include "provisioning_stubs.h"

namespace {

using provisioning::NodeRegistrar;

constexpr const char kRegisterTopic[] = "gw/register";
constexpr uint8_t    kMaxAttempts     = 5;
constexpr uint32_t   kTimeoutMs       = 4000;

provisioning::Credentials stored(const char* nodeId, const char* nodeKey) {
  provisioning::Credentials credentials;
  credentials.nodeId  = nodeId;
  credentials.nodeKey = nodeKey;
  return credentials;
}

struct Rig {
  FakeTransport*                  transport;
  std::unique_ptr<MessageGateway> gateway;

  Rig() {
    testing::resetProvisioningStubs();
    g_millis = 1000;

    MessageGateway::Config cfg;
    cfg.nodePrefix    = "esp";
    cfg.gatewayPrefix = "gw";
    cfg.mac           = testing::fakeMac;
    cfg.firmware      = "test";

    std::unique_ptr<FakeTransport> owned(new FakeTransport());
    transport = owned.get();
    gateway.reset(new MessageGateway(std::move(owned), cfg));
  }

  /** Runs the superloop for `iterations` ticks of `stepMs` simulated time. */
  void spin(NodeRegistrar& registrar, int iterations, uint32_t stepMs = 1000) {
    for (int i = 0; i < iterations; ++i) {
      g_millis += stepMs;
      gateway->loop();
      registrar.loop();
    }
  }
};

}  // namespace

void testRegistrar() {
  g_serialMuted = true;

  SECTION("Registrar: the attempt budget is spent once and does not renew by itself");
  {
    Rig           rig;
    NodeRegistrar registrar(*rig.gateway, "wrong-key", kMaxAttempts, kTimeoutMs,
                            /*revalidateOnBoot=*/false);
    rig.gateway->begin();
    registrar.begin();

    // Nobody ever answers — the gateway drops a bad api_key silently (G20).
    rig.spin(registrar, 300);

    CHECK(registrar.state() == NodeRegistrar::State::GaveUp);
    CHECK(rig.transport->countPublishedOn(kRegisterTopic) == kMaxAttempts);
  }

  SECTION("Registrar: a reconnect renews the budget exactly once");
  {
    Rig           rig;
    NodeRegistrar registrar(*rig.gateway, "wrong-key", kMaxAttempts, kTimeoutMs,
                            /*revalidateOnBoot=*/false);
    rig.gateway->begin();
    registrar.begin();
    rig.spin(registrar, 300);
    CHECK(rig.transport->countPublishedOn(kRegisterTopic) == kMaxAttempts);

    // Link drops and comes back: that, and only that, is a fresh chance.
    rig.transport->setConnected(false);
    rig.spin(registrar, 2);
    rig.transport->setConnected(true);
    rig.spin(registrar, 300);

    CHECK(registrar.state() == NodeRegistrar::State::GaveUp);
    CHECK(rig.transport->countPublishedOn(kRegisterTopic) == 2 * kMaxAttempts);
  }

  SECTION("Registrar: a valid response adopts and persists the identity");
  {
    Rig           rig;
    NodeRegistrar registrar(*rig.gateway, "good-key", kMaxAttempts, kTimeoutMs,
                            /*revalidateOnBoot=*/false);
    rig.gateway->begin();
    registrar.begin();
    rig.spin(registrar, 1);

    rig.transport->deliver("gw/register/response/AA:BB:CC:DD:EE:FF",
                           "{\"node_id\":\"node-1a2b3c4d\",\"node_key\":\"deadbeef\"}");

    CHECK(registrar.state() == NodeRegistrar::State::Registered);
    CHECK(rig.gateway->nodeId() == "node-1a2b3c4d");
    CHECK(testing::fakeNvs.stored.nodeId == "node-1a2b3c4d");
    CHECK(rig.transport->uplinkTopic == "esp/node-1a2b3c4d/requests");
  }

  SECTION("Registrar: stored credentials are revalidated once per boot");
  {
    Rig rig;
    testing::fakeNvs.stored = stored("node-old", "keyold");
    NodeRegistrar registrar(*rig.gateway, "good-key", kMaxAttempts, kTimeoutMs,
                            /*revalidateOnBoot=*/true);
    registrar.begin();
    rig.gateway->begin();

    // Operational from the first iteration: revalidation is opportunistic and
    // must never be a precondition for relaying a tag.
    CHECK(registrar.state() == NodeRegistrar::State::Registered);
    CHECK(rig.gateway->nodeId() == "node-old");

    rig.spin(registrar, 5);
    CHECK(rig.transport->countPublishedOn(kRegisterTopic) == 1);

    // register_node is idempotent by MAC, so the gateway hands back whatever it
    // holds — here a fresh key, because its nodes table was rebuilt (finding G4).
    rig.transport->deliver("gw/register/response/AA:BB:CC:DD:EE:FF",
                           "{\"node_id\":\"node-new\",\"node_key\":\"keynew\"}");
    CHECK(rig.gateway->nodeId() == "node-new");
    CHECK(testing::fakeNvs.stored.nodeKey == "keynew");
  }

  SECTION("Registrar: no answer to the revalidation leaves the stored identity working");
  {
    Rig rig;
    testing::fakeNvs.stored = stored("node-old", "keyold");
    NodeRegistrar registrar(*rig.gateway, "good-key", kMaxAttempts, kTimeoutMs,
                            /*revalidateOnBoot=*/true);
    registrar.begin();
    rig.gateway->begin();
    rig.spin(registrar, 300);

    CHECK(registrar.state() == NodeRegistrar::State::Registered);
    CHECK(rig.gateway->nodeId() == "node-old");
    CHECK(rig.gateway->hasGatewayCredentials());
    // Exactly one: revalidation is a one-shot, not a heartbeat.
    CHECK(rig.transport->countPublishedOn(kRegisterTopic) == 1);
  }

  SECTION("Registrar: revalidateOnBoot = false keeps the stored identity untouched");
  {
    Rig rig;
    testing::fakeNvs.stored = stored("node-old", "keyold");
    NodeRegistrar registrar(*rig.gateway, "good-key", kMaxAttempts, kTimeoutMs,
                            /*revalidateOnBoot=*/false);
    registrar.begin();
    rig.gateway->begin();
    rig.spin(registrar, 300);

    CHECK(rig.transport->countPublishedOn(kRegisterTopic) == 0);
    CHECK(rig.gateway->nodeId() == "node-old");
  }

  SECTION("Registrar: an identity NVS refused to store is reported, not swallowed");
  {
    Rig rig;
    testing::fakeNvs.writable = false;
    NodeRegistrar registrar(*rig.gateway, "good-key", kMaxAttempts, kTimeoutMs,
                            /*revalidateOnBoot=*/false);
    rig.gateway->begin();
    registrar.begin();
    rig.spin(registrar, 1);
    rig.transport->deliver("gw/register/response/AA:BB:CC:DD:EE:FF",
                           "{\"node_id\":\"node-9\",\"node_key\":\"k9\"}");

    // The node works — it has the credentials in RAM — but it will re-register
    // on the next boot and leave another orphan in the gateway's node table, so
    // the state has to say so somewhere the Fog can see it.
    CHECK(registrar.state() == NodeRegistrar::State::Registered);
    CHECK(rig.gateway->nodeId() == "node-9");
    CHECK(testing::fakeNvs.writes == 0);
    CHECK(String(registrar.stateName()) == "registered_unpersisted");
  }

  SECTION("Registrar: a 403 reset cancels a revalidation that had not fired yet");
  {
    Rig rig;
    testing::fakeNvs.stored = stored("node-old", "keyold");
    NodeRegistrar registrar(*rig.gateway, "good-key", kMaxAttempts, kTimeoutMs,
                            /*revalidateOnBoot=*/true);
    registrar.begin();
    // The link never comes up, so the pending revalidation never publishes.
    rig.transport->connectOnBegin = false;
    rig.gateway->begin();
    registrar.reset();
    CHECK(testing::fakeNvs.clears == 1);

    rig.transport->setConnected(true);
    rig.spin(registrar, 2);
    // Exactly one message on the register topic: the fresh registration. The
    // stale revalidation must not ride along behind it.
    CHECK(rig.transport->countPublishedOn(kRegisterTopic) == 1);
    CHECK(registrar.attempts() == 1);
  }

  SECTION("Registrar: the state is reportable as a stable name");
  {
    Rig           rig;
    NodeRegistrar registrar(*rig.gateway, "wrong-key", kMaxAttempts, kTimeoutMs,
                            /*revalidateOnBoot=*/false);
    rig.gateway->begin();
    registrar.begin();
    CHECK(String(registrar.stateName()) == "unregistered");
    CHECK(registrar.attempts() == 0);

    rig.spin(registrar, 1);
    CHECK(String(registrar.stateName()) == "awaiting_response");
    CHECK(registrar.attempts() == 1);

    rig.spin(registrar, 300);
    CHECK(String(registrar.stateName()) == "gave_up");
    CHECK(registrar.attempts() == kMaxAttempts);
  }

  g_serialMuted = false;
}
