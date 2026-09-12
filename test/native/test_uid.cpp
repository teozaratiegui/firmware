#include "rfid/uid_utils.h"
#include "test_support.h"

void testUid() {
  SECTION("uid_utils: a UID renders as upper-case hex, zero-padded");
  {
    const uint8_t uid[UID_LEN] = {0xE2, 0x80, 0x06, 0x90, 0x00, 0x00,
                                  0x50, 0x0E, 0x88, 0xC6, 0xA4, 0xA7};
    CHECK(toUidString(uid) == "E28006900000500E88C6A4A7");
  }

  SECTION("uid_utils: an all-zero UID means no tag in the field");
  {
    const uint8_t empty[UID_LEN] = {0};
    uint8_t       present[UID_LEN] = {0};
    present[UID_LEN - 1] = 0x01;
    CHECK(isZeroUid(empty));
    CHECK(!isZeroUid(present));
  }

  SECTION("uid_utils: sameUid compares the full length");
  {
    uint8_t a[UID_LEN] = {0};
    uint8_t b[UID_LEN] = {0};
    a[UID_LEN - 1] = 0x07;
    CHECK(!sameUid(a, b));
    copyUid(b, a);
    CHECK(sameUid(a, b));
  }
}
