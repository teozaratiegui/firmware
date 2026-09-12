#include "R200.h"
#include "test_support.h"

#include <vector>

namespace {

/** Builds a well-formed R200 frame and computes its checksum. */
std::vector<uint8_t> frame(uint8_t type, uint8_t command, const std::vector<uint8_t>& params) {
  std::vector<uint8_t> out;
  out.push_back(0xAA);
  out.push_back(type);
  out.push_back(command);
  out.push_back(static_cast<uint8_t>(params.size() >> 8));
  out.push_back(static_cast<uint8_t>(params.size() & 0xFF));
  out.insert(out.end(), params.begin(), params.end());

  uint16_t sum = 0;
  for (size_t i = 1; i < out.size(); ++i) sum += out[i];
  out.push_back(static_cast<uint8_t>(sum & 0xFF));
  out.push_back(0xDD);
  return out;
}

/** Inventory answer: RSSI, PC (2), EPC (12), CRC (2). */
std::vector<uint8_t> pollResponse(const std::vector<uint8_t>& epc) {
  std::vector<uint8_t> params{0xC7, 0x30, 0x00};
  params.insert(params.end(), epc.begin(), epc.end());
  params.push_back(0x11);
  params.push_back(0x9B);
  return frame(0x02, 0x22, params);
}

void feed(const std::vector<uint8_t>& bytes) {
  Serial2.feed(bytes.data(), bytes.size());
}

bool uidEquals(const R200& reader, const std::vector<uint8_t>& expected) {
  return std::memcmp(reader.uid, expected.data(), expected.size()) == 0;
}

bool uidIsZero(const R200& reader) {
  for (uint8_t i = 0; i < R200::kEpcLength; ++i) {
    if (reader.uid[i] != 0) return false;
  }
  return true;
}

R200 freshReader() {
  Serial2.reset();
  R200 reader;
  reader.begin(&Serial2, 115200, 16, 17);
  return reader;
}

/** The answer the reader gives to GetModuleInfo: a type byte plus ASCII text. */
std::vector<uint8_t> moduleInfoResponse(const char* text) {
  std::vector<uint8_t> params{0x00};
  for (const char* c = text; *c; ++c) params.push_back(static_cast<uint8_t>(*c));
  return frame(0x01, 0x03, params);
}

/** Everything the driver has written so far, as a frame. */
std::vector<uint8_t> sent() {
  return std::vector<uint8_t>(Serial2.tx.begin(), Serial2.tx.end());
}

// Re-derives the structure the reader's datasheet defines, so a hand-written
// command frame in the driver cannot quietly disagree with it: header, declared
// length, checksum over Type..last parameter, frame end.
bool frameIsWellFormed(const std::vector<uint8_t>& f, uint8_t expectedType, uint8_t expectedCmd) {
  if (f.size() < 7) return false;
  if (f.front() != 0xAA || f.back() != 0xDD) return false;
  if (f[1] != expectedType || f[2] != expectedCmd) return false;

  const size_t declared = static_cast<size_t>(f[3]) << 8 | f[4];
  if (declared + 7 != f.size()) return false;

  uint16_t sum = 0;
  for (size_t i = 1; i < 5 + declared; ++i) sum += f[i];
  return f[5 + declared] == static_cast<uint8_t>(sum & 0xFF);
}

}  // namespace

void testR200() {
  const std::vector<uint8_t> kEpc{0xE2, 0x80, 0x68, 0x90, 0x00, 0x00,
                                  0x50, 0x0E, 0x88, 0xC6, 0xA4, 0xA7};

  SECTION("R200: a valid inventory answer yields the EPC");
  {
    R200 reader = freshReader();
    feed(pollResponse(kEpc));
    reader.loop();
    CHECK(uidEquals(reader, kEpc));
  }

  SECTION("R200: an EPC containing the frame-end byte 0xDD is not truncated");
  {
    // Regression: v0.1 stopped reading at the first 0xDD, so this tag decoded as
    // a short, checksum-invalid frame and was silently dropped.
    const std::vector<uint8_t> epcWithFrameEnd{0xE2, 0x80, 0xDD, 0x90, 0x00, 0x00,
                                               0x50, 0x0E, 0xDD, 0xC6, 0xA4, 0xA7};
    R200 reader = freshReader();
    feed(pollResponse(epcWithFrameEnd));
    reader.loop();
    CHECK(uidEquals(reader, epcWithFrameEnd));
  }

  SECTION("R200: a frame declaring more parameters than the buffer holds is rejected");
  {
    // Regression: with paramLength = 0xFFFC the v0.1 checksum loop index wrapped
    // and never terminated. There is no watchdog configured to recover from it.
    R200 reader = freshReader();
    const std::vector<uint8_t> malicious{0xAA, 0x02, 0x22, 0xFF, 0xFC, 0x00, 0x00, 0xDD};
    feed(malicious);
    reader.loop();
    CHECK(uidIsZero(reader));
  }

  SECTION("R200: a truncated frame leaves the last EPC untouched");
  {
    R200 reader = freshReader();
    feed(pollResponse(kEpc));
    reader.loop();
    CHECK(uidEquals(reader, kEpc));

    std::vector<uint8_t> truncated = pollResponse(kEpc);
    truncated.resize(truncated.size() - 4);
    feed(truncated);
    reader.loop();
    CHECK(uidEquals(reader, kEpc));
  }

  SECTION("R200: a wrong checksum is rejected");
  {
    R200 reader = freshReader();
    std::vector<uint8_t> corrupted = pollResponse(kEpc);
    corrupted[corrupted.size() - 2] ^= 0xFF;  // break the checksum byte
    feed(corrupted);
    reader.loop();
    CHECK(uidIsZero(reader));
  }

  SECTION("R200: 'inventory fail' clears the EPC — the tag left the field");
  {
    R200 reader = freshReader();
    feed(pollResponse(kEpc));
    reader.loop();
    CHECK(uidEquals(reader, kEpc));

    feed(frame(0x01, 0xFF, {0x15}));  // ERR_InventoryFail
    reader.loop();
    CHECK(uidIsZero(reader));
  }

  SECTION("R200: leading noise before the header is discarded");
  {
    R200 reader = freshReader();
    const uint8_t noise[3] = {0x00, 0x7F, 0x13};
    Serial2.feed(noise, sizeof(noise));
    feed(pollResponse(kEpc));
    reader.loop();
    CHECK(uidEquals(reader, kEpc));
  }

  SECTION("R200: poll() emits the documented single-inventory frame");
  {
    R200 reader = freshReader();
    reader.poll();
    const std::vector<uint8_t> expected{0xAA, 0x00, 0x22, 0x00, 0x00, 0x22, 0xDD};
    CHECK(Serial2.tx.size() == expected.size());
    for (size_t i = 0; i < expected.size() && i < Serial2.tx.size(); ++i) {
      CHECK(Serial2.tx[i] == expected[i]);
    }
  }

  SECTION("R200: a multi-poll notification yields the EPC the same way");
  {
    // USE_CONTINUOUS_POLL=1 gets its tags as 0x27 notifications rather than
    // 0x22 answers. Same dispatch arm, and until now only one of the two
    // command bytes was ever exercised.
    R200 reader = freshReader();
    std::vector<uint8_t> params{0xC7, 0x30, 0x00};
    params.insert(params.end(), kEpc.begin(), kEpc.end());
    params.push_back(0x11);
    params.push_back(0x9B);
    feed(frame(0x02, 0x27, params));
    reader.loop();
    CHECK(uidEquals(reader, kEpc));
  }

  SECTION("R200: an inventory answer too short to hold an EPC and its CRC is ignored");
  {
    // A complete answer declares RSSI(1) PC(2) EPC(12) CRC(2) = 17 parameter
    // bytes. Both frames below are well-formed with a checksum that checks out
    // and are simply too short. 16 is the one that matters: the guard used to
    // ask only for the EPC, so 15 and 16 passed it and yielded a UID whose tail
    // was read out of the CRC slot.
    for (int epcBytes : {9, 11}) {  // 14 and 16 declared parameter bytes
      R200                 reader = freshReader();
      std::vector<uint8_t> params{0xC7, 0x30, 0x00};
      params.insert(params.end(), kEpc.begin(), kEpc.begin() + epcBytes);
      params.push_back(0x11);
      params.push_back(0x9B);
      const std::vector<uint8_t> truncated = frame(0x02, 0x22, params);
      CHECK(frameIsWellFormed(truncated, 0x02, 0x22));  // valid, just too short
      feed(truncated);
      reader.loop();
      CHECK(uidIsZero(reader));
    }
  }

  SECTION("R200: a module-info answer is consumed without disturbing the EPC");
  {
    R200 reader = freshReader();
    feed(pollResponse(kEpc));
    reader.loop();
    CHECK(uidEquals(reader, kEpc));

    g_serialMuted = true;
    feed(moduleInfoResponse("R200 V1.0"));
    reader.loop();
    g_serialMuted = false;
    CHECK(uidEquals(reader, kEpc));    // a different command must not touch it
    CHECK(Serial2.available() == 0);   // and the frame was read, not left behind
  }

  SECTION("R200: every command frame it emits is well-formed and checksummed");
  {
    // The four command frames are hand-written byte arrays with precomputed
    // checksums. The documentation has called them correct since v0.1; this
    // recomputes them with the same algorithm the decoder applies on the way in.
    R200 reader = freshReader();

    reader.poll();
    CHECK(frameIsWellFormed(sent(), 0x00, 0x22));  // CMD_SinglePollInstruction

    Serial2.reset();
    reader.dumpModuleInfo();
    CHECK(frameIsWellFormed(sent(), 0x00, 0x03));  // CMD_GetModuleInfo

    Serial2.reset();
    reader.setMultiplePollingMode(true);
    CHECK(frameIsWellFormed(sent(), 0x00, 0x27));  // CMD_MultiplePollInstruction
    // The inventory count is finite and nothing re-arms it — a known limitation,
    // pinned here so a change to it is deliberate.
    CHECK(Serial2.tx.size() == 10);
    CHECK(Serial2.tx[6] == 0xFF);
    CHECK(Serial2.tx[7] == 0xFF);

    Serial2.reset();
    reader.setMultiplePollingMode(false);
    CHECK(frameIsWellFormed(sent(), 0x00, 0x28));  // CMD_StopMultiplePoll
  }

  SECTION("R200: linkTest() passes when the reader answers with a valid frame");
  {
    R200 reader = freshReader();
    const std::vector<uint8_t> answer = moduleInfoResponse("R200 V1.0");
    Serial2.replyOnWrite(answer.data(), answer.size());

    g_serialMuted = true;
    const bool ok = reader.linkTest();
    g_serialMuted = false;
    CHECK(ok);
    // One probe was enough: a second attempt would have written 8 more bytes.
    CHECK(Serial2.tx.size() == 8);
    CHECK(frameIsWellFormed(sent(), 0x00, 0x03));
  }

  SECTION("R200: linkTest() gives up in bounded time when nothing answers");
  {
    // The bring-up diagnostic runs before anything else in setup(), so a reader
    // that is not wired must cost a bounded delay rather than hang the boot.
    R200 reader = freshReader();
    const unsigned long startedAt = g_millis;

    g_serialMuted = true;
    const bool ok = reader.linkTest();
    g_serialMuted = false;
    CHECK(!ok);
    CHECK(Serial2.tx.size() == 16);  // both attempts were made
    // Two 350 ms windows plus the 60 ms pause between them: ~0.8 s. The lower
    // bound earns its place — a linkTest() that gave up without waiting at all
    // would also satisfy "not forever".
    CHECK(g_millis - startedAt >= 700);
    CHECK(g_millis - startedAt < 2000);
  }

  SECTION("R200: linkTest() rejects an answer that fails its checksum");
  {
    R200 reader = freshReader();
    std::vector<uint8_t> corrupted = moduleInfoResponse("R200 V1.0");
    corrupted[corrupted.size() - 2] ^= 0xFF;
    Serial2.replyOnWrite(corrupted.data(), corrupted.size());

    g_serialMuted = true;
    const bool ok = reader.linkTest();
    g_serialMuted = false;
    CHECK(!ok);                       // framing alone is not proof of a link
    CHECK(Serial2.tx.size() == 16);   // it retried before giving up
  }
}
