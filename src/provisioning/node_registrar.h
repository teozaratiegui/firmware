#pragma once

#include <Arduino.h>

#include "MessageGateway.h"
#include "provisioning/node_identity.h"

namespace provisioning {

// -----------------------------------------------------------------------------
//  Registration by MAC against the Fog gateway.
//
//      node    -> <gateway prefix>/register                 {"api_key","mac"}
//      gateway -> <gateway prefix>/register/response/<MAC>  {"node_id","node_key"}
//
//  Implemented as a non-blocking state machine driven from the superloop: the
//  node keeps reading tags while it waits for its identity, and nothing here
//  ever calls delay().
// -----------------------------------------------------------------------------
class NodeRegistrar {
 public:
  enum class State { Unregistered, AwaitingResponse, Registered, GaveUp };

  NodeRegistrar(MessageGateway& gateway, String apiKey, uint8_t maxAttempts, uint32_t timeoutMs,
                bool revalidateOnBoot);

  /**
   * Restores credentials from NVS, or arms the registration handshake.
   *
   * Call it before MessageGateway::begin(): the last will is part of the CONNECT
   * packet, so the node id has to be known before the broker connection opens.
   */
  void begin();
  void loop();

  State state() const { return state_; }
  bool  isRegistered() const { return state_ == State::Registered; }

  /**
   * Stable snake_case name of the current state, for telemetry.
   *
   * A node whose NVS write failed reports `registered_unpersisted`: it works,
   * but it will re-register on every boot and burn a new identity each time,
   * and that used to be visible only as one line on a serial console.
   */
  const char* stateName() const;
  /** Registration attempts spent in the current link generation. */
  uint8_t attempts() const { return attempts_; }

  /** Drops the stored identity and registers again (answer to a 403). */
  void reset();

 private:
  void requestRegistration();
  void publishRegistration();
  bool ensureSubscribed();
  void onRegisterResponse(const String& payload);

  MessageGateway& gateway_;
  String          apiKey_;
  uint8_t         maxAttempts_;
  uint32_t        timeoutMs_;
  bool            revalidateOnBoot_;
  State           state_          = State::Unregistered;
  uint8_t         attempts_       = 0;
  uint32_t        deadlineMs_     = 0;
  bool            subscribed_     = false;
  // False when the gateway issued credentials that NVS refused to store.
  bool            persisted_      = true;
  // One-shot: a node that booted with credentials still re-registers once, in
  // case the gateway lost its nodes table while the node was off.
  bool            revalidatePending_ = false;
  // Link generation in which the budget was exhausted. Leaving GaveUp requires
  // the gateway to report a different one, i.e. an actual reconnection.
  uint32_t        gaveUpOnLink_   = 0;
};

}  // namespace provisioning
