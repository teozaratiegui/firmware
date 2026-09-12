#include "R200.h"
#include "rfid/uid_utils.h"
#include "test_support.h"

#include <type_traits>

namespace {

constexpr uint8_t kUidBytes = R200::kEpcLength;

// Compile-time probe: does `sameUid` accept these two buffers? Comparing UIDs
// of different lengths is a build error now, not a memcmp past the end of the
// shorter one.
template <typename A, typename B>
class ComparesUids {
  template <typename X, typename Y>
  static auto probe(int)
      -> decltype(sameUid(std::declval<X&>(), std::declval<Y&>()), std::true_type{});
  template <typename, typename>
  static std::false_type probe(...);

 public:
  static constexpr bool value = decltype(probe<A, B>(0))::value;
};

}  // namespace

void testUid() {
  SECTION("uid_utils: a UID renders as upper-case hex, zero-padded");
  {
    const uint8_t uid[kUidBytes] = {0xE2, 0x80, 0x06, 0x90, 0x00, 0x00,
                                    0x50, 0x0E, 0x88, 0xC6, 0xA4, 0xA7};
    CHECK(toUidString(uid) == "E28006900000500E88C6A4A7");
  }

  SECTION("uid_utils: an all-zero UID means no tag in the field");
  {
    const uint8_t empty[kUidBytes]   = {0};
    uint8_t       present[kUidBytes] = {0};
    present[kUidBytes - 1]           = 0x01;
    CHECK(isZeroUid(empty));
    CHECK(!isZeroUid(present));
  }

  SECTION("uid_utils: sameUid compares the full length");
  {
    uint8_t a[kUidBytes] = {0};
    uint8_t b[kUidBytes] = {0};
    a[kUidBytes - 1]     = 0x07;
    CHECK(!sameUid(a, b));
    copyUid(b, a);
    CHECK(sameUid(a, b));
  }

  SECTION("uid_utils: the length comes from the buffer, not from a macro");
  {
    // Regression: every helper used to read a UID_LEN macro, so it walked 12
    // bytes over whatever it was handed. Against a 4-byte buffer that is an
    // out-of-range read that renders 24 hex digits of stack garbage.
    const uint8_t shortUid[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    CHECK(toUidString(shortUid) == "DEADBEEF");
    CHECK(!isZeroUid(shortUid));

    const uint8_t zeros[4] = {0};
    CHECK(isZeroUid(zeros));
    CHECK(!sameUid(shortUid, zeros));
  }

  SECTION("uid_utils: comparing UIDs of different lengths does not compile");
  {
    static_assert(ComparesUids<uint8_t[kUidBytes], uint8_t[kUidBytes]>::value,
                  "two of the reader's EPC buffers must be comparable");
    static_assert(!ComparesUids<uint8_t[kUidBytes], uint8_t[4]>::value,
                  "a 12-byte UID must not be comparable with a 4-byte one");
    static_assert(!ComparesUids<uint8_t[kUidBytes], uint8_t*>::value,
                  "a bare pointer carries no length and must not be comparable");

    CHECK((ComparesUids<uint8_t[kUidBytes], uint8_t[kUidBytes]>::value));
    CHECK(!(ComparesUids<uint8_t[kUidBytes], uint8_t[4]>::value));
    CHECK(!(ComparesUids<uint8_t[kUidBytes], uint8_t*>::value));
  }

  SECTION("uid_utils: the EPC length every buffer derives from is still 12");
  {
    // R200::uid is declared `uint8_t[kEpcLength]`, so its size cannot disagree
    // with the constant and there is nothing to assert there. What is worth
    // pinning is the value: it is the EPC a Gen2 tag reports and the width of
    // every UID buffer downstream, so changing it changes the wire format.
    CHECK(R200::kEpcLength == 12);
  }
}
