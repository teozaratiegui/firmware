// What the node signals physically for each status the gateway can return.
//
// The policy under test is fail-closed: the granted pin is driven only by the
// two "let them through" statuses. What A4 adds is that the node stops saying
// "this tag was refused" when what actually happened is that the backend fell
// over.
#include "test_support.h"

#include "app/access_indicator.h"

namespace {

constexpr int      kGranted  = 4;
constexpr int      kDenied   = 5;
constexpr int      kDegraded = 6;
constexpr uint32_t kPulseMs  = 2000;

GatewayResponse answer(int status) {
  GatewayResponse response;
  response.status = status;
  response.valid  = true;
  return response;
}

}  // namespace

void testAccess() {
  g_serialMuted = true;

  SECTION("Access: the outcome distinguishes a refused tag from a broken backend");
  {
    CHECK(app::outcomeFor(answer(200)) == app::AccessOutcome::Granted);
    CHECK(app::outcomeFor(answer(204)) == app::AccessOutcome::Granted);
    // A decision about the tag.
    CHECK(app::outcomeFor(answer(404)) == app::AccessOutcome::Denied);
    CHECK(app::outcomeFor(answer(422)) == app::AccessOutcome::Denied);
    // Not a decision at all: the system could not reach one.
    CHECK(app::outcomeFor(answer(500)) == app::AccessOutcome::Degraded);
    CHECK(app::outcomeFor(answer(503)) == app::AccessOutcome::Degraded);
    CHECK(app::outcomeFor(answer(400)) == app::AccessOutcome::Degraded);
    CHECK(app::outcomeFor(answer(401)) == app::AccessOutcome::Degraded);
    CHECK(app::outcomeFor(answer(403)) == app::AccessOutcome::Degraded);
    CHECK(app::outcomeFor(answer(418)) == app::AccessOutcome::Degraded);
  }

  SECTION("Access: with a degraded pin wired, 503 drives it and not the denied pin");
  {
    resetPins();
    g_millis = 1000;
    app::AccessIndicator indicator(kGranted, kDenied, kDegraded, kPulseMs);
    indicator.begin();

    indicator.apply(answer(503), 12);
    CHECK(digitalRead(kDegraded) == HIGH);
    CHECK(digitalRead(kDenied) == LOW);
    CHECK(digitalRead(kGranted) == LOW);
  }

  SECTION("Access: a refused tag drives the denied pin, a granted one the granted pin");
  {
    resetPins();
    g_millis = 1000;
    app::AccessIndicator indicator(kGranted, kDenied, kDegraded, kPulseMs);
    indicator.begin();

    indicator.apply(answer(422), 12);
    CHECK(digitalRead(kDenied) == HIGH);
    CHECK(digitalRead(kDegraded) == LOW);

    indicator.apply(answer(200), 12);
    CHECK(digitalRead(kGranted) == HIGH);
    CHECK(digitalRead(kDenied) == LOW);
  }

  SECTION("Access: without a degraded pin, degradation blinks the denied pin");
  {
    resetPins();
    g_millis = 1000;
    app::AccessIndicator indicator(kGranted, kDenied, /*degradedPin=*/-1, kPulseMs);
    indicator.begin();

    // Fail-closed is unchanged: the granted pin stays down. What changes is that
    // the pattern is distinguishable from a steady "refused" pulse.
    indicator.apply(answer(503), 12);
    CHECK(digitalRead(kGranted) == LOW);
    CHECK(digitalRead(kDenied) == HIGH);

    int transitions = 0;
    int previous    = digitalRead(kDenied);
    for (int i = 0; i < 60; ++i) {
      g_millis += 100;
      indicator.loop();
      const int level = digitalRead(kDenied);
      if (level != previous) transitions++;
      previous = level;
    }
    CHECK(transitions >= 3);
    CHECK(digitalRead(kDenied) == LOW);  // the pattern ends, it does not latch
  }

  SECTION("Access: a new decision always clears the previous pulse");
  {
    resetPins();
    g_millis = 1000;
    // Only the granted pin is wired — the common case on a bench.
    app::AccessIndicator indicator(kGranted, /*deniedPin=*/-1, /*degradedPin=*/-1, kPulseMs);
    indicator.begin();

    indicator.apply(answer(200), 12);
    CHECK(digitalRead(kGranted) == HIGH);

    // The new outcome has no pin of its own, but leaving the granted pin lit
    // would signal "come in" for a backend that just fell over.
    indicator.apply(answer(503), 12);
    CHECK(digitalRead(kGranted) == LOW);

    indicator.apply(answer(200), 12);
    CHECK(digitalRead(kGranted) == HIGH);
    indicator.apply(answer(404), 12);
    CHECK(digitalRead(kGranted) == LOW);
  }

  SECTION("Access: the pulse is released when it expires");
  {
    resetPins();
    g_millis = 1000;
    app::AccessIndicator indicator(kGranted, kDenied, kDegraded, kPulseMs);
    indicator.begin();

    indicator.apply(answer(200), 12);
    CHECK(digitalRead(kGranted) == HIGH);
    g_millis += kPulseMs + 1;
    indicator.loop();
    CHECK(digitalRead(kGranted) == LOW);
  }

  g_serialMuted = false;
}
