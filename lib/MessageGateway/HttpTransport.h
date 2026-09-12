#pragma once

#include <Arduino.h>

#include "TransportMode.h"

// -----------------------------------------------------------------------------
//  Direct HTTPS POST to the Lambda Function URL, bypassing the Fog layer.
//
//  Kept as the A/B arm for measuring what the Fog layer contributes (latency and
//  traffic with and without it). It has no downlink: `publish` and `subscribe`
//  inherit the no-op defaults, so provisioning is skipped in this mode and the
//  node authenticates with the Cloud API key instead of a gateway-issued
//  node_key.
//
//  NOTE: TLS runs with certificate validation disabled (`setInsecure`). Good
//  enough for the bench, not for production; see documents/ROADMAP.md, v0.5.
// -----------------------------------------------------------------------------
class HttpTransport : public TransportMode {
 public:
  HttpTransport(String url, String apiKey = "", String bearer = "");

  TransportKind kind() const override { return TransportKind::Http; }

  bool begin() override;
  bool isConnected() override;
  bool sendUplink(const String& payload) override;

  /** Status code of the last POST, or a negative HTTPClient error code. */
  int lastStatus() const { return lastStatus_; }

 private:
  String normalisedUrl() const;

  String url_;
  String apiKey_;
  String bearer_;
  int    lastStatus_ = 0;
};
