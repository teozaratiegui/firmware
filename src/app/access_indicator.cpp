#include "app/access_indicator.h"

namespace app {
namespace {

// Degradation without a dedicated pin: short repeated pulses on the denied pin.
// Long enough to be visible, short enough that nobody reads it as the steady
// pulse of a refused tag.
constexpr uint32_t kBlinkHalfPeriodMs = 150;
constexpr uint8_t  kBlinkCount        = 3;

}  // namespace

AccessOutcome outcomeFor(const GatewayResponse& response) {
  if (response.accessGranted()) return AccessOutcome::Granted;
  // Only these two are the gateway's verdict on the tag itself. Everything else
  // — a malformed request, dead credentials, a 500, a 503 — means no decision
  // was reached, and reporting it as "refused" hides a broken system.
  if (response.status == 404 || response.status == 422) return AccessOutcome::Denied;
  return AccessOutcome::Degraded;
}

AccessIndicator::AccessIndicator(int grantedPin, int deniedPin, int degradedPin, uint32_t pulseMs)
    : grantedPin_(grantedPin),
      deniedPin_(deniedPin),
      degradedPin_(degradedPin),
      pulseMs_(pulseMs) {}

void AccessIndicator::begin() {
  const int pins[] = {grantedPin_, deniedPin_, degradedPin_};
  for (int pin : pins) {
    if (pin < 0) continue;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  if (grantedPin_ < 0 && deniedPin_ < 0 && degradedPin_ < 0) {
    Serial.println("[ACCESS] no output pin configured — decisions are serial only.");
  }
}

void AccessIndicator::loop() {
  if (blinksLeft_ > 0) {
    if (static_cast<int32_t>(millis() - pulseUntilMs_) < 0) return;
    blinkHigh_    = !blinkHigh_;
    pulseUntilMs_ = millis() + kBlinkHalfPeriodMs;
    digitalWrite(static_cast<uint8_t>(blinkPin_), blinkHigh_ ? HIGH : LOW);
    if (!blinkHigh_ && --blinksLeft_ == 0) releaseAll();
    return;
  }
  if (!active_) return;
  if (static_cast<int32_t>(millis() - pulseUntilMs_) < 0) return;
  releaseAll();
}

void AccessIndicator::apply(const GatewayResponse& response, uint32_t latencyMs) {
  Serial.printf("[ACCESS] status=%d (%s) rtt=%u ms", response.status, response.describe(),
                static_cast<unsigned>(latencyMs));
  if (!response.message.isEmpty()) {
    Serial.print(" message=");
    Serial.print(response.message);
  }
  if (!response.error.isEmpty()) {
    Serial.print(" error=");
    Serial.print(response.error);
  }
  Serial.println();

  signal(outcomeFor(response));
}

void AccessIndicator::signal(AccessOutcome outcome) {
  switch (outcome) {
    case AccessOutcome::Granted:
      drive(grantedPin_);
      return;
    case AccessOutcome::Denied:
      drive(deniedPin_);
      return;
    case AccessOutcome::Degraded:
      // A dedicated pin if there is one; otherwise blink the denied pin. Either
      // way the granted pin stays down — the door does not open on degradation.
      if (degradedPin_ >= 0) {
        drive(degradedPin_);
        return;
      }
      startBlink(deniedPin_);
      return;
  }
}

// Release first, then bail out on an unwired pin: a new decision must never
// leave the previous pulse burning. With only the granted pin wired, returning
// early would keep "come in" lit for a 503 that arrived right after a 200.
void AccessIndicator::drive(int pin) {
  releaseAll();
  if (pin < 0) return;
  digitalWrite(static_cast<uint8_t>(pin), HIGH);
  pulseUntilMs_ = millis() + pulseMs_;
  active_       = true;
}

void AccessIndicator::startBlink(int pin) {
  releaseAll();
  if (pin < 0) return;
  blinkPin_     = pin;
  blinksLeft_   = kBlinkCount;
  blinkHigh_    = true;
  pulseUntilMs_ = millis() + kBlinkHalfPeriodMs;
  digitalWrite(static_cast<uint8_t>(pin), HIGH);
}

void AccessIndicator::releaseAll() {
  const int pins[] = {grantedPin_, deniedPin_, degradedPin_};
  for (int pin : pins) {
    if (pin >= 0) digitalWrite(static_cast<uint8_t>(pin), LOW);
  }
  active_     = false;
  blinksLeft_ = 0;
  blinkHigh_  = false;
}

}  // namespace app
