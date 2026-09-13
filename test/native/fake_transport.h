#pragma once

#include <Arduino.h>

#include <vector>

#include "TransportMode.h"

// -----------------------------------------------------------------------------
//  A TransportMode the tests drive by hand.
//
//  It records everything MessageGateway asks of it (publishes, subscribes,
//  unsubscribes, the uplink topic, the last will) and lets the test raise and
//  drop the link, so the reconnect behaviour can be exercised without a broker.
// -----------------------------------------------------------------------------
class FakeTransport : public TransportMode {
 public:
  struct Publication {
    String topic;
    String payload;
    bool   retain = false;
  };

  explicit FakeTransport(TransportKind kind = TransportKind::Mqtt) : kind_(kind) {}

  TransportKind kind() const override { return kind_; }

  bool begin() override {
    begun = true;
    if (connectOnBegin) connected_ = true;
    return connected_;
  }

  void loop() override { loops++; }

  bool isConnected() override { return connected_; }

  bool sendUplink(const String& payload) override {
    // A synchronous transport spends real time inside the send; the MQTT one
    // returns as soon as the PUBLISH is on the socket. Charging it here is what
    // lets a test see whether the round trip a synchronous answer reports is a
    // measurement or a zero.
    g_millis += uplinkCostMs;
    if (!connected_ || uplinkTopic.isEmpty() || failUplink) return false;
    uplinks.push_back(Publication{uplinkTopic, payload, false});
    return true;
  }

  /** 0 = no synchronous answer (what MQTT does); >0 = the status a POST returned. */
  int uplinkStatus() const override { return uplinkStatus_; }

  bool publish(const String& topic, const String& payload, bool retain = false) override {
    if (!connected_) return false;
    publications.push_back(Publication{topic, payload, retain});
    return true;
  }

  bool subscribe(const String& topic) override {
    subscribed.push_back(topic);
    return connected_;
  }

  bool unsubscribe(const String& topic) override {
    unsubscribed.push_back(topic);
    return connected_;
  }

  void setUplinkTopic(const String& topic) override { uplinkTopic = topic; }

  void setLastWill(const String& topic, const String& payload, bool retain = true) override {
    willTopic   = topic;
    willPayload = payload;
    willRetain  = retain;
    wills++;
  }

  // ── Test controls ──────────────────────────────────────────────────────────
  void setConnected(bool up) { connected_ = up; }
  /** Simulates a message arriving from the broker. */
  void deliver(const String& topic, const String& payload) { dispatch(topic, payload); }

  int countPublishedOn(const char* topic) const {
    int n = 0;
    for (const Publication& p : publications) {
      if (p.topic == topic) n++;
    }
    return n;
  }
  int countSubscribed(const char* topic) const {
    int n = 0;
    for (const String& t : subscribed) {
      if (t == topic) n++;
    }
    return n;
  }
  bool didUnsubscribe(const char* topic) const {
    for (const String& t : unsubscribed) {
      if (t == topic) return true;
    }
    return false;
  }

  void setUplinkStatus(int status) { uplinkStatus_ = status; }

  bool     connectOnBegin = true;
  bool     failUplink     = false;
  bool     begun          = false;
  uint32_t uplinkCostMs   = 0;

  String              uplinkTopic;
  String              willTopic;
  String              willPayload;
  bool                willRetain = true;
  unsigned            wills      = 0;
  unsigned            loops      = 0;
  std::vector<Publication> publications;
  std::vector<Publication> uplinks;
  std::vector<String>      subscribed;
  std::vector<String>      unsubscribed;

 private:
  TransportKind kind_;
  int           uplinkStatus_ = 0;
  bool          connected_    = false;
};
