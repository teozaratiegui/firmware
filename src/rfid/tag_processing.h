#pragma once

#include <Arduino.h>

#include "Cache.h"
#include "MessageGateway.h"
#include "R200.h"
#include "config/app_config.h"

namespace rfid {

// -----------------------------------------------------------------------------
//  Turns raw UIDs coming off the reader into gateway traffic.
//
//  A tag that simply stays in the field would otherwise be relayed on every
//  scan, so the per-UID cooldown debounces it. Frame-level garbage is already
//  rejected by the driver's checksum, so this layer only has to debounce. The
//  skip path is rate-limited: a tag left on the antenna must not flood the
//  serial console.
// -----------------------------------------------------------------------------
class TagProcessor {
 public:
  TagProcessor(R200& reader, MessageGateway& gateway);

  void loop(uint32_t nowMs);

  bool     tagPresent() const { return tagPresent_; }
  uint32_t accepted() const { return accepted_; }

 private:
  R200&           reader_;
  MessageGateway& gateway_;
  // The UID length is the reader's, not a configurable one — see R200::kEpcLength.
  Cache<kTagCacheCapacity, R200::kEpcLength> cache_;
  uint32_t                                   lastSkipLogMs_ = 0;
  uint32_t                                   accepted_      = 0;
  bool                                       tagPresent_    = false;
};

}  // namespace rfid
