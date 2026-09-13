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

  /**
   * @param tagsAccepted distinct reads the debounce let through, which is the
   *   only honest answer to "how many tags did the reader read". The gateway's
   *   own `tagReadsSent` counts transmission attempts, so a read retried twice
   *   adds three to it — the two numbers are not interchangeable and the
   *   difference is what a lost-read count is made of.
   */
  void loop(bool tagPresent, uint32_t tagsAccepted);

 private:
  void report(bool tagPresent, uint32_t tagsAccepted);

  MessageGateway&              gateway_;
  provisioning::NodeRegistrar& registrar_;
  uint32_t        intervalMs_;
  const char*     mode_;
  const char*     firmware_;
  uint32_t        lastReportMs_ = 0;
  bool            first_        = true;
};

}  // namespace app
