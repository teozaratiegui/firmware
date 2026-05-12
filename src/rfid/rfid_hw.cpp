#include "rfid/rfid_hw.h"

#include "config/app_config.h"

#include <Arduino.h>
#include <SPI.h>

#include "R200.h"

void setupR200Module(R200& rfid) {
  Serial2.setRxBufferSize(2048);
  rfid.begin(&Serial2, R200_BAUD, R200_RX_PIN, R200_TX_PIN);
  rfid.discardRxBuffer();
  rfid.setMultiplePollingMode(false);
  delay(80);
  rfid.discardRxBuffer();

#if R200_LINK_TEST
  {
    const bool r200UartOk = rfid.linkTest();
    Serial.print("R200 link test (GetModuleInfo, CRC OK): ");
    Serial.println(
        r200UartOk ? "PASS — ESP TX/RX hablando con el R200." : "FAIL — revisá GPIO RX/TX cruzados, GND, baud, VCC.");
  }
#endif

#if USE_CONTINUOUS_POLL
  rfid.setMultiplePollingMode(true);
#endif

  Serial.print("R200 UART: RX=GPIO");
  Serial.print(R200_RX_PIN);
  Serial.print(" TX=GPIO");
  Serial.print(R200_TX_PIN);
  Serial.println(" (si no hay tags: cable ESP RX→TX del R200; o probá RX=16 TX=17 en app_config.h)");
  Serial.println("RFID inicializado");
  rfid.dumpModuleInfo();
  for (uint8_t i = 0; i < 20; i++) {
    delay(5);
    rfid.loop();
  }
}
