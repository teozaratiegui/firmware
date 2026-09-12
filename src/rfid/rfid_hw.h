#pragma once

class R200;

namespace rfid {

/** Brings up the R200 over Serial2 and, if enabled, proves the UART link. */
void setupReader(R200& reader);

}  // namespace rfid
