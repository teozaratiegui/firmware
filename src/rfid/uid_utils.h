#pragma once

#include <Arduino.h>
#include <string.h>

#include "config/app_config.h"

inline bool sameUid(const uint8_t* a, const uint8_t* b) {
  return memcmp(a, b, UID_LEN) == 0;
}

inline void copyUid(uint8_t* dst, const uint8_t* src) {
  memcpy(dst, src, UID_LEN);
}

inline bool isZeroUid(const uint8_t* uid) {
  static const uint8_t kZero[UID_LEN] = {0};
  return sameUid(uid, kZero);
}

inline String toUidString(const uint8_t* uid) {
  String s;
  for (uint8_t i = 0; i < UID_LEN; i++) {
    if (uid[i] < 0x10) s += '0';
    s += String(uid[i], HEX);
  }
  s.toUpperCase();
  return s;
}

// R200 frames use 0xAA … 0xDD; mis-synced UART sometimes decodes that edge as "EPC".
inline bool isLikelyFramingGarbageUid(const uint8_t* uid) {
  return uid[0] == 0xDD && uid[1] == 0xAA;
}
