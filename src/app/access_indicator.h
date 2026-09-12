#pragma once

#include <Arduino.h>

#include "GatewayMessages.h"

namespace app {

// What the node should signal for one gateway answer.
//
// Fail-closed is the policy for access control, so only two statuses ever open
// anything. The third outcome exists because the node has to be able to say
// "the system could not decide" out loud: GatewayResponse::describe() already
// knows that 503 is not the same event as 422, and until A4 the physical output
// collapsed both into "refused". Someone standing at the door reading a red LED
// could not tell a revoked tag from a dead backend.
enum class AccessOutcome { Granted, Denied, Degraded };

/**
 * Classifies a gateway answer.
 *
 *   Granted   200, 204            the contract's two "let them through" cases
 *   Denied    404, 422            a decision *about the tag*
 *   Degraded  everything else     400/401/403 (the caller is misconfigured),
 *                                 500/503 (the backend is down), and anything
 *                                 unexpected — none of which is a verdict on
 *                                 this tag
 */
AccessOutcome outcomeFor(const GatewayResponse& response);

// -----------------------------------------------------------------------------
//  What the node does with the gateway's answer.
//
//  v0.1 printed the raw JSON and stopped there. This turns it into a decision:
//  the status is parsed, the round-trip latency is reported, and — when a GPIO
//  is configured — an output is pulsed. All pins default to -1 (disabled), so
//  the firmware still runs on a bare ESP32 with no wiring attached.
//
//  With no degraded pin wired, degradation is signalled as a short repeated
//  blink on the denied pin: still fail-closed, still one wire, but no longer
//  indistinguishable from a refused tag.
// -----------------------------------------------------------------------------
class AccessIndicator {
 public:
  AccessIndicator(int grantedPin, int deniedPin, int degradedPin, uint32_t pulseMs);

  void begin();
  /** Advances the blink pattern and clears the outputs; call every iteration. */
  void loop();

  void apply(const GatewayResponse& response, uint32_t latencyMs);

 private:
  void signal(AccessOutcome outcome);
  void drive(int pin);
  void startBlink(int pin);
  void releaseAll();

  int      grantedPin_;
  int      deniedPin_;
  int      degradedPin_;
  uint32_t pulseMs_;
  uint32_t pulseUntilMs_  = 0;
  int      blinkPin_      = -1;
  uint8_t  blinksLeft_    = 0;
  bool     blinkHigh_     = false;
  bool     active_        = false;
};

}  // namespace app
