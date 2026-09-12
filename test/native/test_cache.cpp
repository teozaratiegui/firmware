#include "Cache.h"
#include "R200.h"
#include "test_support.h"

#include <type_traits>

namespace {

// The production cache is sized by the reader, not by a preference.
constexpr uint8_t kUidBytes = R200::kEpcLength;

using TagCache = Cache<4, kUidBytes>;

const uint8_t kTagA[kUidBytes] = {0xE2, 0x80, 0x68, 0x90, 0, 0, 0x50, 0x0E, 0x88, 0xC6, 0xA4, 0xA7};
const uint8_t kTagB[kUidBytes] = {0xE2, 0x80, 0x68, 0x90, 0, 0, 0x50, 0x0E, 0x88, 0xC6, 0xA4, 0xB0};

void makeTag(uint8_t* out, uint8_t marker) {
  for (uint8_t i = 0; i < kUidBytes; ++i) out[i] = marker;
}

// Compile-time probe: can `CacheType::shouldAccept` be called with `UidType`?
// This is how the UID-length contract is asserted — the length is part of the
// type, so a buffer of the wrong length is a build error rather than an
// out-of-range read.
template <typename CacheType, typename UidType>
class AcceptsUid {
  template <typename C, typename U>
  static auto probe(int)
      -> decltype(std::declval<C&>().shouldAccept(std::declval<U&>(), 0u), std::true_type{});
  template <typename, typename>
  static std::false_type probe(...);

 public:
  static constexpr bool value = decltype(probe<CacheType, UidType>(0))::value;
};

}  // namespace

void testCache() {
  SECTION("Cache: a new UID is accepted, a repeat inside the cooldown is not");
  {
    TagCache cache(5000);
    CHECK(cache.shouldAccept(kTagA, 1000));
    CHECK(!cache.shouldAccept(kTagA, 1100));
    CHECK(!cache.shouldAccept(kTagA, 5999));
    CHECK(cache.shouldAccept(kTagA, 6000));  // exactly one cooldown later
  }

  SECTION("Cache: cooldowns are per UID, not global");
  {
    TagCache cache(5000);
    CHECK(cache.shouldAccept(kTagA, 1000));
    CHECK(cache.shouldAccept(kTagB, 1001));
    CHECK(!cache.shouldAccept(kTagA, 1002));
  }

  SECTION("Cache: a full cache evicts the least recently accepted entry");
  {
    Cache<2, kUidBytes> cache(5000);
    uint8_t first[kUidBytes], second[kUidBytes], third[kUidBytes];
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
    TagCache cache(5000);
    CHECK(cache.shouldAccept(kTagA, 1000));
    cache.clear();
    CHECK(cache.shouldAccept(kTagA, 1001));
  }

  SECTION("Cache: the cooldown holds across the millis() rollover at ~49.7 days");
  {
    // An ESP32 wraps millis() after ~49.7 days of uptime, which is well inside
    // the unattended life of a node bolted to a door. The documentation has
    // claimed the arithmetic is rollover-safe since v0.1 with nothing to show
    // for it; `nowMs - lastAcceptedAt` on uint32_t is what makes it true.
    constexpr uint32_t kJustBeforeWrap = 0xFFFFFF00u;
    TagCache           cache(5000);

    CHECK(cache.shouldAccept(kTagA, kJustBeforeWrap));
    // 0xFFFFFF00 + 4999 wraps to 0x000000E7: still 4999 ms of real elapsed time.
    CHECK(!cache.shouldAccept(kTagA, kJustBeforeWrap + 4999u));
    CHECK(cache.shouldAccept(kTagA, kJustBeforeWrap + 5000u));
    // And the entry's new timestamp is on the far side of the wrap, so the next
    // cooldown is measured from there.
    CHECK(!cache.shouldAccept(kTagA, kJustBeforeWrap + 9999u));
    CHECK(cache.shouldAccept(kTagA, kJustBeforeWrap + 10000u));
  }

  SECTION("Cache: the UID length is the one the type declares, not a global");
  {
    // Regression: UID_LEN was a macro that both Cache.h and app_config.h
    // declared behind their own #ifndef, so the include order decided the
    // value and every comparison used whichever won. A cache built for 4-byte
    // UIDs now compares 4 bytes, full stop.
    Cache<2, 4> cache(5000);
    const uint8_t a[4] = {0x01, 0x02, 0x03, 0x04};
    const uint8_t b[4] = {0x01, 0x02, 0x03, 0x05};
    CHECK(cache.shouldAccept(a, 1000));
    CHECK(!cache.shouldAccept(a, 1001));
    CHECK(cache.shouldAccept(b, 1002));
  }

  SECTION("Cache: a UID of the wrong length does not compile");
  {
    // The whole point of B1: -DUID_LEN=16 used to compile clean and make
    // Cache::remember memcpy 16 bytes out of the reader's 12-byte array. The
    // length now travels with the type, so the mismatch cannot be built.
    static_assert(AcceptsUid<Cache<2, kUidBytes>, uint8_t[kUidBytes]>::value,
                  "the reader's own EPC buffer must be accepted");
    static_assert(!AcceptsUid<Cache<2, kUidBytes>, uint8_t[kUidBytes + 4]>::value,
                  "a longer buffer must not be accepted");
    static_assert(!AcceptsUid<Cache<2, kUidBytes + 4>, uint8_t[kUidBytes]>::value,
                  "a cache built for a longer UID must not accept the reader's buffer");
    static_assert(!AcceptsUid<Cache<2, kUidBytes>, uint8_t*>::value,
                  "a bare pointer carries no length and must not be accepted");

    CHECK((AcceptsUid<Cache<2, kUidBytes>, uint8_t[kUidBytes]>::value));
    CHECK(!(AcceptsUid<Cache<2, kUidBytes>, uint8_t[kUidBytes + 4]>::value));
    CHECK(!(AcceptsUid<Cache<2, kUidBytes + 4>, uint8_t[kUidBytes]>::value));
    CHECK(!(AcceptsUid<Cache<2, kUidBytes>, uint8_t*>::value));
  }
}
