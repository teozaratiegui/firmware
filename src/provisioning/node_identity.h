#pragma once

#include <Arduino.h>

// -----------------------------------------------------------------------------
//  Credentials the Fog gateway issues per node, persisted in NVS so a reboot
//  does not trigger a re-registration (and does not leak a second identity).
// -----------------------------------------------------------------------------
namespace provisioning {

struct Credentials {
  String nodeId;
  String nodeKey;

  bool valid() const { return !nodeId.isEmpty() && !nodeKey.isEmpty(); }
};

/** Reads the stored credentials; returns an invalid pair when there are none. */
Credentials load();

bool store(const Credentials& credentials);

/** Wipes them — used when the gateway answers 403 and the node must re-register. */
void clear();

}  // namespace provisioning
