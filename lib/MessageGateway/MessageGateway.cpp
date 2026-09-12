#include "MessageGateway.h"

MessageGateway::MessageGateway(std::unique_ptr<TransportMode> transport, Config config)
    : transport_(std::move(transport)), cfg_(std::move(config)) {
  outbox_.reserve(cfg_.outboxCapacity);
}

uint32_t MessageGateway::now() const {
  return cfg_.millisFn ? cfg_.millisFn() : millis();
}

String MessageGateway::isoNow() const {
  return cfg_.isoTimeFn ? cfg_.isoTimeFn() : String();
}

// ── Topics ───────────────────────────────────────────────────────────────────

String MessageGateway::requestsTopic() const {
  return nodeId_.isEmpty() ? String() : cfg_.nodePrefix + "/" + nodeId_ + "/requests";
}

String MessageGateway::responsesTopic() const {
  return nodeId_.isEmpty() ? String() : cfg_.nodePrefix + "/" + nodeId_ + "/responses";
}

// Falls back to the MAC while the node has no id. A node that cannot register
// used to emit nothing at all upstream — loud on the register topic that nobody
// watches, silent on the one that is watched — so the only evidence of a
// misconfigured node was a serial console that a door-mounted node does not
// have. The MAC is the identity it always has.
String MessageGateway::telemetryTopic() const {
  const String key = nodeId_.isEmpty() ? cfg_.mac : nodeId_;
  return cfg_.nodePrefix + "/" + key + "/telemetry";
}

// Keyed by MAC, not node id: the presence topic has to be stable across
// re-registrations and has to exist before the node has an id at all.
String MessageGateway::presenceTopic() const {
  return cfg_.nodePrefix + "/" + cfg_.mac + "/status";
}

String MessageGateway::registerTopic() const {
  return cfg_.gatewayPrefix + "/register";
}

String MessageGateway::registerResponseTopic() const {
  return cfg_.gatewayPrefix + "/register/response/" + cfg_.mac;
}

// ── Lifecycle ────────────────────────────────────────────────────────────────

bool MessageGateway::begin() {
  if (!transport_) return false;

  transport_->setMessageHandler(
      [this](const String& topic, const String& payload) { handleMessage(topic, payload); });

  // The will has to be in place before CONNECT, so it is set up front. A node
  // that booted with credentials in NVS already knows its node_id here, which
  // is why the identity is restored before begin() is called.
  refreshLastWill();

  const bool ok = transport_->begin();
  // trackLink() re-states topics and presence on the rising edge; when the link
  // is still down, the topics are registered anyway so the transport can replay
  // them once it connects.
  trackLink();
  if (!linkUp_) applyTopics();
  return ok;
}

void MessageGateway::loop() {
  if (!transport_) return;
  transport_->loop();
  trackLink();
  expireInFlight();
  flushOutbox();
}

// A reconnect is not the same thing as "the link is up": collaborators that
// have to act once per reconnection cannot see it by polling isConnected(),
// because the superloop runs far faster than the broker reconnects. Counting
// the rising edges gives them a value that changes exactly once per new link.
void MessageGateway::trackLink() {
  const bool up = transport_ && transport_->isConnected();
  if (up == linkUp_) return;
  linkUp_ = up;

  if (!up) {
    Serial.println("[GW] uplink down.");
    // The reconnect is the only chance to update the will, so do it now while
    // the current identity is known.
    refreshLastWill();
    return;
  }
  linkGeneration_++;
  // A new session knows nothing of this node: the subscription and the presence
  // document both have to be re-stated. Skipping the presence re-announce left
  // the retained document on <prefix>/<MAC>/status saying online:false forever
  // after the first outage, because the last will had already fired.
  applyTopics();
  if (isMqtt()) announcePresence(true);
  Serial.printf("[GW] uplink up (link generation %u).\n",
                static_cast<unsigned>(linkGeneration_));
}

bool MessageGateway::isConnected() {
  return transport_ && transport_->isConnected();
}

void MessageGateway::setIdentity(const String& nodeId, const String& nodeKey) {
  const bool   changed  = (nodeId != nodeId_);
  const String previous = nodeId_;
  nodeId_  = nodeId;
  nodeKey_ = nodeKey;
  if (changed) dropResponsesSubscription(previous);
  applyTopics();
  if (!changed) return;
  // The will is part of the CONNECT packet and cannot be corrected afterwards,
  // so it is restated as soon as the identity changes — the next reconnect then
  // carries a testament that names the node instead of an empty node_id.
  refreshLastWill();
  if (isMqtt()) announcePresence(true);
}

void MessageGateway::clearIdentity() {
  dropResponsesSubscription(nodeId_);
  nodeId_  = String();
  nodeKey_ = String();
  // The uplink topic goes with them: publishing to the old one with a key the
  // gateway has rejected only produces more 403s.
  if (transport_) transport_->setUplinkTopic(String());
  refreshLastWill();
}

void MessageGateway::dropResponsesSubscription(const String& nodeId) {
  if (!transport_ || nodeId.isEmpty()) return;
  transport_->unsubscribe(cfg_.nodePrefix + "/" + nodeId + "/responses");
}

// Only takes effect on the next CONNECT, which is why it is called on every
// identity change and on every observed disconnect rather than once at boot.
void MessageGateway::refreshLastWill() {
  if (!transport_ || !isMqtt()) return;
  transport_->setLastWill(presenceTopic(),
                          messages::presence(cfg_.mac, nodeId_, false, cfg_.firmware), true);
}

void MessageGateway::applyTopics() {
  if (!transport_ || nodeId_.isEmpty()) return;
  transport_->setUplinkTopic(requestsTopic());
  // Subscribing to the answers is what turns this from fire-and-forget into an
  // access-control node; v0.1 published before subscribing and never parsed it.
  transport_->subscribe(responsesTopic());
}

// ── Pub / sub ────────────────────────────────────────────────────────────────

bool MessageGateway::subscribeTopic(const String& topic, TopicHandler handler) {
  if (!transport_ || topic.isEmpty()) return false;
  for (Subscription& sub : subscriptions_) {
    if (sub.topic == topic) {
      sub.handler = std::move(handler);
      return transport_->subscribe(topic);
    }
  }
  subscriptions_.push_back(Subscription{topic, std::move(handler)});
  return transport_->subscribe(topic);
}

bool MessageGateway::publishTopic(const String& topic, const String& payload, bool retain) {
  return transport_ && transport_->publish(topic, payload, retain);
}

void MessageGateway::handleMessage(const String& topic, const String& payload) {
  if (topic == responsesTopic()) {
    handleResponse(payload);
    return;
  }
  for (const Subscription& sub : subscriptions_) {
    if (sub.topic == topic) {
      if (sub.handler) sub.handler(payload);
      return;
    }
  }
  Serial.print("[GW] unrouted message on ");
  Serial.println(topic);
}

void MessageGateway::handleResponse(const String& payload) {
  const GatewayResponse response = GatewayResponse::parse(payload);
  if (!response.valid) {
    Serial.print("[GW] malformed response: ");
    Serial.println(payload);
    return;
  }

  uint32_t latencyMs = 0;
  if (inFlight_) {
    latencyMs  = now() - inFlightSinceMs_;
    inFlight_  = false;
    stats_.lastLatencyMs = latencyMs;
  }
  stats_.responses++;

  if (onResponse_) onResponse_(response, latencyMs);
}

// ── Tag reads ────────────────────────────────────────────────────────────────

String MessageGateway::buildTagPayload(const String& tag) const {
  return isMqtt() ? messages::tagRead(tag, nodeKey_, isoNow())
                  : messages::httpTagEvent(tag, nodeId_, isoNow());
}

// True when a read can go out right now. Over MQTT that means more than "the
// link is up": the node needs credentials the gateway will accept, and the
// single in-flight slot has to be free — see transmit().
bool MessageGateway::readyToTransmit() const {
  if (!transport_ || !transport_->isConnected()) return false;
  if (!isMqtt()) return true;
  return hasGatewayCredentials() && !inFlight_;
}

bool MessageGateway::sendTagRead(const String& tag, String* payloadOut) {
  if (payloadOut) *payloadOut = String();
  if (tag.isEmpty()) return false;

  if (!readyToTransmit()) {
    enqueue(tag, 0, /*requeue=*/false);
    return false;
  }
  if (!transmit(tag, 0, payloadOut)) {
    enqueue(tag, 0, /*requeue=*/false);
    return false;
  }
  return true;
}

bool MessageGateway::transmit(const String& tag, uint8_t attempts, String* payloadOut) {
  if (!transport_) return false;

  const String payload = buildTagPayload(tag);
  if (!transport_->sendUplink(payload)) {
    stats_.uplinkFailures++;
    return false;
  }

  stats_.tagReadsSent++;
  if (payloadOut) *payloadOut = payload;
  if (isMqtt()) {
    // One read in flight at a time, and callers must not overwrite the slot: the
    // gateway's answer carries no tag (core/contracts/gateway.py:43-59), so the
    // only thing that pairs a response with a read is this slot. Sending a
    // second read before the first is answered used to mis-pair them — the
    // round trip of read N was reported for response 1, and the reads it
    // displaced were never retried and never counted as lost. Both callers
    // (sendTagRead and flushOutbox) now wait through readyToTransmit().
    inFlightTag_      = tag;
    inFlightAttempts_ = static_cast<uint8_t>(attempts + 1);
    inFlightSinceMs_  = now();
    inFlight_         = true;
  }
  return true;
}

// Bounded, RAM-only store-and-forward: survives a broker or Wi-Fi outage, not a
// reboot. Persisting it to NVS/LittleFS is the documented next step.
void MessageGateway::enqueue(const String& tag, uint8_t attempts, bool requeue) {
  // A capacity of zero means "no store-and-forward at all"; without this the
  // full-queue branch below would erase from an empty vector.
  if (cfg_.outboxCapacity == 0) {
    stats_.tagReadsDropped++;
    return;
  }
  if (outbox_.size() >= cfg_.outboxCapacity) {
    outbox_.erase(outbox_.begin());
    stats_.tagReadsDropped++;
    Serial.println("[GW] outbox full — oldest tag read dropped.");
  }
  outbox_.push_back(PendingRead{tag, attempts});
  stats_.tagReadsQueued++;
  if (requeue) {
    Serial.printf("[GW] tag %s unanswered, requeued (attempt %u of %u, %u pending).\n",
                  tag.c_str(), static_cast<unsigned>(attempts + 1),
                  static_cast<unsigned>(cfg_.unansweredReadRetries + 1),
                  static_cast<unsigned>(outbox_.size()));
    return;
  }
  // Deliberately not "uplink down": the read also lands here while an earlier
  // one is still awaiting its answer, which is the common case now.
  Serial.printf("[GW] tag queued, not sent yet (%u pending).\n",
                static_cast<unsigned>(outbox_.size()));
}

void MessageGateway::flushOutbox() {
  if (outbox_.empty()) return;
  // Over MQTT this also waits for the in-flight slot, which is what keeps the
  // drain honest: the backlog used to go out in consecutive superloop
  // iterations, each send overwriting the slot, so a three-read backlog that was
  // answered normally reported one round trip and two zeroes, and a three-read
  // backlog that was never answered retried only the last one and dropped the
  // other two without counting them.
  if (!readyToTransmit()) return;

  // Retrying on every iteration would turn a dead backend into a tight loop of
  // blocking POSTs, so attempts are spaced out.
  const uint32_t nowMs = now();
  if (static_cast<int32_t>(nowMs - nextFlushAtMs_) < 0) return;
  nextFlushAtMs_ = nowMs + cfg_.outboxRetryMs;

  // One per attempt: draining the whole queue at once would stall the superloop
  // exactly when the link has just come back.
  const PendingRead pending = outbox_.front();
  if (!transmit(pending.tag, pending.attempts)) return;
  outbox_.erase(outbox_.begin());
  Serial.printf("[GW] queued tag relayed (%u left).\n", static_cast<unsigned>(outbox_.size()));

  // Ready for the next one as soon as this one is answered; over MQTT the
  // in-flight guard above is what actually paces it, and over HTTP the POST has
  // already completed by the time we get here.
  nextFlushAtMs_ = nowMs;
}

// The outbox only ever covered a failed PUBLISH. The likelier outage on a
// single-Pi deployment is the other one: the broker is up, the PUBLISH succeeds,
// and the gateway container that should consume it is down — a QoS 0 publish
// into nothing. Those reads used to be counted and thrown away here.
void MessageGateway::expireInFlight() {
  if (!inFlight_) return;
  if (now() - inFlightSinceMs_ < cfg_.responseTimeoutMs) return;

  inFlight_ = false;
  stats_.responsesLost++;
  Serial.printf("[GW] no answer for tag %s after %u ms.\n", inFlightTag_.c_str(),
                static_cast<unsigned>(cfg_.responseTimeoutMs));

  if (inFlightAttempts_ <= cfg_.unansweredReadRetries) {
    enqueue(inFlightTag_, inFlightAttempts_, /*requeue=*/true);
    return;
  }

  stats_.readsAbandoned++;
  Serial.printf("[GW] tag %s abandoned after %u unanswered attempts.\n", inFlightTag_.c_str(),
                static_cast<unsigned>(inFlightAttempts_));
}

// ── Telemetry and presence ───────────────────────────────────────────────────

bool MessageGateway::sendTelemetry(const NodeTelemetry& telemetry) {
  if (!transport_) return false;
  // Telemetry never travels on the ingest topic: a synthetic "tag" on
  // .../requests shows up upstream as a phantom asset event. Nor on the
  // presence topic, which is retained and doubles as the last will.
  if (!isMqtt()) return false;
  const String topic = telemetryTopic();
  if (topic.isEmpty()) return false;
  return transport_->publish(topic, messages::telemetry(telemetry), false);
}

bool MessageGateway::announcePresence(bool online) {
  if (!transport_ || !isMqtt()) return false;
  return transport_->publish(presenceTopic(),
                             messages::presence(cfg_.mac, nodeId_, online, cfg_.firmware), true);
}
