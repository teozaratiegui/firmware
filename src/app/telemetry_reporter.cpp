#include "app/telemetry_reporter.h"

#include "net/connectivity.h"

namespace app {

TelemetryReporter::TelemetryReporter(MessageGateway& gateway,
                                     provisioning::NodeRegistrar& registrar, uint32_t intervalMs,
                                     const char* mode, const char* firmware)
    : gateway_(gateway),
      registrar_(registrar),
      intervalMs_(intervalMs),
      mode_(mode),
      firmware_(firmware) {}

void TelemetryReporter::loop(bool tagPresent, uint32_t tagsAccepted) {
  const uint32_t now = millis();
  if (!first_ && now - lastReportMs_ < intervalMs_) return;
  first_        = false;
  lastReportMs_ = now;
  report(tagPresent, tagsAccepted);
}

void TelemetryReporter::report(bool tagPresent, uint32_t tagsAccepted) {
  const MessageGateway::Stats& stats = gateway_.stats();

  NodeTelemetry telemetry;
  telemetry.nodeId         = gateway_.nodeId();
  telemetry.mac            = net::macAddress();
  telemetry.ip             = net::ipAddress();
  telemetry.wifiUp         = net::isConnected();
  telemetry.uptimeS        = millis() / 1000;
  telemetry.freeHeap       = ESP.getFreeHeap();
  telemetry.rssi           = net::rssi();
  telemetry.tagReads       = stats.tagReadsSent;
  telemetry.uplinkFailures = stats.uplinkFailures;
  telemetry.mode           = mode_;
  telemetry.firmware       = firmware_;
  telemetry.readsAbandoned = stats.readsAbandoned;
  telemetry.provisioning     = registrar_.stateName();
  telemetry.registerAttempts = registrar_.attempts();
  // The measurement half. Every one of these was already kept in RAM and
  // printed to a serial console; publishing them is what lets a run be measured
  // from the broker instead of from the USB cable.
  telemetry.tagsAccepted   = tagsAccepted;
  telemetry.queued         = gateway_.pendingCount();
  telemetry.dropped        = stats.tagReadsDropped;
  telemetry.responses      = stats.responses;
  telemetry.responsesLost  = stats.responsesLost;
  telemetry.lastLatencyMs  = stats.lastLatencyMs;
  telemetry.clockSynced    = net::clockSynced();

  // Built from the same struct that goes on the wire, field by field, so the
  // console and the broker cannot drift apart. They used to be two independent
  // argument lists and the serial one carried four numbers the message did not.
  Serial.printf(
      "[TELEMETRY] up=%us wifi=%s ip=%s rssi=%d heap=%u node=%s tag_present=%d accepted=%u "
      "sent=%u queued=%u dropped=%u responses=%u lost=%u abandoned=%u prov=%s/%u "
      "last_rtt=%ums clock=%s\n",
      static_cast<unsigned>(telemetry.uptimeS), telemetry.wifiUp ? "up" : "down",
      telemetry.ip.isEmpty() ? "-" : telemetry.ip.c_str(), static_cast<int>(telemetry.rssi),
      static_cast<unsigned>(telemetry.freeHeap),
      telemetry.nodeId.isEmpty() ? "unregistered" : telemetry.nodeId.c_str(), tagPresent ? 1 : 0,
      static_cast<unsigned>(telemetry.tagsAccepted), static_cast<unsigned>(telemetry.tagReads),
      static_cast<unsigned>(telemetry.queued), static_cast<unsigned>(telemetry.dropped),
      static_cast<unsigned>(telemetry.responses), static_cast<unsigned>(telemetry.responsesLost),
      static_cast<unsigned>(telemetry.readsAbandoned), telemetry.provisioning,
      static_cast<unsigned>(telemetry.registerAttempts),
      static_cast<unsigned>(telemetry.lastLatencyMs), telemetry.clockSynced ? "ntp" : "unset");

  gateway_.sendTelemetry(telemetry);
}

}  // namespace app
