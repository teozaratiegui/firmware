#pragma once
#include <Arduino.h>
#include <string.h>

#ifndef UID_LEN
#define UID_LEN 12
#endif

// Caché por UID: acepta si el UID es nuevo o si pasó el cooldown desde su última aceptación.
template<uint8_t CAPACITY = 16>
class Cache {
public:
  explicit Cache(uint32_t cooldownMs) : cooldownMs_(cooldownMs) {
    clear();
  }

  // Devuelve true si conviene aceptar (UID nuevo o UID con cooldown cumplido).
  bool shouldAccept(const uint8_t* uid, uint32_t nowMs) {
    int idx = find(uid);
    if (idx < 0) {
      // UID nunca visto -> aceptar y recordar ahora
      remember(uid, nowMs);
      return true;
    }
    // UID conocido: chequear cooldown individual
    if (nowMs - entries_[idx].lastAcceptedAt >= cooldownMs_) {
      entries_[idx].lastAcceptedAt = nowMs;
      return true;
    }
    return false; // aún dentro del cooldown
  }

  void clear() {
    for (uint8_t i = 0; i < CAPACITY; ++i) {
      entries_[i].valid = false;
      entries_[i].lastAcceptedAt = 0;
      memset(entries_[i].uid, 0, UID_LEN);
    }
  }

private:
  struct Entry {
    bool valid;
    uint8_t uid[UID_LEN];
    uint32_t lastAcceptedAt;
  };

  Entry entries_[CAPACITY];
  uint32_t cooldownMs_;

  static bool sameUid(const uint8_t* a, const uint8_t* b) {
    return memcmp(a, b, UID_LEN) == 0;
  }

  int find(const uint8_t* uid) const {
    for (uint8_t i = 0; i < CAPACITY; ++i) {
      if (entries_[i].valid && sameUid(entries_[i].uid, uid)) return i;
    }
    return -1;
  }

  void remember(const uint8_t* uid, uint32_t nowMs) {
    // Busca libre
    for (uint8_t i = 0; i < CAPACITY; ++i) {
      if (!entries_[i].valid) {
        entries_[i].valid = true;
        memcpy(entries_[i].uid, uid, UID_LEN);
        entries_[i].lastAcceptedAt = nowMs;
        return;
      }
    }
    // Si está lleno, reemplaza el más antiguo (LRU simple por timestamp)
    uint8_t oldest = 0;
    uint32_t oldestTs = entries_[0].lastAcceptedAt;
    for (uint8_t i = 1; i < CAPACITY; ++i) {
      if (entries_[i].lastAcceptedAt < oldestTs) { oldest = i; oldestTs = entries_[i].lastAcceptedAt; }
    }
    entries_[oldest].valid = true;
    memcpy(entries_[oldest].uid, uid, UID_LEN);
    entries_[oldest].lastAcceptedAt = nowMs;
  }
};
