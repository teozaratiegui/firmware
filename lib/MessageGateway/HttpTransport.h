#pragma once

#include <Arduino.h>

#include "TransportMode.h"

// -----------------------------------------------------------------------------
//  Direct HTTPS POST to the Lambda Function URL, bypassing the Fog layer.
//
//  Kept as the A/B arm for measuring what the Fog layer contributes (latency and
//  traffic with and without it). It has no pub/sub: `publish` and `subscribe`
//  inherit the no-op defaults, so provisioning is skipped in this mode and the
//  node authenticates with the Cloud API key instead of a gateway-issued
//  node_key.
//
//  It does have an answer, though — the POST returns one. It is reported through
//  `uplinkStatus()`, which is what makes this arm measurable: until v0.2 the
//  direct path produced no access decision and no round trip at all, so the
//  bench the thesis leans on was instrumented on the MQTT side only.
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
  int uplinkStatus() const override { return lastStatus_; }

  /**
   * Did the request reach the Cloud and come back with an answer?
   *
   * Deliberately not "was the tag allowed?": those are different axes, and
   * collapsing them was a real defect. This used to accept only 200/201/204, so
   * a 404 (unknown tag) or a 422 (denied) — both perfectly normal outcomes —
   * were reported as a failed send. MessageGateway then put the read back on the
   * outbox and retried it every outboxRetryMs forever, and since the handler
   * writes the event row before returning the status, one denied tag wrote tens
   * of thousands of rows a day. The MQTT arm never had the problem: there the
   * PUBLISH is the send and the decision arrives separately.
   *
   * Only HTTPClient's own negative error codes mean "never got there". Static
   * and in the header so the rule can be asserted without an HTTPClient.
   */
  static bool isDelivered(int code) { return code >= 100; }

  /**
   * How long a connect attempt may take before the read is treated as a failed
   * send. MqttTransport already bounds its socket at 2 s for the same reason
   * (MqttTransport.h): a host that is routable but not listening is a dead link,
   * not a slow one, and HTTPClient's 5 s default freezes the whole superloop
   * once per read while it finds that out.
   */
  static constexpr uint16_t kConnectTimeoutMs = 2000;

  /**
   * How long to wait for the answer once connected. Deliberately *not* cut down
   * with the connect timeout: the function is allowed 4 s (var.lambda_timeout)
   * and a cold start plus the messenger's own 1500 ms bound can legitimately use
   * most of it, so a tighter budget would abandon answers the Cloud is about to
   * give — and, since the POST is not idempotent, retry them as new events.
   */
  static constexpr uint16_t kResponseTimeoutMs = 5000;

 private:
  String normalisedUrl() const;

  String url_;
  String apiKey_;
  String bearer_;
  int    lastStatus_ = 0;
};
