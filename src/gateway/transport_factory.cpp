#include "gateway/transport_factory.h"

#include "config/app_config.h"
#include "net/connectivity.h"
#include "secrets.h"

#include "HttpTransport.h"
#include "MqttTransport.h"

std::unique_ptr<TransportMode> createConfiguredTransport() {
#if GATEWAY_USE_MQTT
  MqttTransport::Config config;
  config.host = kMqttHost;
  config.port = kMqttPort;
  // Stable per-device client id: with clean_session=false the broker keys the
  // session on this string, so it must survive reboots.
  config.clientId = String(kMqttClientId) + "-" + net::macCompact();
  config.user     = kMqttUser;
  config.pass     = kMqttPass;
  config.retain   = kMqttRetain;
  return std::unique_ptr<TransportMode>(new MqttTransport(config));
#else
  return std::unique_ptr<TransportMode>(
      new HttpTransport(String(GATEWAY_LAMBDA_URL), String(GATEWAY_X_API_KEY)));
#endif
}
