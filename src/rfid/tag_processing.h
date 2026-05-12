#pragma once

#include <Arduino.h>

#include "Cache.h"
#include "config/app_config.h"

class R200;
class MessageGateway;

struct TagProcessorState {
  unsigned long lastGarbageLog   = 0;
  unsigned long lastCacheSkipLog = 0;
};

void tagProcessorLoop(R200& rfid, Cache<kTagCacheCapacity>& gate, MessageGateway& msgGw,
                      TagProcessorState& st, uint32_t now);
