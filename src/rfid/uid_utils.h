#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <string.h>

// -----------------------------------------------------------------------------
//  Helpers for the raw EPC the reader hands over.
//
//  Each one takes the UID as a reference to an array and reads its length from
//  the type. Until v0.2 they read a UID_LEN macro instead, which is what let
//  the length diverge: the macro was declared in two headers behind their own
//  #ifndef, so the include order picked the winner, and a helper walked twelve
//  bytes over whatever buffer it was handed. Deducing the length means the
//  buffer and the loop bound can no longer disagree, and comparing UIDs of
//  different lengths is a build error instead of a read past the end of the
//  shorter one.
//
//  The length itself belongs to the reader: R200::kEpcLength.
// -----------------------------------------------------------------------------

template <size_t N>
inline bool sameUid(const uint8_t (&a)[N], const uint8_t (&b)[N]) {
  return memcmp(a, b, N) == 0;
}

template <size_t N>
inline void copyUid(uint8_t (&dst)[N], const uint8_t (&src)[N]) {
  memcpy(dst, src, N);
}

/** All-zero is how the driver reports "no tag in the field". */
template <size_t N>
inline bool isZeroUid(const uint8_t (&uid)[N]) {
  for (size_t i = 0; i < N; ++i) {
    if (uid[i] != 0) return false;
  }
  return true;
}

template <size_t N>
inline String toUidString(const uint8_t (&uid)[N]) {
  String s;
  s.reserve(N * 2);
  for (size_t i = 0; i < N; i++) {
    if (uid[i] < 0x10) s += '0';
    s += String(uid[i], HEX);
  }
  s.toUpperCase();
  return s;
}

// There used to be a "UID starts with DD AA" filter here, guessing at frame
// markers that a mis-synced UART had decoded as an EPC. The driver now
// resynchronises on the header, reads exactly the declared length and verifies
// the checksum, so garbage cannot reach a UID any more — and the heuristic had
// become a blind spot for real tags whose EPC happens to start with DD AA.
