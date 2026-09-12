#include "Cache.h"
#include "test_support.h"

namespace {

const uint8_t kTagA[UID_LEN] = {0xE2, 0x80, 0x68, 0x90, 0, 0, 0x50, 0x0E, 0x88, 0xC6, 0xA4, 0xA7};
const uint8_t kTagB[UID_LEN] = {0xE2, 0x80, 0x68, 0x90, 0, 0, 0x50, 0x0E, 0x88, 0xC6, 0xA4, 0xB0};

void makeTag(uint8_t* out, uint8_t marker) {
  for (uint8_t i = 0; i < UID_LEN; ++i) out[i] = marker;
}

}  // namespace

void testCache() {
  SECTION("Cache: a new UID is accepted, a repeat inside the cooldown is not");
  {
    Cache<4> cache(5000);
    CHECK(cache.shouldAccept(kTagA, 1000));
    CHECK(!cache.shouldAccept(kTagA, 1100));
    CHECK(!cache.shouldAccept(kTagA, 5999));
    CHECK(cache.shouldAccept(kTagA, 6000));  // exactly one cooldown later
  }

  SECTION("Cache: cooldowns are per UID, not global");
  {
    Cache<4> cache(5000);
    CHECK(cache.shouldAccept(kTagA, 1000));
    CHECK(cache.shouldAccept(kTagB, 1001));
    CHECK(!cache.shouldAccept(kTagA, 1002));
  }

  SECTION("Cache: a full cache evicts the least recently accepted entry");
  {
    Cache<2> cache(5000);
    uint8_t first[UID_LEN], second[UID_LEN], third[UID_LEN];
    makeTag(first, 0x01);
    makeTag(second, 0x02);
    makeTag(third, 0x03);

    CHECK(cache.shouldAccept(first, 1000));
    CHECK(cache.shouldAccept(second, 2000));
    CHECK(cache.shouldAccept(third, 3000));   // evicts `first`, the oldest
    CHECK(cache.shouldAccept(first, 3100));   // forgotten, so it looks new again
    CHECK(!cache.shouldAccept(third, 3200));  // still remembered
  }

  SECTION("Cache: clear() forgets everything");
  {
    Cache<4> cache(5000);
    CHECK(cache.shouldAccept(kTagA, 1000));
    cache.clear();
    CHECK(cache.shouldAccept(kTagA, 1001));
  }
}
