#pragma once

#include <Arduino.h>

#include "MessageGateway.h"
#include "provisioning/node_registrar.h"

namespace app {

// -----------------------------------------------------------------------------
//  Periodic sign of life, identical in both run modes.
//
//  In v0.1 this behaved backwards: the RFID (production) mode sent nothing
//  upstream, while the interval mode disguised its telemetry as a tag read on
//  the ingest topic. Now every mode reports on <node prefix>/<node_id>/telemetry
//  and the ingest topic only ever carries real tags.
// -----------------------------------------------------------------------------
class TelemetryReporter {
 public:
  TelemetryReporter(MessageGateway& gateway, provisioning::NodeRegistrar& registrar,
                    uint32_t intervalMs, const char* mode, const char* firmware);

  void loop(bool tagPresent);

 private:
  void report(bool tagPresent);

  MessageGateway&              gateway_;
  provisioning::NodeRegistrar& registrar_;
  uint32_t        intervalMs_;
  const char*     mode_;
  const char*     firmware_;
  uint32_t        lastReportMs_ = 0;
  bool            first_        = true;
};

}  // namespace app
