#pragma once

#include <Arduino.h>

#include "provisioning/node_identity.h"

// -----------------------------------------------------------------------------
//  Host stand-ins for the two collaborators NodeRegistrar reaches for outside
//  its own translation unit: NVS-backed credential storage and the Wi-Fi MAC.
//  The real ones need Preferences and WiFi; these are in-memory, and the test
//  controls them through `fakeNvs`.
// -----------------------------------------------------------------------------
namespace testing {

struct FakeNvs {
  provisioning::Credentials stored;
  unsigned                  writes = 0;
  unsigned                  clears = 0;
  bool                      writable = true;
};

extern FakeNvs fakeNvs;
extern String  fakeMac;

inline void resetProvisioningStubs() {
  fakeNvs = FakeNvs{};
  fakeMac = String("AA:BB:CC:DD:EE:FF");
}

}  // namespace testing
