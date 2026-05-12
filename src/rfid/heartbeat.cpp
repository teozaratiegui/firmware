#include "rfid/heartbeat.h"

#include "config/app_config.h"
#include "MessageGateway.h"
#include "rfid/uid_utils.h"

#include <Arduino.h>
#include <WiFi.h>

#include "R200.h"

void logHeartbeat(const R200& rfid, uint32_t nowMs, MessageGateway* msgGw) {
#if SYSTEM_MODE == SYSTEM_MODE_GATEWAY_INTERVAL
  (void)rfid;
  Serial.print("HEARTBEAT uptime_s=");
  Serial.print(nowMs / 1000);
  Serial.print(" WiFi=");
  Serial.print(WiFi.status() == WL_CONNECTED ? "OK" : "off");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(" ip=");
    Serial.print(WiFi.localIP());
  }
  Serial.print(" heap=");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" SYSTEM_MODE_GATEWAY_INTERVAL (timed gateway telemetry; no RFID reader)");
#if MESSAGE_GATEWAY
  if (!msgGw) {
    Serial.println("[GW/interval] ERROR: MessageGateway is null (MESSAGE_GATEWAY misconfigured).");
    return;
  }
  {
    const String wifi = WiFi.status() == WL_CONNECTED ? "OK" : "off";
    const String ip    = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("");
    const bool ok =
        msgGw->sendGatewayIntervalStatus(nowMs / 1000, wifi, ip, ESP.getFreeHeap());
    if (ok) {
      Serial.println("[GW/interval] telemetry send succeeded.");
    } else {
      Serial.println("[GW/interval] telemetry send FAILED.");
#if GATEWAY_USE_MQTT
      Serial.println(
          "       Look at the nearest [GW/MQTT] ERROR lines: broker host/port, Wi-Fi, MQTT "
          "buffer size, or PubSubClient state code.");
#else
      Serial.println(
          "       Look at [GW/HTTP] lines. To use MQTT set GATEWAY_USE_MQTT to 1, run a clean "
          "build, and flash again.");
#endif
    }
  }
#endif
  return;
#endif

  // SYSTEM_MODE_RFID: serial stats only (tags go tag_processing → MessageGateway)
  (void)msgGw;
  Serial.print("HEARTBEAT uptime_s=");
  Serial.print(nowMs / 1000);
  Serial.print(" WiFi=");
  Serial.print(WiFi.status() == WL_CONNECTED ? "OK" : "off");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(" ip=");
    Serial.print(WiFi.localIP());
  }
  Serial.print(" heap=");
  Serial.print(ESP.getFreeHeap());
  Serial.print(" tagPresent=");
  Serial.println(!isZeroUid(rfid.uid) ? 1 : 0);
}
