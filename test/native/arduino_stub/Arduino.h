#pragma once
// -----------------------------------------------------------------------------
//  Minimal host-side stand-in for the Arduino core, so the parts of the firmware
//  that are pure logic (frame decoding, the debounce cache) can be tested on a
//  laptop with nothing but a C++ compiler.
//
//  It is deliberately small: anything that needs Wi-Fi, MQTT or NVS is not
//  testable this way and is exercised on hardware instead.
// -----------------------------------------------------------------------------
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

typedef uint8_t byte;

// ── Clock ────────────────────────────────────────────────────────────────────
// Tests drive it explicitly; yield() nudges it so the driver's timeout loops
// terminate instead of spinning forever on an empty fake UART.
extern unsigned long g_millis;
inline unsigned long millis() { return g_millis; }
inline void delay(unsigned long ms) { g_millis += ms; }
inline void yield() { g_millis += 1; }

// ── Serial noise ─────────────────────────────────────────────────────────────
// The gateway and the registrar log heavily; a test that drives them would bury
// its own output. Tests mute the console around those calls.
extern bool g_serialMuted;

// ── GPIO ─────────────────────────────────────────────────────────────────────
// Enough to assert which pin an access decision drove, and nothing more.
#define INPUT  0x01
#define OUTPUT 0x03
#define LOW    0x0
#define HIGH   0x1

extern int g_pinMode[64];
extern int g_pinLevel[64];

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t level);
int  digitalRead(uint8_t pin);
void resetPins();

#define HEX 16
#define DEC 10
#define SERIAL_8N1 0x800001c

// ── String ───────────────────────────────────────────────────────────────────
class String {
 public:
  String() {}
  String(const char* s) : s_(s ? s : "") {}
  String(const std::string& s) : s_(s) {}
  explicit String(int v) : s_(std::to_string(v)) {}
  explicit String(unsigned long v) : s_(std::to_string(v)) {}
  String(uint8_t v, int base);
  unsigned length() const { return static_cast<unsigned>(s_.size()); }
  bool isEmpty() const { return s_.empty(); }
  const char* c_str() const { return s_.c_str(); }
  void reserve(unsigned n) { s_.reserve(n); }
  void toUpperCase();
  /** Arduino's in-place replace; used to strip the colons out of a MAC. */
  void replace(const char* from, const char* to) {
    if (!from || !*from || !to) return;
    const std::string needle(from);
    const std::string patch(to);
    for (std::size_t at = s_.find(needle); at != std::string::npos;
         at = s_.find(needle, at + patch.size())) {
      s_.replace(at, needle.size(), patch);
    }
  }
  String substring(unsigned from) const {
    return from >= s_.size() ? String() : String(s_.substr(from));
  }
  String& operator+=(char c) { s_ += c; return *this; }
  String& operator+=(const char* c) { s_ += c; return *this; }
  String& operator+=(const String& o) { s_ += o.s_; return *this; }
  // ArduinoJson's writer appends through concat(); the real Arduino String has
  // it, and the codec binary run.sh builds when ArduinoJson is on disk needs it
  // to compile the real GatewayMessages.cpp against ArduinoJson.
  bool concat(char c) { s_ += c; return true; }
  bool concat(const char* c) { if (c) s_ += c; return true; }
  bool concat(const String& o) { s_ += o.s_; return true; }
  bool operator==(const char* o) const { return s_ == o; }
  bool operator==(const String& o) const { return s_ == o.s_; }
  std::string s_;
};

// ── Serial ───────────────────────────────────────────────────────────────────
class Print {
 public:
  void print(const char* v) { if (!g_serialMuted) std::fputs(v, stdout); }
  void print(const String& v) { if (!g_serialMuted) std::fputs(v.c_str(), stdout); }
  void print(char v) { if (!g_serialMuted) std::fputc(v, stdout); }
  void print(int v) { if (!g_serialMuted) std::printf("%d", v); }
  void print(unsigned v) { if (!g_serialMuted) std::printf("%u", v); }
  void print(unsigned long v) { if (!g_serialMuted) std::printf("%lu", v); }
  void print(uint8_t v, int base) {
    if (!g_serialMuted) std::printf(base == HEX ? "%X" : "%u", v);
  }
  void println() { if (!g_serialMuted) std::fputc('\n', stdout); }
  void println(const char* v) { if (!g_serialMuted) std::printf("%s\n", v); }
  void println(const String& v) { if (!g_serialMuted) std::printf("%s\n", v.c_str()); }
  int printf(const char* fmt, ...) {
    if (g_serialMuted) return 0;
    va_list args;
    va_start(args, fmt);
    const int n = std::vprintf(fmt, args);
    va_end(args);
    return n;
  }
};

/** Fake UART: tests push the bytes the reader is supposed to have sent. */
class HardwareSerial : public Print {
 public:
  void begin(unsigned long) {}
  void begin(unsigned long, uint32_t, int8_t, int8_t) {}
  void setRxBufferSize(size_t) {}
  int available() { return static_cast<int>(rx.size()); }
  int read() {
    if (rx.empty()) return -1;
    const uint8_t b = rx.front();
    rx.pop_front();
    return b;
  }
  size_t write(const uint8_t* data, size_t len) {
    tx.insert(tx.end(), data, data + len);
    // Deliver an armed answer, if any: see replyOnWrite().
    if (!reply.empty()) {
      rx.insert(rx.end(), reply.begin(), reply.end());
      reply.clear();
    }
    return len;
  }
  void flush() {}

  void feed(const uint8_t* data, size_t len) { rx.insert(rx.end(), data, data + len); }

  /**
   * Arms a reply to be delivered on the next write().
   *
   * linkTest() drains the RX buffer and *then* waits for an answer to the probe
   * it just sent, so a pre-loaded frame is thrown away before it can be read.
   * This is the only way to stand in for a reader that talks back.
   */
  void replyOnWrite(const uint8_t* data, size_t len) { reply.assign(data, data + len); }

  void reset() { rx.clear(); tx.clear(); reply.clear(); }

  std::deque<uint8_t>  rx;
  std::deque<uint8_t>  tx;
  std::vector<uint8_t> reply;
};

extern HardwareSerial Serial;
extern HardwareSerial Serial2;

// ── String concatenation ─────────────────────────────────────────────────────
// The topic builders compose prefixes with `+`; the real Arduino String has it.
inline String operator+(const String& a, const String& b) { return String(a.s_ + b.s_); }
inline String operator+(const String& a, const char* b) { return String(a.s_ + (b ? b : "")); }
inline String operator+(const char* a, const String& b) { return String((a ? a : "") + b.s_); }
inline bool   operator!=(const String& a, const String& b) { return !(a == b); }
inline bool   operator!=(const String& a, const char* b) { return !(a == b); }
