#include "HttpTransport.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <ctype.h>

namespace {

// The Lambda answers 201 on first sight of a tag and 200 afterwards; 204 is the
// documented "allowed, no body" case. All three mean the event was accepted.
bool isAccepted(int code) {
  return code == 200 || code == 201 || code == 204;
}

}  // namespace

HttpTransport::HttpTransport(String url, String apiKey, String bearer)
    : url_(std::move(url)), apiKey_(std::move(apiKey)), bearer_(std::move(bearer)) {}

bool HttpTransport::begin() {
  if (normalisedUrl().isEmpty()) {
    Serial.println("[GW/HTTP] ERROR: GATEWAY_LAMBDA_URL is empty — uplink disabled.");
    return false;
  }
  return true;
}

bool HttpTransport::isConnected() {
  return WiFi.status() == WL_CONNECTED;
}

String HttpTransport::normalisedUrl() const {
  String url = url_;
  url.trim();
  if (url.isEmpty()) return url;

  // Lower-case the scheme so "HTTPS://" still takes the TLS branch.
  const int colon = url.indexOf(':');
  for (int i = 0; i < colon; ++i) {
    url.setCharAt(i, static_cast<char>(tolower(static_cast<unsigned char>(url.charAt(i)))));
  }
  // Function URLs are HTTPS-only; plain http:// gets a TLS mismatch from AWS.
  if (url.startsWith("http://") && url.indexOf(".lambda-url.") >= 0) {
    url = "https://" + url.substring(7);
  }
  return url;
}

bool HttpTransport::sendUplink(const String& payload) {
  const String url = normalisedUrl();
  if (url.isEmpty()) {
    Serial.println("[GW/HTTP] ERROR: empty URL.");
    return false;
  }
  if (!isConnected()) {
    Serial.println("[GW/HTTP] ERROR: Wi-Fi down, POST skipped.");
    return false;
  }

  const bool useTls = url.startsWith("https://");
  HTTPClient       http;
  WiFiClientSecure tls;

  bool opened;
  if (useTls) {
    tls.setInsecure();  // no certificate validation — bench only
    opened = http.begin(tls, url);
  } else {
    opened = http.begin(url);
  }
  if (!opened) {
    Serial.println("[GW/HTTP] ERROR: http.begin() failed (bad URL or TLS setup).");
    http.end();
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  if (!apiKey_.isEmpty()) http.addHeader("x-api-key", apiKey_);
  if (!bearer_.isEmpty()) http.addHeader("Authorization", "Bearer " + bearer_);

  lastStatus_ = http.POST(payload);
  Serial.printf("[GW/HTTP] POST %d bytes -> status %d\n", payload.length(), lastStatus_);

  if (lastStatus_ < 0) {
    Serial.printf("[GW/HTTP] transport error: %s\n", http.errorToString(lastStatus_).c_str());
  } else if (!isAccepted(lastStatus_)) {
    const String body = http.getString();
    if (!body.isEmpty()) {
      Serial.print("[GW/HTTP] body: ");
      Serial.println(body.length() > 512 ? body.substring(0, 512) + "..." : body);
    }
  }

  http.end();
  return isAccepted(lastStatus_);
}
