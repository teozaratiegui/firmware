#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>

#include <vector>

#include "TransportMode.h"

// -----------------------------------------------------------------------------
//  MQTT uplink to the Fog gateway.
//
//  Session handling, in order of why it matters:
//   * stable client id (`<clientId>-<MAC>`) + clean_session = false, so the
//     broker keeps the subscription and queues QoS 1 downlink while the node is
//     away. A random client id, as in v0.1, silently defeated both.
//   * subscriptions are remembered and replayed after every reconnect.
//   * retained last will on the presence topic, so the Fog can tell a dead node
//     from a quiet one.
//
//  Limitation: PubSubClient publishes at QoS 0 only — there is no API for QoS 1
//  publish. Uplink delivery is therefore best-effort and is covered by the
//  gateway's own reply, not by the broker. Moving to 256dpi/arduino-mqtt is the
//  documented next step (see documents/ROADMAP.md).
// -----------------------------------------------------------------------------
class MqttTransport : public TransportMode {
 public:
  struct Config {
    String   host       = "127.0.0.1";
    uint16_t port       = 1883;
    String   clientId   = "esp32-r200";
    String   user;
    String   pass;
    bool     retain     = false;
    uint16_t bufferSize = 1024;
    uint16_t keepaliveS = 30;
    uint32_t retryMs    = 3000;
    // Seconds PubSubClient may block inside one CONNECT or one read. Its own
    // default is 15, which is 15 seconds of frozen superloop per retry when the
    // broker's host is routable but silent — no reader polling, no blink, no
    // telemetry. The retry timer below bounds how often that happens; this is
    // what bounds how long it lasts.
    uint16_t socketTimeoutS = 2;
  };

  explicit MqttTransport(Config cfg);
  ~MqttTransport() override;

  TransportKind kind() const override { return TransportKind::Mqtt; }

  bool begin() override;
  void loop() override;
  bool isConnected() override;

  bool sendUplink(const String& payload) override;
  bool publish(const String& topic, const String& payload, bool retain = false) override;
  bool subscribe(const String& topic) override;
  bool unsubscribe(const String& topic) override;
  void setUplinkTopic(const String& topic) override;
  void setLastWill(const String& topic, const String& payload, bool retain = true) override;

 private:
  bool connect();
  void replaySubscriptions();
  void onMessage(char* topic, const uint8_t* payload, unsigned int length);

  static const char* stateName(int state);
  static MqttTransport* instance_;  // PubSubClient takes a plain function pointer
  static void trampoline(char* topic, uint8_t* payload, unsigned int length);

  Config              cfg_;
  WiFiClient          net_;
  PubSubClient        mqtt_;
  String              uplinkTopic_;
  String              willTopic_;
  String              willPayload_;
  bool                willRetain_ = true;
  std::vector<String> subscriptions_;
  uint32_t            nextRetryAtMs_ = 0;
  bool                started_       = false;
};
