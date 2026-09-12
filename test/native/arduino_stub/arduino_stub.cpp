#include "Arduino.h"

#include <algorithm>

unsigned long  g_millis = 0;
HardwareSerial Serial;
HardwareSerial Serial2;

String::String(uint8_t v, int base) {
  char buffer[8];
  std::snprintf(buffer, sizeof(buffer), base == HEX ? "%x" : "%u", v);
  s_ = buffer;
}

void String::toUpperCase() {
  std::transform(s_.begin(), s_.end(), s_.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });
}

bool g_serialMuted = false;

int g_pinMode[64];
int g_pinLevel[64];

void pinMode(uint8_t pin, uint8_t mode) {
  if (pin < 64) g_pinMode[pin] = mode;
}

void digitalWrite(uint8_t pin, uint8_t level) {
  if (pin < 64) g_pinLevel[pin] = level;
}

int digitalRead(uint8_t pin) {
  return pin < 64 ? g_pinLevel[pin] : LOW;
}

void resetPins() {
  for (int i = 0; i < 64; ++i) {
    g_pinMode[i]  = -1;
    g_pinLevel[i] = LOW;
  }
}
