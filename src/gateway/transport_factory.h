#pragma once

#include <memory>

class TransportMode;

/**
 * Builds the uplink selected at build time: MQTT to the Fog gateway
 * (GATEWAY_USE_MQTT=1) or HTTPS straight to the Lambda Function URL (0).
 * The two are not interchangeable at runtime — see documents/ARCHITECTURE.md.
 */
std::unique_ptr<TransportMode> createConfiguredTransport();
