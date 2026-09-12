#include "net/connectivity.h"

#include "config/app_config.h"
#include "secrets.h"

#include <WiFi.h>
#include <time.h>

namespace net {
namespace {

uint32_t lastRetryMs = 0;
bool     linkUp      = false;

// Anything before 2021 means SNTP has not answered yet, so millis()-relative
// timestamps must not be passed off as wall-clock time.
constexpr time_t kSaneEpoch = 1609459200;  // 2021-01-01T00:00:00Z

void startNtp() {
  configTime(0, 0, kNtpServer);  // UTC; the Cloud stores ISO-8601 in UTC
}

// SNTP is armed on every Wi-Fi rising edge, not once at boot.
//
// It used to be started only from begin(), inside the `connected` branch. A node
// that booted before its access point — the normal order after a power cut —
// therefore never called configTime() at all, so clockSynced() stayed false for
// the whole uptime and every tag read went out with no `ts` at all. Silently:
// the only symptom is `clock=unset` on a serial console a door-mounted node does
// not have. Re-arming here also shortens recovery after a long outage, since
// esp-idf's own SNTP retry interval is an hour.
void onLinkUp() {
  Serial.printf("[NET] Wi-Fi up  ip=%s  rssi=%d dBm  mac=%s\n", ipAddress().c_str(),
                static_cast<int>(rssi()), macAddress().c_str());
  startNtp();
}

}  // namespace

void begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("[NET] connecting to Wi-Fi");
  const uint32_t deadline = millis() + kWifiConnectTimeoutMs;
  while (WiFi.status() != WL_CONNECTED && static_cast<int32_t>(millis() - deadline) < 0) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (isConnected()) {
    linkUp = true;
    onLinkUp();
  } else {
    Serial.println("[NET] Wi-Fi FAILED — check WIFI_SSID / WIFI_PASS in secrets.h.");
  }
}

void loop() {
  if (isConnected()) {
    // The rising edge, which polling isConnected() cannot see. WiFi.setAutoReconnect
    // means the link can also come back without this function asking for it, so
    // the edge is tracked here rather than next to the reconnect attempt below.
    if (!linkUp) {
      linkUp = true;
      onLinkUp();
    }
    return;
  }
  linkUp = false;

  const uint32_t now = millis();
  if (now - lastRetryMs < kWifiRetryIntervalMs) return;
  lastRetryMs = now;

  Serial.println("[NET] Wi-Fi down — reconnecting.");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

bool isConnected() {
  return WiFi.status() == WL_CONNECTED;
}

String ipAddress() {
  return isConnected() ? WiFi.localIP().toString() : String();
}

int32_t rssi() {
  return isConnected() ? WiFi.RSSI() : 0;
}

String macAddress() {
  String mac = WiFi.macAddress();
  mac.toUpperCase();
  return mac;
}

String macCompact() {
  String mac = macAddress();
  mac.replace(":", "");
  return mac;
}

bool clockSynced() {
  return time(nullptr) > kSaneEpoch;
}

String isoTimestamp() {
  if (!clockSynced()) return String();

  const time_t nowSeconds = time(nullptr);
  struct tm utc;
  gmtime_r(&nowSeconds, &utc);

  char buffer[24];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return String(buffer);
}

}  // namespace net
