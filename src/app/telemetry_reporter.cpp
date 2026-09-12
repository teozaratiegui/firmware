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

void TelemetryReporter::loop(bool tagPresent) {
  const uint32_t now = millis();
  if (!first_ && now - lastReportMs_ < intervalMs_) return;
  first_        = false;
  lastReportMs_ = now;
  report(tagPresent);
}

void TelemetryReporter::report(bool tagPresent) {
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

  Serial.printf(
      "[TELEMETRY] up=%us wifi=%s ip=%s rssi=%d heap=%u node=%s tag_present=%d sent=%u "
      "queued=%u dropped=%u lost=%u abandoned=%u prov=%s/%u last_rtt=%ums clock=%s\n",
      static_cast<unsigned>(telemetry.uptimeS), telemetry.wifiUp ? "up" : "down",
      telemetry.ip.isEmpty() ? "-" : telemetry.ip.c_str(), static_cast<int>(telemetry.rssi),
      static_cast<unsigned>(telemetry.freeHeap),
      telemetry.nodeId.isEmpty() ? "unregistered" : telemetry.nodeId.c_str(), tagPresent ? 1 : 0,
      static_cast<unsigned>(stats.tagReadsSent), static_cast<unsigned>(gateway_.pendingCount()),
      static_cast<unsigned>(stats.tagReadsDropped), static_cast<unsigned>(stats.responsesLost),
      static_cast<unsigned>(stats.readsAbandoned), telemetry.provisioning,
      static_cast<unsigned>(telemetry.registerAttempts),
      static_cast<unsigned>(stats.lastLatencyMs), net::clockSynced() ? "ntp" : "unset");

  gateway_.sendTelemetry(telemetry);
}

}  // namespace app
