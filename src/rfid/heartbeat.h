#pragma once

#include <Arduino.h>

class R200;
class MessageGateway;

void logHeartbeat(const R200& rfid, uint32_t nowMs, MessageGateway* msgGw = nullptr);
