#pragma once
#include <Arduino.h>
#include <memory>
#include <ctype.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <PubSubClient.h>

// -------- Transport interface (strategy) --------
class TransportMode {
public:
  virtual ~TransportMode() = default;
  virtual bool begin() = 0;
  virtual bool send(const String& payload) = 0;
  // Called every main loop iteration so transports that need polling (MQTT) can do so.
  virtual void loop() {}
  virtual bool isMqttTransport() const { return false; }
  // Ad-hoc publish to an arbitrary topic (MQTT only; HTTP and others return false).
  virtual bool publishToTopic(const char* topic, const char* payload, bool retain = false) {
    (void)topic;
    (void)payload;
    (void)retain;
    return false;
  }
};

// -------- HTTP transport --------
// Supports https:// via WiFiClientSecure (setInsecure for Lambda URL try-out).
// Optional x-api-key and/or Bearer Authorization headers.
class Http : public TransportMode {
public:
  Http(String url, String xApiKey = "", String authBearer = "")
  : url_(std::move(url)), xApiKey_(std::move(xApiKey)), bearer_(std::move(authBearer)) {}

  bool begin() override { return true; }

  // Only HTTP 200 counts as success; 404, 403, 409, 422, 4xx, 5xx → false.
  bool send(const String& payload) override {
    String url = url_;
    url.trim();
    if (url.length() == 0) {
      Serial.println("[GW/HTTP] GATEWAY_LAMBDA_URL is empty after trim");
      return false;
    }
    // Normalize scheme to lowercase so HTTPS:// or Http:// still match TLS branch.
    const int colon = url.indexOf(':');
    if (colon > 0) {
      for (int i = 0; i < colon; ++i) {
        url.setCharAt(i, (char)tolower((unsigned char)url.charAt(i)));
      }
    }
    // Lambda Function URLs are HTTPS-only; plain http:// → TLS mismatch error from AWS.
    if (url.startsWith("http://") && url.indexOf(".lambda-url.") >= 0) {
      Serial.println("[GW/HTTP] URL used http:// on *.lambda-url.* — rewriting to https://");
      url = "https://" + url.substring(7);
    }

    const bool useTls = url.startsWith("https://");
    Serial.print("[GW/HTTP] ");
    Serial.print(useTls ? "TLS (WiFiClientSecure)" : "PLAIN HTTP (WiFiClient)");
    Serial.print(" POST url_len=");
    Serial.print(url.length());
    Serial.print(" scheme=");
    Serial.print(useTls ? "https" : (url.startsWith("http://") ? "http" : "?"));
    Serial.print(" url_preview=");
    {
      const unsigned show = url.length() > 56 ? 56 : url.length();
      Serial.println(url.substring(0, show) + (url.length() > 56 ? "…" : ""));
    }

    HTTPClient http;
    WiFiClientSecure tlsClient;
    bool opened = false;
    if (useTls) {
      tlsClient.setInsecure();
      opened = http.begin(tlsClient, url);
    } else {
      opened = http.begin(url);
    }
    if (!opened) {
      Serial.println("[GW/HTTP] http.begin() failed (bad URL or TLS setup)");
      http.end();
      return false;
    }

    http.addHeader("Content-Type", "application/json");
    const bool hasKey = xApiKey_.length() > 0;
    const bool hasBearer = bearer_.length() > 0;
    if (hasKey)
      http.addHeader("x-api-key", xApiKey_);
    if (hasBearer)
      http.addHeader("Authorization", "Bearer " + bearer_);
    Serial.print("[GW/HTTP] POST json_bytes=");
    Serial.print(payload.length());
    Serial.print(" headers: x-api-key=");
    Serial.print(hasKey ? "yes" : "no");
    Serial.print(" bearer=");
    Serial.println(hasBearer ? "yes" : "no");

    const int code = http.POST(payload);
    Serial.print("[GW/HTTP] HTTP status code: ");
    Serial.println(code);
    if (code < 0) {
      Serial.print("[GW/HTTP] transport error: ");
      Serial.println(http.errorToString(code));
    }
    if (code != 200 || code != 201) {
      const String errBody = http.getString();
      if (errBody.length()) {
        Serial.print("[GW/HTTP] response body (truncated 512): ");
        Serial.println(errBody.length() > 512 ? errBody.substring(0, 512) + "…" : errBody);
      }
    }
    http.end();
    return code == 200;
  }

private:
  String url_;
  String xApiKey_;
  String bearer_;
};

// -------- MQTT transport --------
//
// Node → gateway: <topicPrefix><nodeId>/requests  (publish)
// Gateway → node: <topicPrefix><nodeId>/responses (subscribe before any publish)
//
// The static callbackTarget_ / onMessageTrampoline pattern is required because
// PubSubClient only accepts a plain C-style function pointer as callback.
// callbackTarget_ is defined in MessageGateway.cpp.
class Mqtt : public TransportMode {
public:
  struct Config {
    String   topicPrefix = "bicicletero/esp/";
    String   nodeId      = "001";
    String   host        = "192.168.1.5";
    uint16_t port        = 1883;
    String   user;
    String   pass;
    String   clientId    = "esp32-r200";
    bool     retain      = false;
  };

  explicit Mqtt(Config cfg)
  : cfg_(std::move(cfg)), mqtt_(net_) {
    callbackTarget_  = this;
    requestsTopic_   = cfg_.topicPrefix + cfg_.nodeId + "/requests";
    responsesTopic_  = cfg_.topicPrefix + cfg_.nodeId + "/responses";
    mqtt_.setBufferSize(768);
    mqtt_.setCallback(onMessageTrampoline);
  }

  ~Mqtt() {
    if (callbackTarget_ == this) callbackTarget_ = nullptr;
  }

  bool begin() override {
    mqtt_.setServer(cfg_.host.c_str(), cfg_.port);
    return ensureConnected();
  }

  bool send(const String& payload) override {
    if (!ensureConnected()) {
      Serial.println("[GW/MQTT] send aborted: no broker connection (details logged above).");
      return false;
    }
    const uint16_t bufSize = mqtt_.getBufferSize();
    const size_t approxPacket = 32 + requestsTopic_.length() + payload.length();
    if (approxPacket > bufSize) {
      Serial.print("[GW/MQTT] ERROR: message exceeds MQTT buffer (approx ");
      Serial.print(approxPacket);
      Serial.print(" bytes > buffer ");
      Serial.print(bufSize);
      Serial.println("). Increase buffer in Mqtt constructor.");
      return false;
    }
    const bool pubOk = mqtt_.publish(requestsTopic_.c_str(), payload.c_str(), cfg_.retain);
    if (!pubOk) {
      Serial.print("[GW/MQTT] ERROR: publish() failed on requests topic. payload_bytes=");
      Serial.print(payload.length());
      Serial.print(" mqtt.connected=");
      Serial.print(mqtt_.connected() ? "true" : "false");
      Serial.print(" mqtt.state=");
      Serial.print(mqtt_.state());
      Serial.print(" (");
      Serial.print(mqttStateStr(mqtt_.state()));
      Serial.println(")");
    }
    return pubOk;
  }

  bool publishToTopic(const char* topic, const char* payload, bool retain) override {
    if (!topic || !payload) return false;
    if (!ensureConnected()) {
      Serial.println("[GW/MQTT] publishToTopic aborted: not connected.");
      return false;
    }
    const bool ok = mqtt_.publish(topic, payload, retain);
    if (!ok) {
      Serial.print("[GW/MQTT] ERROR: publishToTopic failed topic=");
      Serial.println(topic);
    }
    return ok;
  }

  // Must be called every main loop so PubSubClient processes incoming messages
  // and sends keepalive PINGs to the broker.
  void loop() override {
    if (mqtt_.connected()) mqtt_.loop();
  }

  bool isMqttTransport() const override { return true; }

  static Mqtt*  callbackTarget_;
  static void   onMessageTrampoline(char* topic, byte* payload, unsigned int len);

private:
  Config       cfg_;
  String       requestsTopic_;
  String       responsesTopic_;
  WiFiClient   net_;
  PubSubClient mqtt_;

  static const char* mqttStateStr(int s) {
    switch (s) {
      case MQTT_CONNECTION_TIMEOUT:
        return "CONNECTION_TIMEOUT";
      case MQTT_CONNECTION_LOST:
        return "CONNECTION_LOST";
      case MQTT_CONNECT_FAILED:
        return "CONNECT_FAILED";
      case MQTT_DISCONNECTED:
        return "DISCONNECTED";
      case MQTT_CONNECTED:
        return "CONNECTED";
      case MQTT_CONNECT_BAD_PROTOCOL:
        return "BAD_PROTOCOL";
      case MQTT_CONNECT_BAD_CLIENT_ID:
        return "BAD_CLIENT_ID";
      case MQTT_CONNECT_UNAVAILABLE:
        return "UNAVAILABLE";
      case MQTT_CONNECT_BAD_CREDENTIALS:
        return "BAD_CREDENTIALS";
      case MQTT_CONNECT_UNAUTHORIZED:
        return "UNAUTHORIZED";
      default:
        return "UNKNOWN";
    }
  }

  void handleIncomingMessage(char* topic, byte* payload, unsigned int len) {
    String msg;
    msg.reserve(len);
    for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
    const String t(topic);
    if (t == responsesTopic_) {
      Serial.print("[GW/MQTT] gateway → node (responses): ");
      Serial.println(msg);
    } else {
      Serial.print("[GW/MQTT] message topic=");
      Serial.print(topic);
      Serial.print(" payload=");
      Serial.println(msg);
    }
  }

  bool ensureConnected() {
    if (mqtt_.connected()) return true;

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(
          "[GW/MQTT] ERROR: Wi-Fi is not connected (WL_CONNECTED). MQTT broker cannot be reached.");
      return false;
    }

    for (uint8_t i = 0; i < 3 && !mqtt_.connected(); ++i) {
      String cid = cfg_.clientId + "-" + String((uint32_t)esp_random(), HEX);
      const bool ok = cfg_.user.length()
        ? mqtt_.connect(cid.c_str(), cfg_.user.c_str(), cfg_.pass.c_str())
        : mqtt_.connect(cid.c_str());
      if (!ok) {
        Serial.print("[GW/MQTT] broker CONNECT failed attempt ");
        Serial.print(i + 1);
        Serial.print("/3 host=");
        Serial.print(cfg_.host);
        Serial.print(" port=");
        Serial.print(cfg_.port);
        Serial.print(" mqtt.state=");
        Serial.print(mqtt_.state());
        Serial.print(" (");
        Serial.print(mqttStateStr(mqtt_.state()));
        Serial.println(")");
        if (i == 0) {
          Serial.print("[GW/MQTT] hint: ESP32_IP=");
          Serial.print(WiFi.localIP());
          Serial.println(
              " — CONNECT_FAILED = TCP could not open (wrong broker IP, port closed, or ESP32 and broker "
              "on different LAN subnets).");
          Serial.println(
              "[GW/MQTT] hint: APP_MQTT_HOST=localhost is only valid on the PC; firmware needs the PC's "
              "IP on the same network as the ESP32. Docker must publish 0.0.0.0:1883 (not host-lo only).");
        }
        delay(100);
        continue;
      }

      if (!mqtt_.subscribe(responsesTopic_.c_str())) {
        Serial.print("[GW/MQTT] ERROR: SUBSCRIBE failed for ");
        Serial.println(responsesTopic_);
        mqtt_.disconnect();
        delay(50);
        continue;
      }
      for (uint8_t k = 0; k < 10; ++k) {
        mqtt_.loop();
        delay(5);
      }
      Serial.print("[GW/MQTT] OK connected node_id=");
      Serial.print(cfg_.nodeId);
      Serial.print(" subscribed ");
      Serial.println(responsesTopic_);
      Serial.print("[GW/MQTT] publish requests on ");
      Serial.println(requestsTopic_);
      return true;
    }

    Serial.println(
        "[GW/MQTT] ERROR: gave up connecting to the broker after 3 attempts. Fix host/port/credentials "
        "or firewall; see mqtt.state codes above.");
    return false;
  }
};

// -------- Gateway config --------
struct MessageGatewayConfig {
  uint32_t    (*timeProviderMs)() = nullptr;
  const char*   esp32Id           = "001";

  MessageGatewayConfig() = default;
  MessageGatewayConfig(uint32_t (*tp)(), const char* id)
  : timeProviderMs(tp), esp32Id(id) {}
};

// -------- Gateway --------
class MessageGateway {
public:
  explicit MessageGateway(std::unique_ptr<TransportMode> transport,
                          MessageGatewayConfig cfg = {})
  : transport_(std::move(transport)), cfg_(cfg) {}

  void begin() {
    if (transport_) transport_->begin();
  }

  // Call from main loop() so MQTT keepalive and incoming messages are processed.
  void loop() {
    if (transport_) transport_->loop();
  }

  // Publishes on the active transport if it supports arbitrary topics (e.g. MQTT).
  bool publishToTopic(const String& topic, const String& payload, bool retain = false) {
    if (!transport_ || !topic.length()) return false;
    return transport_->publishToTopic(topic.c_str(), payload.c_str(), retain);
  }

  void setTransport(std::unique_ptr<TransportMode> transport) {
    transport_ = std::move(transport);
    if (transport_) transport_->begin();
  }

  TransportMode*       transport()       { return transport_.get(); }
  const TransportMode* transport() const { return transport_.get(); }

  void setEnabled(bool e) { enabled_ = e; }
  bool isEnabled()  const { return enabled_; }

  // Same JSON that would be POSTed/published for this UID (for Serial logging).
  String makeTagPayload(const String& uid) const {
    if (transport_ && transport_->isMqttTransport()) {
      const uint32_t ts = cfg_.timeProviderMs ? cfg_.timeProviderMs() : millis();
      return buildMqttPayload(uid, ts);
    }
    return buildLambdaTagBody(uid);
  }

  // Dispatches the right payload shape per transport and sends it.
  bool sendTag(const String& uid) {
    if (!enabled_ || !transport_) return false;
    const uint32_t ts = cfg_.timeProviderMs ? cfg_.timeProviderMs() : millis();
    const String payload = transport_->isMqttTransport()
      ? buildMqttPayload(uid, ts)
      : buildLambdaTagBody(uid);
    return transport_->send(payload);
  }

  bool sendAbsent() {
    if (!enabled_ || !transport_) return false;
    const uint32_t ts = cfg_.timeProviderMs ? cfg_.timeProviderMs() : millis();
    const String payload = transport_->isMqttTransport()
      ? buildMqttAbsentPayload(ts)
      : buildLambdaTagBody("");
    return transport_->send(payload);
  }

  // Periodic telemetry on the same ingest path as tags (HTTP Lambda or MQTT ingest topic).
  // Used when SYSTEM_MODE is SYSTEM_MODE_GATEWAY_INTERVAL (no R200; timed gateway telemetry only).
  bool sendGatewayIntervalStatus(uint32_t uptimeSec, const String& wifiLabel, const String& ip,
                                 uint32_t heap) {
    if (!enabled_) {
      Serial.println("[GW] sendGatewayIntervalStatus skipped: MessageGateway is disabled.");
      return false;
    }
    if (!transport_) {
      Serial.println("[GW] sendGatewayIntervalStatus skipped: no transport (factory/build error).");
      return false;
    }
    const uint32_t tsMs = cfg_.timeProviderMs ? cfg_.timeProviderMs() : millis();
    // Non-empty synthetic tag; suffix is random 0.xxxxxxxx (8 decimals) each ping.
    String intervalTag = "interval-";
    intervalTag += cfg_.esp32Id ? cfg_.esp32Id : "001";
    intervalTag += "-";
    intervalTag += randomUnitFraction8Decimals();

    String p;
    if (transport_->isMqttTransport()) {
      p = "{\"tag\":\"";
      p += intervalTag;
      p += "\",\"ts\":";
      p += String(tsMs);
      p += ",\"esp32_id\":\"";
      p += cfg_.esp32Id ? cfg_.esp32Id : "001";
      p += "\",\"gateway_mode\":\"interval\",\"uptime_s\":";
      p += String(uptimeSec);
      p += ",\"wifi\":\"";
      p += wifiLabel;
      p += "\",\"ip\":\"";
      p += ip;
      p += "\",\"heap\":";
      p += String(heap);
      p += "}";
    } else {
      p = buildLambdaTagBody(intervalTag);
    }
    return transport_->send(p);
  }

private:
  std::unique_ptr<TransportMode> transport_;
  MessageGatewayConfig           cfg_;
  bool                           enabled_ = true;

  // Uniform-ish fraction in [0, 1) as "0.########" (exactly eight digits after the decimal).
  static String randomUnitFraction8Decimals() {
    const uint32_t v = esp_random() % 100000000UL;
    String digits = String(v);
    while (digits.length() < 8) {
      digits = String("0") + digits;
    }
    return String("0.") + digits;
  }

  // Lambda / HTTP ingest: {"tag":"<hex uid>"}  (empty string for absent)
  static String buildLambdaTagBody(const String& uid) {
    String p = "{\"tag\":\"";
    p += uid;
    p += "\"}";
    return p;
  }

  // {"tag_id":"...","ts":...,"esp32_id":"..."}
  String buildMqttPayload(const String& uid, uint32_t tsMs) const {
    String p = "{\"tag_id\":\"";
    p += uid;
    p += "\",\"ts\":";
    p += String(tsMs);
    p += ",\"esp32_id\":\"";
    p += cfg_.esp32Id ? cfg_.esp32Id : "001";
    p += "\"}";
    return p;
  }

  // {"tag_id":"","ts":...,"esp32_id":"...","tag_present":false}
  String buildMqttAbsentPayload(uint32_t tsMs) const {
    String p = "{\"tag_id\":\"\",\"ts\":";
    p += String(tsMs);
    p += ",\"esp32_id\":\"";
    p += cfg_.esp32Id ? cfg_.esp32Id : "001";
    p += "\",\"tag_present\":false}";
    return p;
  }
};
