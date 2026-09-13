#pragma once

#include <Arduino.h>

#include <functional>

// Which wire protocol is carrying the uplink. The payload shape differs per
// transport, so callers occasionally need to know.
enum class TransportKind { Mqtt, Http };

// -----------------------------------------------------------------------------
//  Strategy interface for the uplink.
//
//  `sendUplink` is the hot path (one tag read). `publish` / `subscribe` are the
//  generic pub/sub operations that provisioning and telemetry need; transports
//  that cannot do pub/sub (HTTP) answer false and ignore them.
// -----------------------------------------------------------------------------
class TransportMode {
 public:
  using MessageHandler = std::function<void(const String& topic, const String& payload)>;

  virtual ~TransportMode() = default;

  virtual TransportKind kind() const = 0;

  virtual bool begin() = 0;
  virtual void loop() {}
  virtual bool isConnected() = 0;

  /** Sends one message on the node's own uplink channel. */
  virtual bool sendUplink(const String& payload) = 0;

  /**
   * Status of the answer the last sendUplink() carried back, or 0 when this
   * transport has no synchronous answer.
   *
   * MQTT has none: the gateway replies later, on the responses topic. HTTP does
   * — the POST returns the Cloud's status — and reporting it here is what lets
   * MessageGateway feed both arms of the A/B bench through the same path, so the
   * direct arm also gets an access decision and a measured round trip. It used
   * to be an HttpTransport-only accessor that nothing could reach, because the
   * gateway holds a TransportMode.
   */
  virtual int uplinkStatus() const { return 0; }

  virtual bool publish(const String& topic, const String& payload, bool retain = false) {
    (void)topic;
    (void)payload;
    (void)retain;
    return false;
  }

  virtual bool subscribe(const String& topic) {
    (void)topic;
    return false;
  }

  /**
   * Drops a subscription, and with it any record kept for the reconnect replay.
   *
   * Needed because the node re-registers under a new node_id: with
   * clean_session=false the broker keeps the old <node_id>/responses
   * subscription alive across reboots, so every abandoned identity leaves a
   * permanent delivery path that nothing routes.
   */
  virtual bool unsubscribe(const String& topic) {
    (void)topic;
    return false;
  }

  /** Where `sendUplink` publishes. Changes when the gateway assigns a node id. */
  virtual void setUplinkTopic(const String& topic) { (void)topic; }

  /** Retained "I am gone" message the broker publishes when this node drops. */
  virtual void setLastWill(const String& topic, const String& payload, bool retain = true) {
    (void)topic;
    (void)payload;
    (void)retain;
  }

  void setMessageHandler(MessageHandler handler) { handler_ = std::move(handler); }

 protected:
  void dispatch(const String& topic, const String& payload) const {
    if (handler_) handler_(topic, payload);
  }

 private:
  MessageHandler handler_;
};
