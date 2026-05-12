#include "MessageGateway.h"

// Static member definition — must live in exactly one translation unit.
Mqtt* Mqtt::callbackTarget_ = nullptr;

void Mqtt::onMessageTrampoline(char* topic, byte* payload, unsigned int len) {
  if (callbackTarget_) callbackTarget_->handleIncomingMessage(topic, payload, len);
}
