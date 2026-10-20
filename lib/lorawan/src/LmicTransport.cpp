#include "LmicTransport.h"

// clang-format off
#include <lmic.h>
#include <hal/hal.h>
// clang-format on
#include <Arduino.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <string.h>

#include "Log.h"
#include "SystemInfo.h"

// Pin mapping of the LoRa-Bee (RFM95) in XBee socket 1 of the senseBox MCU S2, plan §4.7.1.
// DIO0 and DIO1 share the socket's single INT pin, as in home v1. LMIC polls the pins and reads
// the radio's IRQ register, so the shared pin is fine for it.
const lmic_pinmap lmic_pins = {
    .nss = PIN_XB1_CS,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = PIN_XB1_RESET,
    .dio = {PIN_XB1_INT, PIN_XB1_INT, LMIC_UNUSED_PIN},
    .rxtx_rx_active = 0,
    .rssi_cal = 0,
    .spi_freq = 8000000,
    .pConfig = nullptr,
};

namespace {

constexpr const char* kTag = "lorawan";
constexpr uint32_t kTaskStack = 4096;
constexpr UBaseType_t kTaskPriority = 3;  // above the Arduino loop task (1)
// A TX incl. both RX windows takes < 10 s even at SF12; anything longer means the radio hangs.
constexpr uint32_t kTxTimeoutMs = 30000;
constexpr uint8_t kMaxRadioResets = 3;
// Without a radio, LMIC fails an assert in radio_init() and spins with interrupts off, which
// ends in an interrupt-watchdog reset (and after a few of those in safe mode). So the radio is
// probed first, and while it is missing the probe is repeated now and then.
constexpr uint32_t kRadioProbeIntervalMs = 60000;

// ---- data shared with the LMIC callbacks (written before the task starts, then read-only) ----
uint8_t g_devEui[8];   // LSB first, as LMIC expects
uint8_t g_joinEui[8];  // LSB first
uint8_t g_appKey[16];  // MSB first

// ---- queues between main loop and LMIC task ----
enum class CmdType : uint8_t { Send, ResetRadio };
struct Cmd {
  CmdType type;
  uint8_t port;
  uint8_t len;
  bool requestTime;
  uint8_t data[51];
};

enum class EvtType : uint8_t {
  Joining, Joined, JoinNoAnswer, JoinFailed, TxStart, TxComplete, TxRejected, TimeSync, LinkDead, Reset, RadioMissing
};
struct Evt {
  EvtType type;
  int32_t a;   // TxComplete: rssi; TxStart: SF; TimeSync: GPS seconds; TxRejected: error code
  int32_t b;   // TxComplete: snr; TimeSync: ms since the uplink ended (when the event was created)
  uint32_t c;  // TxComplete: downlink length
  uint64_t uptimeMs;  // TimeSync: 64-bit uptime when the event was created (32-bit wraps after 49 days)
};

QueueHandle_t g_cmdQueue = nullptr;
QueueHandle_t g_evtQueue = nullptr;

void pushEvent(EvtType type, int32_t a = 0, int32_t b = 0, uint32_t c = 0, uint64_t uptimeMs = 0) {
  Evt e{type, a, b, c, uptimeMs};
  xQueueSend(g_evtQueue, &e, 0);  // never block the LMIC task; drop if the main loop lags
}

// Parses 16 hex digits (MSB first) into 8 bytes stored LSB first.
bool parseEuiLsbFirst(const char* hex, uint8_t out[8]) {
  if (strlen(hex) != 16) return false;
  for (int i = 0; i < 8; ++i) {
    char byteStr[3] = {hex[2 * i], hex[2 * i + 1], '\0'};
    char* end = nullptr;
    const long v = strtol(byteStr, &end, 16);
    if (end != byteStr + 2) return false;
    out[7 - i] = static_cast<uint8_t>(v);
  }
  return true;
}

// Resets the SX1276 like LMIC's radio_init() and reads its version register (0x12 for an
// SX1276/RFM95). An empty socket reads 0x00 or 0xFF.
bool radioPresent() {
  constexpr uint8_t kRegVersion = 0x42;
  constexpr uint8_t kSx1276Version = 0x12;
  pinMode(lmic_pins.nss, OUTPUT);
  digitalWrite(lmic_pins.nss, HIGH);
  pinMode(lmic_pins.rst, OUTPUT);
  digitalWrite(lmic_pins.rst, LOW);
  delay(1);
  pinMode(lmic_pins.rst, INPUT);
  delay(5);
  SPI.begin();
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(lmic_pins.nss, LOW);
  SPI.transfer(kRegVersion & 0x7F);  // MSB 0 = read
  const uint8_t version = SPI.transfer(0x00);
  digitalWrite(lmic_pins.nss, HIGH);
  SPI.endTransaction();
  return version == kSx1276Version;
}

// ---- LMIC task: the only code that touches LMIC ----

bool g_radioMissing = false;  // LMIC task only: stack not (re)started, don't run LMIC

void onNetworkTime(void* /*userData*/, int flagSuccess) {
  if (!flagSuccess) return;
  lmic_time_reference_t ref;
  if (!LMIC_getNetworkTimeReference(&ref)) return;
  const ostime_t now = os_getTime();
  const int32_t msSinceUplink = osticks2ms(now - ref.tLocal);
  pushEvent(EvtType::TimeSync, static_cast<int32_t>(ref.tNetwork), msSinceUplink, 0, sysinfo::uptimeMs());
}

void onLmicEvent(void* /*userData*/, ev_t ev) {
  switch (ev) {
    case EV_JOINING: pushEvent(EvtType::Joining); break;
    case EV_JOINED:
      LMIC_setLinkCheckMode(0);
      pushEvent(EvtType::Joined);
      break;
    // One join request without a join-accept (logged). EV_JOIN_FAILED only comes after LMIC
    // went through SF7..SF12 on all three join channels: about 2.5 h (0.1 % duty cycle).
    case EV_JOIN_TXCOMPLETE: pushEvent(EvtType::JoinNoAnswer); break;
    case EV_JOIN_FAILED:
    case EV_REJOIN_FAILED: pushEvent(EvtType::JoinFailed); break;
    case EV_TXSTART: pushEvent(EvtType::TxStart, getSf(LMIC.rps) - SF7 + 7); break;
    case EV_TXCOMPLETE:
      pushEvent(EvtType::TxComplete, LMIC.rssi - RSSI_OFF, LMIC.snr / SNR_SCALEUP, LMIC.dataLen);
      break;
    case EV_TXCANCELED: pushEvent(EvtType::TxRejected, -1); break;
    case EV_LINK_DEAD: pushEvent(EvtType::LinkDead); break;
    case EV_RESET: pushEvent(EvtType::Reset); break;
    default: break;
  }
}

void startStack() {
  os_init_ex(&lmic_pins);
  LMIC_registerEventCb(onLmicEvent, nullptr);
  LMIC_reset();
  // Widen the RX windows by 1 %: common requirement on ESP32 for reliable join-accepts.
  LMIC_setClockError(MAX_CLOCK_ERROR * 1 / 100);
  LMIC_setAdrMode(1);
  LMIC_setLinkCheckMode(0);
  LMIC_startJoining();
}

void powerCycleRadio() {
  digitalWrite(PIN_XB1_ENABLE, HIGH);  // LOW = on
  vTaskDelay(pdMS_TO_TICKS(250));
  digitalWrite(PIN_XB1_ENABLE, LOW);
  vTaskDelay(pdMS_TO_TICKS(100));
}

void handleCommand(const Cmd& cmd) {
  if (cmd.type == CmdType::ResetRadio) {
    powerCycleRadio();
    g_radioMissing = !radioPresent();
    if (g_radioMissing) {
      pushEvent(EvtType::RadioMissing);
    } else {
      startStack();
    }
    return;
  }
  if (g_radioMissing) {
    pushEvent(EvtType::TxRejected, -101);
    return;
  }
  if (LMIC.opmode & OP_TXRXPEND) {
    pushEvent(EvtType::TxRejected, -100);
    return;
  }
  if (cmd.requestTime) LMIC_requestNetworkTime(onNetworkTime, nullptr);
  const lmic_tx_error_t err = LMIC_setTxData2(cmd.port, const_cast<uint8_t*>(cmd.data), cmd.len, 0);
  if (err != LMIC_ERROR_SUCCESS) pushEvent(EvtType::TxRejected, err);
}

void lmicTask(void*) {
  startStack();
  Cmd cmd;
  for (;;) {
    if (xQueueReceive(g_cmdQueue, &cmd, 0) == pdTRUE) handleCommand(cmd);
    if (!g_radioMissing) os_runloop_once();
    vTaskDelay(1);
  }
}

}  // namespace

// LMIC key callbacks (C linkage from lmic.h).
void os_getDevEui(u1_t* buf) { memcpy(buf, g_devEui, 8); }
void os_getArtEui(u1_t* buf) { memcpy(buf, g_joinEui, 8); }
void os_getDevKey(u1_t* buf) { memcpy(buf, g_appKey, 16); }

// ---------------------------------------------------------------------------
// Main-loop side
// ---------------------------------------------------------------------------

LmicTransport::LmicTransport(Clock& clock, HealthRegistry& health)
    : clock_(clock), health_(health), healthId_(health.add("lorawan")) {}

bool LmicTransport::begin(const config::LoRaWanConfig& cfg, const uint8_t appKey[16]) {
  if (started_) return true;
  if (!parseEuiLsbFirst(cfg.devEui, g_devEui) || !parseEuiLsbFirst(cfg.joinEui, g_joinEui)) {
    health_.reportError(healthId_, 1, "DevEUI/JoinEUI not configured");
    LOGE(kTag, "DevEUI/JoinEUI missing or invalid, LoRaWAN disabled");
    return false;
  }
  if (!radioPresent()) {
    health_.reportError(healthId_, 8, "LoRa module not found");
    LOGE(kTag, "no LoRa module in XBee socket 1, LoRaWAN disabled");
    return false;
  }
  memcpy(g_appKey, appKey, 16);

  g_cmdQueue = xQueueCreate(2, sizeof(Cmd));
  g_evtQueue = xQueueCreate(16, sizeof(Evt));
  if (g_cmdQueue == nullptr || g_evtQueue == nullptr ||
      xTaskCreate(lmicTask, "lmic", kTaskStack, nullptr, kTaskPriority, nullptr) != pdPASS) {
    health_.reportError(healthId_, 2, "could not start LMIC task");
    LOGE(kTag, "could not start LMIC task");
    return false;
  }
  started_ = true;
  health_.setStatus(healthId_, ComponentStatus::Degraded);  // until joined
  LOGI(kTag, "started, DevEUI %s, joining", cfg.devEui);
  return true;
}

bool LmicTransport::send(uint8_t port, const uint8_t* data, size_t len, bool requestTime) {
  if (!started_ || !joined_ || busy_ || len > sizeof(Cmd::data)) return false;
  const uint32_t nowMs = millis();
  // Worst case for the budget: assume SF of the last uplink, or SF12 if unknown.
  const uint8_t sf = lastSf_ >= 7 ? lastSf_ : 12;
  const uint32_t cost = airtime::uplinkMs(sf, len, requestTime ? 1 : 0);
  if (!budget_.canSpend(cost, nowMs)) {
    LOGW(kTag, "fair-use budget exhausted (%ld ms left, need %lu ms), skipping",
         static_cast<long>(budget_.availableMs(nowMs)), static_cast<unsigned long>(cost));
    return false;
  }
  Cmd cmd{};
  cmd.type = CmdType::Send;
  cmd.port = port;
  cmd.len = static_cast<uint8_t>(len);
  cmd.requestTime = requestTime;
  memcpy(cmd.data, data, len);
  if (xQueueSend(g_cmdQueue, &cmd, 0) != pdTRUE) return false;
  busy_ = true;
  busySinceMs_ = nowMs;
  pendingLen_ = static_cast<uint8_t>(len);
  pendingTimeReq_ = requestTime;
  return true;
}

void LmicTransport::poll(uint32_t nowMs) {
  if (!started_) return;
  for (int i = 0; i < 8; ++i) {
    if (uxQueueMessagesWaiting(g_evtQueue) == 0) break;
    handleEvent(nowMs);
  }

  // Radio supervision: a TX that never completes means a hung radio.
  if (busy_ && nowMs - busySinceMs_ > kTxTimeoutMs) {
    busy_ = false;
    joined_ = false;
    ++radioResets_;
    health_.reportError(healthId_, 3, "TX timeout, radio reset");
    LOGE(kTag, "no TX complete after %lu s, resetting radio (%u)", static_cast<unsigned long>(kTxTimeoutMs / 1000),
         radioResets_);
    Cmd cmd{};
    cmd.type = CmdType::ResetRadio;
    xQueueSend(g_cmdQueue, &cmd, 0);
    if (radioResets_ > kMaxRadioResets) LOGE(kTag, "radio keeps hanging; check the LoRa-Bee");
  }

  if (radioMissing_ && nowMs - radioMissingSinceMs_ > kRadioProbeIntervalMs) {
    radioMissingSinceMs_ = nowMs;
    Cmd cmd{};
    cmd.type = CmdType::ResetRadio;
    xQueueSend(g_cmdQueue, &cmd, 0);
  }
}

void LmicTransport::handleEvent(uint32_t nowMs) {
  Evt e;
  if (xQueueReceive(g_evtQueue, &e, 0) != pdTRUE) return;
  switch (e.type) {
    case EvtType::RadioMissing:
      if (!radioMissing_) {
        health_.reportError(healthId_, 8, "LoRa module not found");
        LOGE(kTag, "LoRa module gone; probing again every %lu s", static_cast<unsigned long>(kRadioProbeIntervalMs / 1000));
      }
      radioMissing_ = true;
      radioMissingSinceMs_ = nowMs;
      joined_ = false;
      busy_ = false;
      break;
    case EvtType::Joining:
      if (radioMissing_) LOGI(kTag, "LoRa module found again");
      radioMissing_ = false;
      LOGI(kTag, "joining");
      break;
    case EvtType::Joined:
      joined_ = true;
      radioResets_ = 0;
      health_.reportSuccess(healthId_, clock_.nowUtc());
      LOGI(kTag, "joined");
      break;
    case EvtType::JoinNoAnswer:
      // Only logged: the LED keeps showing "connecting" until LMIC gives up on all SFs.
      LOGW(kTag, "no answer to join request %lu (SF%u)", static_cast<unsigned long>(joinAttempts_), lastSf_);
      break;
    case EvtType::JoinFailed:
      joined_ = false;
      health_.reportError(healthId_, 4, "join failed on SF7-SF12, retrying", ComponentStatus::Degraded);
      LOGW(kTag, "join failed on all spreading factors, LMIC keeps trying at SF12");
      break;
    case EvtType::TxStart:
      lastSf_ = static_cast<uint8_t>(e.a);
      if (!joined_) {  // a join request (LMIC also reports EV_TXSTART for those)
        ++joinAttempts_;
        LOGI(kTag, "join request %lu, SF%u", static_cast<unsigned long>(joinAttempts_), lastSf_);
      }
      break;
    case EvtType::TxComplete: {
      busy_ = false;
      ++uplinks_;
      lastRssi_ = static_cast<int16_t>(e.a);
      lastSnr_ = static_cast<int8_t>(e.b);
      if (e.c > 0) ++downlinks_;
      budget_.spend(airtime::uplinkMs(lastSf_ >= 7 ? lastSf_ : 12, pendingLen_, pendingTimeReq_ ? 1 : 0), nowMs);
      health_.reportSuccess(healthId_, clock_.nowUtc());
      LOGI(kTag, "TX complete, SF%u, downlink %lu B, RSSI %d dBm, SNR %d dB", lastSf_,
           static_cast<unsigned long>(e.c), lastRssi_, lastSnr_);
      break;
    }
    case EvtType::TxRejected:
      busy_ = false;
      health_.reportError(healthId_, 5, "uplink rejected by LMIC", ComponentStatus::Degraded);
      LOGW(kTag, "uplink rejected (%ld)", static_cast<long>(e.a));
      break;
    case EvtType::TimeSync: {
      // GPS seconds at the end of the uplink; the uplink ended e.b ms before e.uptimeMs.
      const uint64_t utcMs = Clock::gpsToUnixMs(static_cast<uint64_t>(static_cast<uint32_t>(e.a)) * 1000u);
      const uint64_t atUptimeMs = e.uptimeMs - static_cast<uint64_t>(e.b);
      if (clock_.setUtc(utcMs, atUptimeMs, TimeSource::LoRaWan)) {
        LOGI(kTag, "time synced via DeviceTimeReq: %lu", static_cast<unsigned long>(clock_.nowUtc()));
      } else {
        LOGW(kTag, "implausible network time ignored");
      }
      break;
    }
    case EvtType::LinkDead:
      health_.reportError(healthId_, 6, "link dead (no downlinks)", ComponentStatus::Degraded);
      LOGW(kTag, "link dead");
      break;
    case EvtType::Reset:
      joined_ = false;
      break;
  }
}

LmicTransport::Status LmicTransport::status(uint32_t nowMs) {
  return Status{joined_, joinAttempts_, uplinks_, downlinks_, lastRssi_, lastSnr_, lastSf_, budget_.availableMs(nowMs)};
}
