#pragma once

#include <Arduino.h>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "GatewayMessages.h"
#include "TransportMode.h"

// -----------------------------------------------------------------------------
//  Facade over the uplink. Owns the transport, knows the node's topic layout,
//  routes incoming messages to subscribers and keeps tag reads alive across a
//  short outage.
//
//  It deliberately knows nothing about pin numbers, RFID or build flags: the
//  composition root in src/ supplies the configuration and the clocks, which is
//  what makes the whole thing testable off-device.
// -----------------------------------------------------------------------------
class MessageGateway {
 public:
  using TopicHandler    = std::function<void(const String& payload)>;
  using ResponseHandler = std::function<void(const GatewayResponse&, uint32_t latencyMs)>;
  using MillisFn        = uint32_t (*)();
  using IsoTimeFn       = String (*)();

  struct Config {
    String      nodePrefix        = "esp";
    String      gatewayPrefix     = "gateway";
    String      mac;                        // canonical AA:BB:CC:DD:EE:FF
    const char* firmware          = "";
    uint32_t    responseTimeoutMs = 8000;
    // How many reads ride out an outage. 0 disables the outbox: a read that
    // cannot go out immediately is counted in `tagReadsDropped` and forgotten.
    uint8_t     outboxCapacity    = 16;
    uint32_t    outboxRetryMs     = 2000;
    // How many times a read that got no answer is put back on the outbox before
    // it is abandoned. 0 restores the original policy (drop on timeout). Every
    // retry risks a duplicate event upstream if the answer was merely slow, and
    // the gateway retries its own POST without an idempotency key (finding G3),
    // so the two policies are a measurable knob rather than a hard-coded choice.
    uint8_t     unansweredReadRetries = 2;
    MillisFn    millisFn          = nullptr;
    IsoTimeFn   isoTimeFn         = nullptr;  // empty String until NTP syncs
  };

  struct Stats {
    uint32_t tagReadsSent    = 0;
    uint32_t tagReadsQueued  = 0;
    uint32_t tagReadsDropped = 0;
    uint32_t uplinkFailures  = 0;
    uint32_t responses       = 0;
    uint32_t responsesLost   = 0;   // timed out with no answer
    uint32_t readsAbandoned  = 0;   // unanswered and out of retries
    uint32_t lastLatencyMs   = 0;
  };

  MessageGateway(std::unique_ptr<TransportMode> transport, Config config);

  bool begin();
  void loop();

  /** Credentials handed out by the gateway; rebuilds topics and subscribes. */
  void setIdentity(const String& nodeId, const String& nodeKey);
  /** Forgets them, so reads queue up instead of going out with dead credentials. */
  void clearIdentity();
  /**
   * True once the gateway has issued a node_key. Deliberately not "has an
   * identity": the direct-to-Lambda path sets a node id with an empty key and
   * authenticates with the Cloud API key instead, so it is false there even
   * though the node knows its name.
   */
  bool hasGatewayCredentials() const { return !nodeId_.isEmpty() && !nodeKey_.isEmpty(); }
  const String& nodeId() const { return nodeId_; }

  bool isConnected();
  bool isMqtt() const { return transport_ && transport_->kind() == TransportKind::Mqtt; }

  /**
   * Counts observed disconnected->connected transitions, starting at 0 and
   * reaching 1 on the first successful connect.
   *
   * It exists so collaborators can tell "the link came back" from "the link is
   * up", which polling isConnected() cannot do. The registrar uses it to decide
   * when its attempt budget is genuinely renewed.
   */
  uint32_t linkGeneration() const { return linkGeneration_; }

  bool subscribeTopic(const String& topic, TopicHandler handler);
  bool publishTopic(const String& topic, const String& payload, bool retain = false);

  /**
   * Relays one tag read. Queues it instead of losing it when it cannot go out —
   * the link is down, the node has no credentials, or an earlier read is still
   * waiting for its answer. Returns false in all three cases; the read is not
   * lost, it is on the outbox and `loop()` will relay it.
   *
   * When `payloadOut` is given it receives the JSON that was actually published,
   * or is cleared when the read was queued. Callers that want to log the read
   * use that instead of building the payload a second time: two builds meant
   * two clock reads, so the line on the console could carry a different `ts`
   * than the message on the wire.
   */
  bool sendTagRead(const String& tag, String* payloadOut = nullptr);
  bool sendTelemetry(const NodeTelemetry& telemetry);
  /** Retained presence document on the MAC-keyed status topic. */
  bool announcePresence(bool online);

  void setResponseHandler(ResponseHandler handler) { onResponse_ = std::move(handler); }

  String requestsTopic() const;
  String responsesTopic() const;
  String telemetryTopic() const;
  String presenceTopic() const;
  String registerTopic() const;
  String registerResponseTopic() const;

  /** The JSON sendTagRead publishes for one tag, per the transport's contract. */
  String buildTagPayload(const String& tag) const;

  const Stats& stats() const { return stats_; }
  uint8_t      pendingCount() const { return static_cast<uint8_t>(outbox_.size()); }

  /**
   * Whether a read that cannot go out right now is kept or dropped.
   *
   * Callers log the two outcomes differently: with the outbox disabled, "queued"
   * would be a lie. Serial-only bring-up (MESSAGE_GATEWAY=0) is the case that
   * matters — see makeGatewayConfig() in src/main.cpp.
   */
  bool storeAndForwardEnabled() const { return cfg_.outboxCapacity > 0; }

 private:
  struct Subscription {
    String       topic;
    TopicHandler handler;
  };

  // A queued read carries how many times it has already been put on the wire, so
  // the retry budget belongs to the read and not to the queue.
  // No default member initialiser: the Arduino core still compiles this as
  // C++11, where that would stop PendingRead being an aggregate.
  struct PendingRead {
    String  tag;
    uint8_t attempts;
  };

  uint32_t now() const;
  String   isoNow() const;
  bool     readyToTransmit() const;
  void     handleMessage(const String& topic, const String& payload);
  void     handleResponse(const String& payload);
  void     completeRead(const GatewayResponse& response);
  bool     transmit(const String& tag, uint8_t attempts, String* payloadOut = nullptr);
  void     flushOutbox();
  void     expireInFlight();
  void     enqueue(const String& tag, uint8_t attempts, bool requeue);
  void     applyTopics();
  void     trackLink();
  void     refreshLastWill();
  void     dropResponsesSubscription(const String& nodeId);

  std::unique_ptr<TransportMode> transport_;
  Config                         cfg_;
  String                         nodeId_;
  String                         nodeKey_;
  std::vector<Subscription>      subscriptions_;
  std::vector<PendingRead>       outbox_;
  ResponseHandler                onResponse_;
  Stats                          stats_;
  String                         inFlightTag_;
  uint8_t                        inFlightAttempts_ = 0;
  uint32_t                       inFlightSinceMs_ = 0;
  uint32_t                       nextFlushAtMs_   = 0;
  uint32_t                       linkGeneration_  = 0;
  bool                           inFlight_        = false;
  bool                           linkUp_          = false;
};
