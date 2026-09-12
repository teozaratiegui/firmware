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
  for (uint8_t i = 0; i < 12; ++i) {
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
}
