#include "MqttTransport.h"

#include <WiFi.h>

MqttTransport* MqttTransport::instance_ = nullptr;

void MqttTransport::trampoline(char* topic, uint8_t* payload, unsigned int length) {
  if (instance_) instance_->onMessage(topic, payload, length);
}

MqttTransport::MqttTransport(Config cfg) : cfg_(std::move(cfg)), mqtt_(net_) {
  // PubSubClient's callback is a plain function pointer, so there is exactly one
  // slot for it and therefore exactly one usable MqttTransport. The node builds
  // one; say so out loud rather than letting a second instance quietly steal
  // every incoming message from the first.
  if (instance_ != nullptr) {
    Serial.println("[GW/MQTT] ERROR: a second MqttTransport was built — the first one will "
                   "stop receiving messages. Only one is supported.");
  }
  instance_ = this;
  mqtt_.setBufferSize(cfg_.bufferSize);
  mqtt_.setKeepAlive(cfg_.keepaliveS);
  mqtt_.setSocketTimeout(cfg_.socketTimeoutS);
  mqtt_.setCallback(trampoline);
}

MqttTransport::~MqttTransport() {
  if (instance_ == this) instance_ = nullptr;
}

bool MqttTransport::begin() {
  mqtt_.setServer(cfg_.host.c_str(), cfg_.port);
  started_ = true;
  return connect();
}

void MqttTransport::loop() {
  if (!started_) return;
  if (mqtt_.connected()) {
    mqtt_.loop();
    return;
  }
  // A dead broker must not take the superloop with it. Two things bound that:
  // this timer, which spaces the attempts out, and cfg_.socketTimeoutS, which
  // caps how long one attempt can block inside PubSubClient. The timer alone is
  // not enough — it limits how often the node stalls, not for how long.
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - nextRetryAtMs_) < 0) return;
  nextRetryAtMs_ = now + cfg_.retryMs;
  connect();
}

bool MqttTransport::isConnected() {
  return mqtt_.connected();
}

bool MqttTransport::connect() {
  if (mqtt_.connected()) return true;
  if (WiFi.status() != WL_CONNECTED) return false;

  const bool ok =
      willTopic_.isEmpty()
          ? mqtt_.connect(cfg_.clientId.c_str(),
                          cfg_.user.isEmpty() ? nullptr : cfg_.user.c_str(),
                          cfg_.pass.isEmpty() ? nullptr : cfg_.pass.c_str(),
                          nullptr, 0, false, nullptr,
                          /*cleanSession=*/false)
          : mqtt_.connect(cfg_.clientId.c_str(),
                          cfg_.user.isEmpty() ? nullptr : cfg_.user.c_str(),
                          cfg_.pass.isEmpty() ? nullptr : cfg_.pass.c_str(),
                          willTopic_.c_str(), 1, willRetain_, willPayload_.c_str(),
                          /*cleanSession=*/false);

  if (!ok) {
    Serial.printf("[GW/MQTT] CONNECT failed host=%s:%u state=%d (%s)\n", cfg_.host.c_str(),
                  cfg_.port, mqtt_.state(), stateName(mqtt_.state()));
    return false;
  }

  Serial.printf("[GW/MQTT] connected as %s (clean_session=false)\n", cfg_.clientId.c_str());
  replaySubscriptions();
  return true;
}

void MqttTransport::replaySubscriptions() {
  for (const String& topic : subscriptions_) {
    if (!mqtt_.subscribe(topic.c_str(), 1)) {
      Serial.print("[GW/MQTT] ERROR: SUBSCRIBE failed for ");
      Serial.println(topic);
    }
  }
}

bool MqttTransport::subscribe(const String& topic) {
  if (topic.isEmpty()) return false;
  // Remember it first: the replay after a reconnect is what keeps downlink alive.
  bool known = false;
  for (const String& existing : subscriptions_) {
    if (existing == topic) {
      known = true;
      break;
    }
  }
  if (!known) subscriptions_.push_back(topic);

  if (!mqtt_.connected()) return false;
  const bool ok = mqtt_.subscribe(topic.c_str(), 1);
  if (ok) {
    Serial.print("[GW/MQTT] subscribed ");
    Serial.println(topic);
  }
  return ok;
}

bool MqttTransport::unsubscribe(const String& topic) {
  if (topic.isEmpty()) return false;
  // Forget it first: leaving it in the replay list would resubscribe it on the
  // next reconnect and undo the UNSUBSCRIBE.
  for (size_t i = 0; i < subscriptions_.size(); ++i) {
    if (subscriptions_[i] == topic) {
      subscriptions_.erase(subscriptions_.begin() + static_cast<long>(i));
      break;
    }
  }

  if (!mqtt_.connected()) return false;
  const bool ok = mqtt_.unsubscribe(topic.c_str());
  if (ok) {
    Serial.print("[GW/MQTT] unsubscribed ");
    Serial.println(topic);
  }
  return ok;
}

void MqttTransport::setUplinkTopic(const String& topic) {
  uplinkTopic_ = topic;
}

void MqttTransport::setLastWill(const String& topic, const String& payload, bool retain) {
  willTopic_   = topic;
  willPayload_ = payload;
  willRetain_  = retain;
  // Takes effect on the next CONNECT — the will is part of the CONNECT packet.
}

bool MqttTransport::sendUplink(const String& payload) {
  if (uplinkTopic_.isEmpty()) {
    Serial.println("[GW/MQTT] uplink topic not set yet (node not registered).");
    return false;
  }
  return publish(uplinkTopic_, payload, cfg_.retain);
}

bool MqttTransport::publish(const String& topic, const String& payload, bool retain) {
  if (!mqtt_.connected()) return false;

  const size_t needed = topic.length() + payload.length() + 16;
  if (needed > mqtt_.getBufferSize()) {
    Serial.printf("[GW/MQTT] ERROR: message %u B exceeds buffer %u B\n",
                  static_cast<unsigned>(needed), mqtt_.getBufferSize());
    return false;
  }

  const bool ok = mqtt_.publish(topic.c_str(), payload.c_str(), retain);
  if (!ok) {
    Serial.printf("[GW/MQTT] ERROR: publish failed topic=%s state=%d (%s)\n", topic.c_str(),
                  mqtt_.state(), stateName(mqtt_.state()));
  }
  return ok;
}

void MqttTransport::onMessage(char* topic, const uint8_t* payload, unsigned int length) {
  String body;
  body.reserve(length);
  for (unsigned int i = 0; i < length; ++i) body += static_cast<char>(payload[i]);
  dispatch(String(topic), body);
}

const char* MqttTransport::stateName(int state) {
  switch (state) {
    case MQTT_CONNECTION_TIMEOUT:     return "CONNECTION_TIMEOUT";
    case MQTT_CONNECTION_LOST:        return "CONNECTION_LOST";
    case MQTT_CONNECT_FAILED:         return "CONNECT_FAILED (TCP could not open: wrong host/port or different subnet)";
    case MQTT_DISCONNECTED:           return "DISCONNECTED";
    case MQTT_CONNECTED:              return "CONNECTED";
    case MQTT_CONNECT_BAD_PROTOCOL:   return "BAD_PROTOCOL";
    case MQTT_CONNECT_BAD_CLIENT_ID:  return "BAD_CLIENT_ID";
    case MQTT_CONNECT_UNAVAILABLE:    return "UNAVAILABLE";
    case MQTT_CONNECT_BAD_CREDENTIALS:return "BAD_CREDENTIALS";
    case MQTT_CONNECT_UNAUTHORIZED:   return "UNAUTHORIZED";
    default:                          return "UNKNOWN";
  }
}
