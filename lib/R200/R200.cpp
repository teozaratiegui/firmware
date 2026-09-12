#include "R200.h"

const uint8_t R200::blankUid[R200::kEpcLength] = {0};

R200::R200() = default;

bool R200::begin(HardwareSerial* serial, int baud, uint8_t rxPin, uint8_t txPin) {
  _serial = serial;
  _serial->begin(baud, SERIAL_8N1, rxPin, txPin);
  return true;
}

void R200::discardRxBuffer() {
  (void)flush();
}

uint8_t R200::flush() {
  uint8_t discarded = 0;
  while (_serial && _serial->available()) {
    _serial->read();
    if (discarded < 255) discarded++;
  }
  return discarded;
}

bool R200::dataAvailable() const {
  return _serial && _serial->available() > 0;
}

// -----------------------------------------------------------------------------
//  Frame reception
// -----------------------------------------------------------------------------

// The declared parameter count comes off the wire, so everything derived from it
// (checksum span, EPC offset) is validated against what actually arrived before
// it is used as an index.
uint16_t R200::declaredParamLength() const {
  if (_frameLength < kMinFrameLength) return 0;
  const uint16_t declared =
      static_cast<uint16_t>(_buffer[R200_ParamLengthMSBPos]) << 8 | _buffer[R200_ParamLengthLSBPos];
  const uint16_t arrived = _frameLength - kMinFrameLength;
  return declared <= arrived ? declared : arrived;
}

uint8_t R200::calculateCheckSum() const {
  // Sum runs from Type up to the last parameter — frame header, checksum and
  // frame end excluded. In v0.1 the upper bound was `paramLength + 4 + 1` over
  // the *unvalidated* declared length: a frame claiming 0xFFxx parameters
  // wrapped the uint16_t index and spun the loop forever, with no watchdog
  // configured to recover from it.
  const uint16_t paramLength = declaredParamLength();
  uint16_t       sum         = 0;
  for (uint16_t i = R200_TypePos; i < R200_ParamPos + paramLength; ++i) {
    sum += _buffer[i];
  }
  return static_cast<uint8_t>(sum & 0xFF);
}

bool R200::frameIsValid() const {
  if (_frameLength < kMinFrameLength) return false;
  if (_buffer[R200_HeaderPos] != R200_FrameHeader) return false;
  if (_buffer[_frameLength - 1] != R200_FrameEnd) return false;

  const uint16_t paramLength = declaredParamLength();
  // A truncated frame declares more parameters than it delivered.
  if (static_cast<uint16_t>(paramLength + kMinFrameLength) != _frameLength) return false;

  return _buffer[R200_ParamPos + paramLength] == calculateCheckSum();
}

/** Reads `count` bytes into the buffer at `offset`, or false if the clock runs out. */
bool R200::readExactly(uint16_t offset, uint16_t count, unsigned long startedAt,
                       unsigned long timeoutMs) {
  uint16_t read = 0;
  while (read < count) {
    if (millis() - startedAt >= timeoutMs) return false;
    if (!_serial->available()) {
      yield();  // 115200 baud leaves gaps between bytes; do not starve the RTOS
      continue;
    }
    _buffer[offset + read] = static_cast<uint8_t>(_serial->read());
    read++;
  }
  return true;
}

// Reads one frame, driven by the declared length rather than by scanning for the
// frame-end byte. v0.1 stopped at the first 0xDD, which truncates any frame
// whose EPC or payload happens to contain that byte; and it always burned the
// whole timeout (a fixed 100 ms of busy-wait per call) because the `break` only
// left the inner loop, which made it the dominant term of the superloop period.
bool R200::receiveData(unsigned long timeoutMs) {
  if (!_serial) return false;

  const unsigned long startedAt = millis();
  _frameLength                  = 0;
  memset(_buffer, 0, sizeof(_buffer));

  // 1. Resynchronise on the frame header, discarding leading noise.
  bool synced = false;
  while (millis() - startedAt < timeoutMs) {
    if (!_serial->available()) {
      yield();
      continue;
    }
    if (static_cast<uint8_t>(_serial->read()) == R200_FrameHeader) {
      synced = true;
      break;
    }
  }
  if (!synced) return false;
  _buffer[R200_HeaderPos] = R200_FrameHeader;

  // 2. Preamble: type, command and the 2-byte parameter length.
  if (!readExactly(1, 4, startedAt, timeoutMs)) return false;

  const uint16_t paramLength =
      static_cast<uint16_t>(_buffer[R200_ParamLengthMSBPos]) << 8 | _buffer[R200_ParamLengthLSBPos];
  if (static_cast<uint32_t>(paramLength) + kMinFrameLength > RX_BUFFER_LENGTH) {
    Serial.printf("[R200] frame declares %u parameters, buffer holds %d — discarded.\n",
                  static_cast<unsigned>(paramLength), RX_BUFFER_LENGTH - kMinFrameLength);
    flush();
    return false;
  }

  // 3. Parameters plus checksum and frame end.
  if (!readExactly(R200_ParamPos, paramLength + 2, startedAt, timeoutMs)) return false;

  _frameLength = paramLength + kMinFrameLength;
  return true;
}

// -----------------------------------------------------------------------------
//  Frame dispatch
// -----------------------------------------------------------------------------

void R200::loop() {
  if (!dataAvailable()) return;
  if (!receiveData(100)) return;
  if (!frameIsValid()) return;
  handleFrame();
}

void R200::handleFrame() {
  switch (_buffer[R200_CommandPos]) {
    case CMD_GetModuleInfo: {
      const uint16_t paramLength = declaredParamLength();
      // Parameters are [type byte][ASCII text]; print the text only.
      for (uint16_t i = 1; i < paramLength; ++i) {
        Serial.print(static_cast<char>(_buffer[R200_ParamPos + i]));
      }
      Serial.println();
      break;
    }

    case CMD_SinglePollInstruction:
    case CMD_MultiplePollInstruction: {
      // Response: RSSI(1) PC(2) EPC(12) CRC(2) — 17 parameter bytes. The guard
      // used to ask for 15, the EPC alone, contradicting the line above it: a
      // frame declaring 15 or 16 passed and produced a UID whose tail came out
      // of the CRC slot. Now it asks for the whole answer.
      if (declaredParamLength() < kInventoryParamLength) break;
      if (memcmp(uid, &_buffer[kEpcOffset], kEpcLength) != 0) {
        memcpy(uid, &_buffer[kEpcOffset], kEpcLength);
#ifdef R200_DEBUG
        Serial.print("[R200] new tag ");
        dumpUIDToSerial();
        Serial.println();
#endif
      }
      break;
    }

    case CMD_ExecutionFailure: {
      if (declaredParamLength() < 1) break;
      const uint8_t reason = _buffer[R200_ParamPos];
      // "Inventory fail" is the normal answer when the field is empty.
      if (reason == ERR_InventoryFail) {
        if (memcmp(uid, blankUid, kEpcLength) != 0) {
          memset(uid, 0, kEpcLength);
#ifdef R200_DEBUG
          Serial.println("[R200] tag left the field");
#endif
        }
      } else if (reason == ERR_CommandError) {
        Serial.println("[R200] command error");
      }
      break;
    }

    default:
      break;
  }
}

void R200::dumpUIDToSerial() const {
  Serial.print("0x");
  for (uint8_t i = 0; i < kEpcLength; ++i) {
    if (uid[i] < 0x10) Serial.print('0');
    Serial.print(uid[i], HEX);
  }
}

// -----------------------------------------------------------------------------
//  Commands
// -----------------------------------------------------------------------------

void R200::dumpModuleInfo() {
  if (!_serial) return;
  static const uint8_t frame[8] = {
      R200_FrameHeader, FrameType_Command, CMD_GetModuleInfo, 0x00, 0x01, 0x00, 0x04,
      R200_FrameEnd,
  };
  _serial->write(frame, sizeof(frame));
}

bool R200::linkTest() {
  if (!_serial) return false;
  static const uint8_t frame[8] = {
      R200_FrameHeader, FrameType_Command, CMD_GetModuleInfo, 0x00, 0x01, 0x00, 0x04,
      R200_FrameEnd,
  };

  for (uint8_t attempt = 0; attempt < 2; ++attempt) {
    discardRxBuffer();
    if (attempt) delay(60);
    _serial->write(frame, sizeof(frame));
    _serial->flush();

    const unsigned long deadline = millis() + 350;
    while (static_cast<int32_t>(millis() - deadline) < 0) {
      if (!dataAvailable()) {
        delay(3);
        continue;
      }
      if (receiveData(220) && frameIsValid()) return true;
      discardRxBuffer();
    }
  }
  return false;
}

void R200::poll() {
  if (!_serial) return;
  static const uint8_t frame[7] = {
      R200_FrameHeader, FrameType_Command, CMD_SinglePollInstruction, 0x00, 0x00, 0x22,
      R200_FrameEnd,
  };
  _serial->write(frame, sizeof(frame));
}

void R200::setMultiplePollingMode(bool enable) {
  if (!_serial) return;

  if (enable) {
    // 0xFFFF inventories, then the reader stops on its own — nothing re-arms it.
    static const uint8_t frame[10] = {
        R200_FrameHeader, FrameType_Command, CMD_MultiplePollInstruction, 0x00, 0x03,
        0x22,             0xFF,              0xFF,                        0x4A, R200_FrameEnd,
    };
    _serial->write(frame, sizeof(frame));
    return;
  }

  static const uint8_t frame[7] = {
      R200_FrameHeader, FrameType_Command, CMD_StopMultiplePoll, 0x00, 0x00, 0x28, R200_FrameEnd,
  };
  _serial->write(frame, sizeof(frame));
}
