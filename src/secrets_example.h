#pragma once
// Copy this file to src/secrets.h and fill it in. secrets.h is gitignored.

// ── Wi-Fi ────────────────────────────────────────────────────────────────────
#define WIFI_SSID "YOUR_SSID"
#define WIFI_PASS "YOUR_PASSWORD"

// ── Fog gateway (GATEWAY_USE_MQTT = 1) ───────────────────────────────────────
// Shared key the gateway checks before it issues node credentials. It has to
// match NODE_API_KEY in the gateway's .env — if that variable is unset there,
// the gateway registers anyone (gateway finding G9).
#define NODE_API_KEY "shared-registration-key"

// ── Cloud, direct mode (GATEWAY_USE_MQTT = 0) ────────────────────────────────
// Only used by the A/B bench path that bypasses the Fog layer.
// The Function URL is HTTPS-only: http:// gets a TLS mismatch from AWS.
#define GATEWAY_LAMBDA_URL "https://your-function-url.lambda-url.sa-east-1.on.aws/"
#define GATEWAY_X_API_KEY  "your-api-key"
