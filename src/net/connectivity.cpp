#include "net/connectivity.h"

#include "secrets.h"

#include <Arduino.h>
#include <WiFi.h>

void connectivitySetup() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi connecting");
  {
    const unsigned long deadline = millis() + 60000UL;
    while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
      delay(300);
      Serial.print(".");
    }
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi OK, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi FAILED (check secrets.h SSID/password)");
  }
}

void connectivityLoop() {
  // Hook for future: reconnect logic, etc.
}
