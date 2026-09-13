// TagProcessor: the per-UID debounce between the reader and the uplink.
//
// The layer had no coverage at all, which is awkward for the one piece of policy
// that decides how many events a tag left sitting on the antenna produces. It
// needs no hardware: the driver's `uid` is the only thing it reads, and the
// gateway it writes to is the FakeTransport rig every other gateway test uses.
#include "test_support.h"

#include <memory>

#include "MessageGateway.h"
#include "R200.h"
#include "fake_transport.h"
#include "rfid/tag_processing.h"

namespace {

constexpr const char kRequestsTopic[] = "esp/node-1/requests";

struct Rig {
  R200                            reader;
  FakeTransport*                  transport;
  std::unique_ptr<MessageGateway> gateway;
  std::unique_ptr<rfid::TagProcessor> processor;

  explicit Rig(uint8_t outboxCapacity = 4) {
    g_millis = 1000;

    MessageGateway::Config cfg;
    cfg.nodePrefix     = "esp";
    cfg.gatewayPrefix  = "gw";
    cfg.mac            = "AA:BB:CC:DD:EE:FF";
    cfg.firmware       = "test";
    cfg.outboxCapacity = outboxCapacity;

    std::unique_ptr<FakeTransport> owned(new FakeTransport());
    transport = owned.get();
    gateway.reset(new MessageGateway(std::move(owned), cfg));
    gateway->setIdentity("node-1", "key");
    gateway->begin();
    processor.reset(new rfid::TagProcessor(reader, *gateway));
  }

  /** Puts a tag in the field. The driver reports all-zero when there is none. */
  void present(uint8_t marker) {
    for (uint8_t i = 0; i < R200::kEpcLength; ++i) reader.uid[i] = marker;
  }

  void clearField() {
    for (uint8_t i = 0; i < R200::kEpcLength; ++i) reader.uid[i] = 0;
  }

  /** One scan at the current clock, then the gateway's own housekeeping. */
  void scan() {
    processor->loop(g_millis);
    gateway->loop();
  }

  int relayed() const { return static_cast<int>(transport->uplinks.size()); }
};

}  // namespace

void testTagProcessing() {
  g_serialMuted = true;

  SECTION("TagProcessor: an empty field is not a read");
  {
    Rig rig;
    rig.scan();
    CHECK(!rig.processor->tagPresent());
    CHECK(rig.processor->accepted() == 0);
    CHECK(rig.relayed() == 0);
  }

  SECTION("TagProcessor: a tag sitting in the field is relayed once per cooldown");
  {
    Rig rig;
    rig.present(0xA1);

    rig.scan();
    CHECK(rig.processor->tagPresent());
    CHECK(rig.processor->accepted() == 1);
    CHECK(rig.relayed() == 1);

    // The reader re-reports the same tag on every poll — without the debounce
    // this is one event per 350 ms for as long as the bike is parked there.
    for (int i = 0; i < 20; ++i) {
      g_millis += 200;
      rig.scan();
    }
    CHECK(rig.processor->accepted() == 1);
    CHECK(rig.relayed() == 1);

    // Past the cooldown it is a genuine second read.
    g_millis += kTagCooldownMs;
    rig.scan();
    CHECK(rig.processor->accepted() == 2);
    CHECK(rig.relayed() == 2);
  }

  SECTION("TagProcessor: the cooldown is per UID, not global");
  {
    Rig rig;
    rig.present(0xA1);
    rig.scan();
    CHECK(rig.relayed() == 1);

    // A second tag arriving inside the first one's cooldown is a different asset
    // and has to get through the debounce. It does not reach the wire yet — the
    // first read is still awaiting its answer and only one may be in flight — so
    // it goes on the outbox rather than being dropped.
    rig.present(0xB2);
    g_millis += 100;
    rig.scan();
    CHECK(rig.processor->accepted() == 2);
    CHECK(rig.gateway->pendingCount() == 1);

    // Answer the first read and the second one drains on the next iteration.
    rig.transport->deliver("esp/node-1/responses", "{\"status\":200}");
    g_millis += 2000;
    rig.scan();
    CHECK(rig.relayed() == 2);
    CHECK(rig.gateway->pendingCount() == 0);

    // ...but the first tag is still inside its own window.
    rig.present(0xA1);
    g_millis += 100;
    rig.scan();
    CHECK(rig.processor->accepted() == 2);
  }

  SECTION("TagProcessor: leaving the field clears tagPresent without a read");
  {
    Rig rig;
    rig.present(0xA1);
    rig.scan();
    CHECK(rig.processor->tagPresent());

    rig.clearField();
    g_millis += 100;
    rig.scan();
    CHECK(!rig.processor->tagPresent());
    CHECK(rig.processor->accepted() == 1);
  }

  SECTION("TagProcessor: what the uplink carries is the UID as hex");
  {
    Rig rig;
    rig.present(0x0B);
    rig.scan();
    CHECK(rig.relayed() == 1);
    // Zero-padded and upper case: 0x0B is "0B", never "B".
    CHECK(rig.transport->uplinks[0].topic == kRequestsTopic);
    CHECK(rig.transport->uplinks[0].payload.s_.find("0B0B0B0B0B0B0B0B0B0B0B0B") !=
          std::string::npos);
  }

  SECTION("TagProcessor: with the outbox disabled a read is dropped, not queued");
  {
    // What serial-only bring-up (MESSAGE_GATEWAY=0) configures: the read is
    // still accepted and printed, but there is no uplink to keep it for.
    Rig rig(0);
    rig.transport->setConnected(false);
    rig.present(0xA1);
    rig.scan();

    CHECK(rig.processor->accepted() == 1);
    CHECK(rig.relayed() == 0);
    CHECK(rig.gateway->pendingCount() == 0);
    CHECK(rig.gateway->stats().tagReadsDropped == 1);
    CHECK(!rig.gateway->storeAndForwardEnabled());
  }

  SECTION("TagProcessor: with an outbox the same read is kept instead");
  {
    Rig rig(4);
    rig.transport->setConnected(false);
    rig.present(0xA1);
    rig.scan();

    CHECK(rig.processor->accepted() == 1);
    CHECK(rig.gateway->pendingCount() == 1);
    CHECK(rig.gateway->stats().tagReadsDropped == 0);
    CHECK(rig.gateway->storeAndForwardEnabled());
  }

  g_serialMuted = false;
}
