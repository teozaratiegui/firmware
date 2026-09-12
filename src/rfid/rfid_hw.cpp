#include "rfid/rfid_hw.h"

#include "config/app_config.h"

#include <Arduino.h>

#include "R200.h"

namespace rfid {

void setupReader(R200& reader) {
  Serial2.setRxBufferSize(2048);
  reader.begin(&Serial2, R200_BAUD, R200_RX_PIN, R200_TX_PIN);
  reader.discardRxBuffer();
  reader.setMultiplePollingMode(false);
  delay(80);
  reader.discardRxBuffer();

#if R200_LINK_TEST
  const bool linkOk = reader.linkTest();
  Serial.printf("[RFID] UART link test: %s\n",
                linkOk ? "PASS — the ESP32 is talking to the R200."
                       : "FAIL — check crossed RX/TX, common GND, baud rate and R200 power.");
#endif

#if USE_CONTINUOUS_POLL
  // Multi-poll runs a finite counter (0xFFFF) and is never re-armed: it stops
  // on its own after ~65k inventories. Single poll is the supported mode.
  reader.setMultiplePollingMode(true);
#endif

  Serial.printf("[RFID] R200 on Serial2 RX=GPIO%d TX=GPIO%d @ %d baud\n", R200_RX_PIN, R200_TX_PIN,
                R200_BAUD);
  reader.dumpModuleInfo();
  for (uint8_t i = 0; i < 20; ++i) {
    delay(5);
    reader.loop();
  }
}

}  // namespace rfid
