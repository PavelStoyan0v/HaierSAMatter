// Haier SAMatter – Modbus-controlled heat pump bridge for ESP32-C6
// Adds State (OFF/HEAT/COOL), Mode (QUIET/ECO/TURBO), target temperature,
// and read-only telemetry (twi, two, compressor Hz/target) exposed via Matter.

#include <Adafruit_NeoPixel.h>
#include <Matter.h>
#include <MatterEndpoints/MatterTemperatureControlledCabinet.h>
#include <MatterEndpoints/MatterTemperatureSensor.h>
#include <MatterEndpoints/MatterPressureSensor.h>
#include <ModbusMaster.h>
#include <Preferences.h>
#include <HardwareSerial.h>
#include <vector>

// ----------------------- Hardware mapping -----------------------
constexpr uint8_t RS485_RX_PIN = 17;   // UART RX from RS485
constexpr uint8_t RS485_TX_PIN = 16;   // UART TX to RS485
constexpr uint8_t RELAY_PIN = 2;       // Disconnect relay (isolate remote during writes)
constexpr uint8_t NEOPIXEL_PIN = 8;    // Onboard NeoPixel
constexpr uint8_t NEOPIXEL_COUNT = 1;

// ----------------------- Modbus constants -----------------------
constexpr uint8_t MODBUS_ID = 0x11;
constexpr uint32_t MODBUS_BAUD = 9600;
constexpr uint8_t MODBUS_CFG = SERIAL_8E1;

// Frame signature we wait for before writing (R241 block: 0x11 0x03 0x2C + 44 data + CRC)
constexpr size_t R241_FRAME_BYTES = 49;
constexpr uint8_t R241_BYTE_COUNT = 0x2C;
constexpr uint32_t WRITE_IDLE_GAP_MS = 30;   // Idle gap after frame end before asserting relay
constexpr uint32_t RELAY_SETTLE_MS = 120;    // Settle after relay toggle

// ----------------------- Matter configuration -----------------------
constexpr double TARGET_TEMP_MIN_C = 5.0;
constexpr double TARGET_TEMP_MAX_C = 60.0;
constexpr double TARGET_TEMP_STEP_C = 0.5;
constexpr double TARGET_TEMP_DEFAULT_C = 22.0;

// State and Mode enumerations mapped to compact integer values for Matter number endpoints
enum class PumpState : uint8_t { Off = 0, Heat = 1, Cool = 2 };
enum class PumpMode : uint8_t { Eco = 0, Quiet = 1, Turbo = 2 };

struct Telemetry {
  PumpState state = PumpState::Off;
  PumpMode mode = PumpMode::Eco;
  double setTempC = TARGET_TEMP_DEFAULT_C;
  double twiC = 0.0;
  double twoC = 0.0;
  double compHz = 0.0;
  double compTargetHz = 0.0;
};

struct RegisterBlock {
  uint16_t startAddress;
  uint8_t length;
  uint8_t expectedByteCount;
};

constexpr RegisterBlock BLOCKS[] = {
    {101, 6, 0x0C},  // R101 block
    {141, 16, 0x20}, // R141 block
    {201, 1, 0x02},  // R201 block
    {241, 22, 0x2C}  // R241 block (triggers safe write window)
};

// ----------------------- Globals -----------------------
Preferences matterPref;
HardwareSerial rs485Serial(1);
ModbusMaster modbus;
Adafruit_NeoPixel pixel(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// Matter endpoints (numbers for control, sensors for telemetry)
class NumberEndpoint : public MatterTemperatureControlledCabinet {
public:
  using ChangeCB = std::function<void(double)>;
  void onChange(ChangeCB cb) { callback = cb; }

  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id,
                         esp_matter_attr_val_t *val) override {
    bool ret = MatterTemperatureControlledCabinet::attributeChangeCB(endpoint_id, cluster_id, attribute_id, val);
    if (cluster_id == chip::app::Clusters::TemperatureControl::Id &&
        attribute_id == chip::app::Clusters::TemperatureControl::Attributes::TemperatureSetpoint::Id &&
        callback) {
      callback(static_cast<double>(val->val.i16) / 100.0);
    }
    return ret;
  }

private:
  ChangeCB callback = nullptr;
};

NumberEndpoint TargetTempControl;
NumberEndpoint StateControl;  // OFF/HEAT/COOL -> 0/1/2
NumberEndpoint ModeControl;   // ECO/QUIET/TURBO -> 0/1/2

MatterTemperatureSensor InletTempSensor;   // twi
MatterTemperatureSensor OutletTempSensor;  // two
MatterPressureSensor CompressorHzSensor;   // current Hz
MatterPressureSensor CompressorTargetHzSensor;  // target Hz

// Register cache and tracking
uint16_t holdingRegisters[256] = {0};
bool registersUpdated = false;
Telemetry currentTelemetry;
Telemetry lastConfirmed;  // Used to revert Matter state on failed write

// Frame assembly
std::vector<uint8_t> frameBuf;
size_t expectedFrameLen = 0;
unsigned long lastByteAt = 0;
unsigned long lastSafeFrameAt = 0;
bool safeWindow = false;
bool isWriting = false;

// Pending write request from Matter
enum class WriteKind { None, State, Mode, Temp };
struct PendingWrite {
  WriteKind kind = WriteKind::None;
  double value = 0.0;
};
PendingWrite pending;

// ----------------------- Utility helpers -----------------------
uint8_t encodeState(PumpState state) {
  switch (state) {
    case PumpState::Off: return 0x00;
    case PumpState::Cool: return 0x03;
    case PumpState::Heat: return 0x05;
  }
  return 0x00;
}

PumpState decodeState(uint16_t r101) {
  uint8_t low = r101 & 0xFF;
  if (low == 0x03) return PumpState::Cool;
  if (low == 0x05) return PumpState::Heat;
  return PumpState::Off;
}

PumpMode decodeMode(uint16_t r201) {
  switch (r201) {
    case 0: return PumpMode::Eco;
    case 1: return PumpMode::Quiet;
    case 2: return PumpMode::Turbo;
    default: return PumpMode::Eco;
  }
}

uint16_t encodeMode(PumpMode mode) {
  switch (mode) {
    case PumpMode::Eco: return 0;
    case PumpMode::Quiet: return 1;
    case PumpMode::Turbo: return 2;
  }
  return 0;
}

double decodeSetTemp(uint16_t r102) {
  uint8_t high = (r102 >> 8) & 0xFF;
  return static_cast<double>(high) / 2.0;
}

uint16_t encodeSetTemp(uint16_t baseR102, double tempC) {
  uint8_t low = baseR102 & 0xFF;
  uint8_t high = static_cast<uint8_t>(tempC * 2.0);
  return static_cast<uint16_t>((high << 8) | low);
}

double decodeTwi(uint16_t r146, uint16_t r147) {
  uint8_t lowNibble = r146 & 0x0F;
  uint8_t upper = (r147 >> 8) & 0xFF;
  return static_cast<double>((lowNibble << 8) | upper) / 10.0;
}

double decodeTwo(uint16_t r146, uint16_t r147) {
  uint8_t highNibble = (r146 >> 4) & 0x0F;
  uint8_t low = r147 & 0xFF;
  return static_cast<double>((highNibble << 8) | low) / 10.0;
}

double decodeCompFreq(uint16_t r243) { return static_cast<double>(r243 & 0xFF); }
double decodeCompTargetFreq(uint16_t r244) { return static_cast<double>((r244 >> 8) & 0xFF); }

void blinkFailure() {
  pixel.setPixelColor(0, pixel.Color(255, 0, 0));
  pixel.show();
  delay(150);
  pixel.clear();
  pixel.show();
}

// ----------------------- Register update -----------------------
void applyRegister(uint16_t address, uint16_t value) {
  if (holdingRegisters[address] != value) {
    holdingRegisters[address] = value;
    registersUpdated = true;
  }
}

void updateTelemetryFromRegisters() {
  currentTelemetry.state = decodeState(holdingRegisters[101]);
  currentTelemetry.mode = decodeMode(holdingRegisters[201]);
  currentTelemetry.setTempC = decodeSetTemp(holdingRegisters[102]);
  currentTelemetry.twiC = decodeTwi(holdingRegisters[146], holdingRegisters[147]);
  currentTelemetry.twoC = decodeTwo(holdingRegisters[146], holdingRegisters[147]);
  currentTelemetry.compHz = decodeCompFreq(holdingRegisters[243]);
  currentTelemetry.compTargetHz = decodeCompTargetFreq(holdingRegisters[244]);
}

// ----------------------- Matter sync -----------------------
void publishMatterTelemetry() {
  // Write sensors
  InletTempSensor.setTemperature(currentTelemetry.twiC);
  OutletTempSensor.setTemperature(currentTelemetry.twoC);
  CompressorHzSensor.setPressure(currentTelemetry.compHz);
  CompressorTargetHzSensor.setPressure(currentTelemetry.compTargetHz);

  // Write control numbers to reflect confirmed state
  TargetTempControl.setTemperatureSetpoint(currentTelemetry.setTempC);
  StateControl.setTemperatureSetpoint(static_cast<double>(static_cast<uint8_t>(currentTelemetry.state)));
  ModeControl.setTemperatureSetpoint(static_cast<double>(static_cast<uint8_t>(currentTelemetry.mode)));

  lastConfirmed = currentTelemetry;
}

void revertMatterAttribute(const PendingWrite &req) {
  switch (req.kind) {
    case WriteKind::State:
      StateControl.setTemperatureSetpoint(static_cast<double>(static_cast<uint8_t>(lastConfirmed.state)));
      break;
    case WriteKind::Mode:
      ModeControl.setTemperatureSetpoint(static_cast<double>(static_cast<uint8_t>(lastConfirmed.mode)));
      break;
    case WriteKind::Temp:
      TargetTempControl.setTemperatureSetpoint(lastConfirmed.setTempC);
      break;
    default:
      break;
  }
  blinkFailure();
}

// ----------------------- Modbus parsing -----------------------
void resetFrame() {
  frameBuf.clear();
  expectedFrameLen = 0;
}

void processFrame(const std::vector<uint8_t> &frame) {
  if (frame.size() < 5) return;  // minimal sanity (addr, func, count, data..., crc)
  if (frame[0] != MODBUS_ID || frame[1] != 0x03) return;
  uint8_t byteCount = frame[2];

  // Update safe window if this is the R241 block
  if (byteCount == R241_BYTE_COUNT) {
    safeWindow = true;
    lastSafeFrameAt = millis();
  }

  // Identify block by byte count
  for (const auto &block : BLOCKS) {
    if (block.expectedByteCount != byteCount) continue;
    size_t required = 3 + block.length * 2;
    if (frame.size() < required) continue;
    for (uint8_t i = 0; i < block.length; i++) {
      uint8_t hi = frame[3 + i * 2];
      uint8_t lo = frame[4 + i * 2];
      applyRegister(block.startAddress + i, static_cast<uint16_t>((hi << 8) | lo));
    }
    break;
  }
}

void ingestSerial() {
  while (rs485Serial.available()) {
    uint8_t b = rs485Serial.read();
    lastByteAt = millis();

    if (frameBuf.empty()) {
      if (b == MODBUS_ID) {
        frameBuf.push_back(b);
      }
      continue;
    }

    frameBuf.push_back(b);
    if (frameBuf.size() == 3) {
      expectedFrameLen = 3 + frameBuf[2] + 2;  // +CRC
      if (expectedFrameLen > 200) {
        resetFrame();
      }
    }

    if (expectedFrameLen > 0 && frameBuf.size() >= expectedFrameLen) {
      processFrame(frameBuf);
      resetFrame();
    }
  }
}

// ----------------------- Modbus writes -----------------------
bool readyToWrite() {
  if (!safeWindow) return false;
  if (millis() - lastSafeFrameAt > 3000) {
    safeWindow = false;
    return false;
  }
  return (millis() - lastByteAt) > WRITE_IDLE_GAP_MS;
}

void setRelay(bool enabled) {
  digitalWrite(RELAY_PIN, enabled ? HIGH : LOW);
}

bool writeState(PumpState newState) {
  uint16_t r101 = holdingRegisters[101];
  uint8_t hi = (r101 >> 8) & 0xFF;
  uint8_t lo = encodeState(newState);
  uint16_t regs[6];
  regs[0] = static_cast<uint16_t>((hi << 8) | lo);
  for (int i = 1; i < 6; i++) {
    regs[i] = holdingRegisters[101 + i];
  }
  for (int i = 0; i < 6; i++) modbus.setTransmitBuffer(i, regs[i]);
  uint8_t rc = modbus.writeMultipleRegisters(101, 6);
  if (rc == modbus.ku8MBSuccess) {
    applyRegister(101, regs[0]);
    return true;
  }
  return false;
}

bool writeMode(PumpMode mode) {
  uint16_t value = 256 + encodeMode(mode);
  modbus.setTransmitBuffer(0, value);
  uint8_t rc = modbus.writeMultipleRegisters(201, 1);
  if (rc == modbus.ku8MBSuccess) {
    applyRegister(201, value);
    return true;
  }
  return false;
}

bool writeTemp(double tempC) {
  uint16_t regs[6];
  regs[0] = holdingRegisters[101];
  regs[1] = encodeSetTemp(holdingRegisters[102], tempC);
  for (int i = 2; i < 6; i++) regs[i] = holdingRegisters[101 + i];
  for (int i = 0; i < 6; i++) modbus.setTransmitBuffer(i, regs[i]);
  uint8_t rc = modbus.writeMultipleRegisters(101, 6);
  if (rc == modbus.ku8MBSuccess) {
    applyRegister(102, regs[1]);
    return true;
  }
  return false;
}

bool performWrite(const PendingWrite &req) {
  if (!readyToWrite()) return false;

  isWriting = true;
  safeWindow = false;  // consume the window

  setRelay(true);
  delay(RELAY_SETTLE_MS);
  while (rs485Serial.available()) rs485Serial.read();

  bool ok = false;
  switch (req.kind) {
    case WriteKind::State:
      ok = writeState(static_cast<PumpState>(static_cast<uint8_t>(req.value)));
      break;
    case WriteKind::Mode:
      ok = writeMode(static_cast<PumpMode>(static_cast<uint8_t>(req.value)));
      break;
    case WriteKind::Temp:
      ok = writeTemp(req.value);
      break;
    default:
      break;
  }

  delay(50);
  setRelay(false);
  isWriting = false;

  if (!ok) {
    revertMatterAttribute(req);
  }
  return ok;
}

// ----------------------- Matter callbacks -----------------------
void queueWrite(WriteKind kind, double value) {
  pending.kind = kind;
  pending.value = value;
}

void handleStateChange(double v) { queueWrite(WriteKind::State, v); }
void handleModeChange(double v) { queueWrite(WriteKind::Mode, v); }
void handleTempChange(double v) { queueWrite(WriteKind::Temp, v); }

// ----------------------- Setup & loop -----------------------
void setupModbus() {
  rs485Serial.begin(MODBUS_BAUD, MODBUS_CFG, RS485_RX_PIN, RS485_TX_PIN);
  rs485Serial.setRxBufferSize(1024);
  modbus.begin(MODBUS_ID, rs485Serial);
}

void setupMatter() {
  matterPref.begin("MatterPrefs", false);

  pixel.begin();
  pixel.clear();
  pixel.show();

  // Control endpoints
  TargetTempControl.begin(TARGET_TEMP_DEFAULT_C, TARGET_TEMP_MIN_C, TARGET_TEMP_MAX_C, TARGET_TEMP_STEP_C);
  TargetTempControl.onChange(handleTempChange);

  StateControl.begin(0.0, 0.0, 2.0, 1.0);
  StateControl.onChange(handleStateChange);

  ModeControl.begin(0.0, 0.0, 2.0, 1.0);
  ModeControl.onChange(handleModeChange);

  // Telemetry sensors
  InletTempSensor.begin();
  OutletTempSensor.begin();
  CompressorHzSensor.begin();
  CompressorTargetHzSensor.begin();

  Matter.begin();
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  setRelay(false);

  setupModbus();
  setupMatter();

  // Initialize Matter values to defaults until first Modbus frame arrives
  publishMatterTelemetry();
}

void processPendingWrite() {
  if (pending.kind == WriteKind::None) return;
  if (isWriting) return;
  if (!readyToWrite()) return;

  PendingWrite req = pending;
  pending.kind = WriteKind::None;
  performWrite(req);
}

void loop() {
  ingestSerial();

  if (registersUpdated) {
    registersUpdated = false;
    updateTelemetryFromRegisters();
    publishMatterTelemetry();
  }

  processPendingWrite();
}
