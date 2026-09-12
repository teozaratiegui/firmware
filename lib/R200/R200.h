#ifndef R200_h
#define R200_h

#include <Arduino.h>
#include <stdint.h>

// Uncomment for verbose frame-level logging on the serial console.
// #define R200_DEBUG

#define RX_BUFFER_LENGTH 256

// -----------------------------------------------------------------------------
//  Driver for the R200 UHF RFID reader over UART.
//
//  Frame layout, both directions:
//      AA | Type | Command | ParamLen (MSB, LSB) | Param... | Checksum | DD
//  Type is 0x00 command, 0x01 response, 0x02 notification. The checksum is the
//  low byte of the sum from Type up to the last parameter (header excluded).
//  Total frame length is therefore ParamLen + 7.
// -----------------------------------------------------------------------------
class R200 {
 public:
  R200();

  /** Last EPC seen, all zeros when no tag is in the field. */
  uint8_t uid[12] = {0};

  bool begin(HardwareSerial* serial = &Serial2, int baud = 115200, uint8_t rxPin = 16,
             uint8_t txPin = 17);

  /** Reads and dispatches whatever the reader has sent. Call every iteration. */
  void loop();

  /** Sends one inventory command. */
  void poll();

  void setMultiplePollingMode(bool enable = true);

  /** Clears the UART RX FIFO (power-on noise, bad framing). */
  void discardRxBuffer();

  void dumpModuleInfo();

  /** Sends GetModuleInfo and waits for a valid frame — proves UART TX and RX. */
  bool linkTest();

  bool dataAvailable() const;

  void dumpUIDToSerial() const;

  // Position of each element in a frame, as an offset from the header.
  enum R200_FrameStructure : uint8_t {
    R200_HeaderPos         = 0x00,
    R200_TypePos           = 0x01,
    R200_CommandPos        = 0x02,
    R200_ParamLengthMSBPos = 0x03,
    R200_ParamLengthLSBPos = 0x04,
    R200_ParamPos          = 0x05,
  };

  enum R200_FrameControl : uint8_t {
    R200_FrameHeader = 0xAA,
    R200_FrameEnd    = 0xDD,
  };

  enum R200_FrameType : uint8_t {
    FrameType_Command      = 0x00,
    FrameType_Response     = 0x01,
    FrameType_Notification = 0x02,
  };

  enum R200_Command : uint8_t {
    CMD_GetModuleInfo                    = 0x03,
    CMD_SinglePollInstruction            = 0x22,
    CMD_MultiplePollInstruction          = 0x27,
    CMD_StopMultiplePoll                 = 0x28,
    CMD_SetSelectParameter               = 0x0C,
    CMD_GetSelectParameter               = 0x0B,
    CMD_SetSendSelectInstruction         = 0x12,
    CMD_ReadLabel                        = 0x39,
    CMD_WriteLabel                       = 0x49,
    CMD_LockLabel                        = 0x82,
    CMD_KillTag                          = 0x65,
    CMD_GetQueryParameters               = 0x0D,
    CMD_SetQueryParameters               = 0x0E,
    CMD_SetWorkArea                      = 0x07,
    CMD_SetWorkingChannel                = 0xAB,
    CMD_GetWorkingChannel                = 0xAA,
    CMD_SetAutoFrequencyHopping          = 0xAD,
    CMD_AcquireTransmitPower             = 0xB7,
    CMD_SetTransmitPower                 = 0xB6,
    CMD_SetTransmitContinuousCarrier     = 0xB0,
    CMD_GetReceiverDemodulatorParameters = 0xF1,
    CMD_SetReceiverDemodulatorParameters = 0xF0,
    CMD_TestRFInputBlockingSignal        = 0xF2,
    CMD_TestChannelRSSI                  = 0xF3,
    CMD_ControlIOPort                    = 0x1A,
    CMD_ModuleSleep                      = 0x17,
    CMD_SetModuleIdleSleepTime           = 0x1D,
    CMD_ExecutionFailure                 = 0xFF,
  };

  enum R200_ErrorCode : uint8_t {
    ERR_CommandError   = 0x17,
    ERR_FHSSFail       = 0x20,
    ERR_InventoryFail  = 0x15,
    ERR_AccessFail     = 0x16,
    ERR_ReadFail       = 0x09,
    ERR_WriteFail      = 0x10,
    ERR_LockFail       = 0x13,
    ERR_KillFail       = 0x12,
  };

 private:
  static constexpr uint8_t  kEpcLength      = 12;
  // Inventory answer: AA 02 22 PL_H PL_L RSSI PC PC EPC(12) CRC CRC CHK DD.
  // The EPC therefore starts at byte 8, after the 5-byte preamble, RSSI and PC.
  // v0.1 read from byte 9 and shifted every EPC it ever reported by one byte.
  static constexpr uint8_t  kEpcOffset      = 8;
  static constexpr uint16_t kMinFrameLength = 7;   // header + 4 + checksum + end

  /** Parameter count declared by the frame, clamped to what actually arrived. */
  uint16_t declaredParamLength() const;
  uint8_t  calculateCheckSum() const;
  bool     frameIsValid() const;
  bool     receiveData(unsigned long timeoutMs = 100);
  bool     readExactly(uint16_t offset, uint16_t count, unsigned long startedAt,
                       unsigned long timeoutMs);
  void     handleFrame();
  uint8_t  flush();

  HardwareSerial* _serial       = nullptr;
  uint8_t         _buffer[RX_BUFFER_LENGTH] = {0};
  uint16_t        _frameLength  = 0;

  static const uint8_t blankUid[kEpcLength];
};
#endif
