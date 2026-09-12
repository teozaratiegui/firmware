#pragma once

#include <Arduino.h>

// -----------------------------------------------------------------------------
//  Wi-Fi station, wall-clock time and the node's stable hardware identity.
// -----------------------------------------------------------------------------
namespace net {

/** Joins the configured network and starts NTP. Blocks up to the Wi-Fi timeout. */
void begin();

/** Non-blocking reconnect; call every superloop iteration. */
void loop();

bool isConnected();

String ipAddress();
int32_t rssi();

/** Canonical MAC, upper case with colons — the identity the gateway registers. */
String macAddress();

/** MAC without separators, for the MQTT client id. */
String macCompact();

/** True once NTP has given the node real wall-clock time. */
bool clockSynced();

/** UTC ISO-8601 (e.g. 2026-09-12T14:03:07Z), or "" while the clock is unset. */
String isoTimestamp();

}  // namespace net
