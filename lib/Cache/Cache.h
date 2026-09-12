#pragma once
#include <Arduino.h>
#include <string.h>

// -----------------------------------------------------------------------------
//  Per-UID debounce for tag reads.
//
//  A tag that simply stays inside the antenna field answers every inventory
//  round, so without this every poll would become an event upstream. A UID is
//  accepted the first time it is seen and then again only once its own cooldown
//  has elapsed — per UID, not globally, so a second tag is never held back by
//  the first one.
//
//  Storage is fixed and static: CAPACITY entries of UID_BYTES bytes, ~324 B for
//  the production Cache<16, 12>. No heap, no TTL. An entry only leaves when it
//  is evicted to make room, so with more than CAPACITY tags in play entries
//  thrash and a tag can be relayed inside its cooldown.
//
//  UID_BYTES is a template parameter and not a macro on purpose, and the UID
//  arrives as a reference to an array of exactly that length. Until v0.2 the
//  length was a UID_LEN macro that this header and src/config/app_config.h each
//  declared behind their own #ifndef, so the include order decided the value:
//  building with -DUID_LEN=16 compiled clean and made `remember` memcpy 16
//  bytes out of the reader's 12-byte array. The length now comes from the one
//  place that knows it (R200::kEpcLength) and a mismatch is a build error.
// -----------------------------------------------------------------------------
template <uint8_t CAPACITY, uint8_t UID_BYTES>
class Cache {
  static_assert(CAPACITY > 0, "a cache with no entries would accept every read");
  static_assert(UID_BYTES > 0, "a zero-length UID cannot identify a tag");

 public:
  using Uid = uint8_t[UID_BYTES];

  explicit Cache(uint32_t cooldownMs) : cooldownMs_(cooldownMs) {
    clear();
  }

  /** True when the read should be relayed: a new UID, or one past its cooldown. */
  bool shouldAccept(const Uid& uid, uint32_t nowMs) {
    int idx = find(uid);
    if (idx < 0) {
      remember(uid, nowMs);
      return true;
    }
    // Unsigned subtraction, so the elapsed time is still right across the
    // millis() wrap at ~49.7 days of uptime — which is well inside the
    // unattended life of a node bolted to a door. Covered by a test, because
    // the documentation has claimed it since v0.1.
    if (nowMs - entries_[idx].lastAcceptedAt >= cooldownMs_) {
      entries_[idx].lastAcceptedAt = nowMs;
      return true;
    }
    return false;  // still inside the cooldown
  }

  void clear() {
    for (uint8_t i = 0; i < CAPACITY; ++i) {
      entries_[i].valid          = false;
      entries_[i].lastAcceptedAt = 0;
      memset(entries_[i].uid, 0, UID_BYTES);
    }
  }

 private:
  struct Entry {
    bool     valid;
    uint8_t  uid[UID_BYTES];
    uint32_t lastAcceptedAt;
  };

  Entry    entries_[CAPACITY];
  uint32_t cooldownMs_;

  int find(const Uid& uid) const {
    for (uint8_t i = 0; i < CAPACITY; ++i) {
      if (entries_[i].valid && memcmp(entries_[i].uid, uid, UID_BYTES) == 0) return i;
    }
    return -1;
  }

  void remember(const Uid& uid, uint32_t nowMs) {
    for (uint8_t i = 0; i < CAPACITY; ++i) {
      if (!entries_[i].valid) {
        entries_[i].valid = true;
        memcpy(entries_[i].uid, uid, UID_BYTES);
        entries_[i].lastAcceptedAt = nowMs;
        return;
      }
    }
    // Full: drop the least recently accepted entry. This comparison is on
    // absolute timestamps and is *not* wrap-safe, unlike the cooldown above.
    // Past the millis() wrap, entries stamped just before it hold near-maximal
    // values and stay the maximum for another whole ~49.7-day cycle, so every
    // eviction in that window picks the newest entry rather than the oldest.
    // Each stale entry heals the first time its own tag is read again: find()
    // still matches it and the unsigned subtraction above sees a long-elapsed
    // cooldown, which refreshes the stamp. So the window closes once every
    // cached tag has been seen once more, not after a single eviction.
    // The cost is duplicate events upstream, never a missed read, and only on
    // a node up for 49.7 days with more than CAPACITY tags in play — which is
    // already the documented limitation of a cache this size. Left as is.
    uint8_t  oldest   = 0;
    uint32_t oldestTs = entries_[0].lastAcceptedAt;
    for (uint8_t i = 1; i < CAPACITY; ++i) {
      if (entries_[i].lastAcceptedAt < oldestTs) {
        oldest   = i;
        oldestTs = entries_[i].lastAcceptedAt;
      }
    }
    entries_[oldest].valid = true;
    memcpy(entries_[oldest].uid, uid, UID_BYTES);
    entries_[oldest].lastAcceptedAt = nowMs;
  }
};
