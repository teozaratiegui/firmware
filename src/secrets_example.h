#pragma once
#define WIFI_SSID        "TU_SSID"
#define WIFI_PASS        "TU_PASS"

// Lambda Function URL must use https:// (http:// causes "plain HTTP request was sent to HTTPS port").
// JSON body is {"tag":"<hex>"} — empty tag for absent / interval pings.
#define GATEWAY_LAMBDA_URL   "https://your-api.lambda-url.region.on.aws/"
#define GATEWAY_X_API_KEY    "your-api-key"
