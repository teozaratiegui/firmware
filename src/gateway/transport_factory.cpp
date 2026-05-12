#include "gateway/transport_factory.h"

#include "config/app_config.h"
#include "secrets.h"

#include <memory>

#include "MessageGateway.h"

std::unique_ptr<TransportMode> createDefaultTransport() {
#if GATEWAY_USE_MQTT
  Mqtt::Config mc;
  mc.topicPrefix = kMqttTopicPrefix;
  mc.nodeId      = MQTT_NODE_ID;
  mc.host        = kMqttHost;
  mc.port        = kMqttPort;
  mc.clientId    = kMqttClientId;
  mc.retain      = kMqttRetain;
  mc.user        = kMqttUser;
  mc.pass        = kMqttPass;
  return std::unique_ptr<TransportMode>(new Mqtt(mc));
#else
  return std::unique_ptr<TransportMode>(
      new Http(String(GATEWAY_LAMBDA_URL), String(GATEWAY_X_API_KEY)));
#endif
}
