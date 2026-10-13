// senseBox:home v2 firmware - composition root.
//
// This file only wires the modules together; it contains no sensor- or target-specific logic.
// Architecture: implementation-docs/IMPLEMENTATION_PLAN.md §4.

#include <Arduino.h>
#include <string.h>

#include "App.h"
#include "Board.h"
#include "BootGuard.h"
#include "ButtonDetector.h"
#include "CayenneUplink.h"
#include "Clock.h"
#include "ConfigCodec.h"
#include "Drivers.h"
#include "Health.h"
#include "LmicTransport.h"
#include "Log.h"
#include "PersistentStore.h"
#include "Pipeline.h"
#include "SensorManager.h"
#include "SerialConsole.h"
#include "StationConfig.h"
#include "StatusLed.h"
#include "SystemInfo.h"
#include "Version.h"

#if SBH_DEV_BUILD
// config/olpe-template.json, embedded by board_build.embed_txtfiles (dev builds only).
extern const char olpeTemplateJson[] asm("_binary_config_olpe_template_json_start");
#endif

namespace {

constexpr const char* kTag = "main";
constexpr uint32_t kLoopDelayMs = 10;
constexpr uint32_t kStatusLogIntervalMs = 5u * 60u * 1000u;

uint64_t uptimeMs() { return sysinfo::uptimeMs(); }
uint32_t uptimeS() { return static_cast<uint32_t>(sysinfo::uptimeMs() / 1000); }

class SerialLogSink : public LogSink {
 public:
  void write(const LogLine& line) override {
    Serial.printf("[%7lu] %c %-8s %s\n", static_cast<unsigned long>(line.uptimeS), logger::levelLetter(line.level),
                  line.tag, line.msg);
  }
};

// Logs every measurement (debug) and every window mean (info). Replaced by SD storage in phase 3.
class MeasurementLog : public MeasurementSink {
 public:
  void onMeasurement(const Measurement& m) override {
    if (m.kind == ValueKind::Aggregated) {
      LOGI("data", "slot %u %-17s mean %8.2f %s q=0x%03x", m.sensorSlot, phenomenonName(m.phenomenon), m.value,
           unitSymbol(m.unit), m.quality);
    } else {
      LOGD("data", "slot %u %-17s %-9s %8.2f %s q=0x%03x", m.sensorSlot, phenomenonName(m.phenomenon),
           valueKindName(m.kind), m.value, unitSymbol(m.unit), m.quality);
    }
  }
};

SerialLogSink g_serialLog;
MeasurementLog g_measurementLog;

Clock g_clock(uptimeMs);
HealthRegistry g_health(uptimeS);
config::StationConfig g_config;
Pipeline g_pipeline(g_config);
SensorManager g_sensors(g_clock, g_health, g_pipeline);
LmicTransport g_lora(g_clock, g_health);
CayenneUplink* g_cayenne = nullptr;  // created at setup if configured

StatusLed g_led;
ButtonDetector g_button;
BootGuard g_bootGuard;
App g_app{g_clock, g_health, g_config, g_sensors, g_lora, false};
SerialConsole g_console(g_app);

HealthRegistry::Id g_configHealth = HealthRegistry::kInvalid;
uint32_t g_lastStatusLogMs = 0;
char g_jsonBuf[4096];

void loadConfig() {
  g_configHealth = g_health.add("config");
  config::setDefaults(g_config);

  size_t len = persist::loadConfigJson(g_jsonBuf, sizeof(g_jsonBuf));
  const char* source = "nvs";
#if SBH_DEV_BUILD
  if (len == 0) {
    strncpy(g_jsonBuf, olpeTemplateJson, sizeof(g_jsonBuf) - 1);
    len = strlen(g_jsonBuf);
    source = "built-in Olpe template (dev build)";
  }
#endif
  if (len == 0) {
    g_health.reportError(g_configHealth, 1, "no configuration stored", ComponentStatus::Degraded);
    LOGW(kTag, "no configuration stored: station is unconfigured");
    return;
  }

  const config::DecodeResult r = config::decode(g_jsonBuf, len, g_config);
  if (!r.ok) {
    // Never delete a config we can't read: keep it as backup, run unconfigured.
    persist::backupConfigJson(g_jsonBuf);
    g_health.reportError(g_configHealth, 2, r.error);
    LOGE(kTag, "config from %s rejected: %s (kept as backup)", source, r.error);
    return;
  }
  if (r.migrated) {
    persist::backupConfigJson(g_jsonBuf);
    if (config::encode(g_config, g_jsonBuf, sizeof(g_jsonBuf)) > 0) persist::saveConfigJson(g_jsonBuf);
    LOGI(kTag, "config migrated from schema %u to %u", r.fromVersion, g_config.schemaVersion);
  }
  g_health.reportSuccess(g_configHealth);
  LOGI(kTag, "config '%s' loaded from %s: %u sensor(s), %u uplink(s)", g_config.name, source, g_config.sensorCount,
       g_config.uplinkCount);
}

void logSensorBus() {
  uint8_t found[16];
  const size_t n = board::scanSensorBus(found, sizeof(found));
  char list[16 * 6] = {};
  size_t pos = 0;
  for (size_t i = 0; i < n && pos + 6 < sizeof(list); ++i) pos += snprintf(list + pos, sizeof(list) - pos, " 0x%02x", found[i]);
  LOGI(kTag, "sensor bus: %u device(s):%s", static_cast<unsigned>(n), n > 0 ? list : " none");
}

void startUplinks() {
  for (uint8_t i = 0; i < g_config.uplinkCount; ++i) {
    const config::UplinkConfig& u = g_config.uplinks[i];
    if (u.type != config::UplinkType::LoRaWanCayenne || g_cayenne != nullptr) continue;

    uint8_t appKey[16];
    if (persist::readSecret(persist::secret::kLoRaAppKey, appKey, sizeof(appKey)) == sizeof(appKey)) {
      g_lora.begin(u.lorawan, appKey);
    } else {
      LOGW(kTag, "LoRaWAN AppKey not set: frames are built and logged, but not sent");
    }
    memset(appKey, 0, sizeof(appKey));

    g_cayenne = new CayenneUplink(g_config, u.lorawan, g_clock, g_health, g_lora);
    g_cayenne->setAggregateSink(&g_measurementLog);
    g_cayenne->begin(millis());
    g_pipeline.addSink(g_cayenne);
  }
  if (g_config.uplinkCount == 0) LOGI(kTag, "no data target configured: measuring only");
}

void periodicStatusLog(uint32_t nowMs) {
  if (nowMs - g_lastStatusLogMs < kStatusLogIntervalMs) return;
  g_lastStatusLogMs = nowMs;
  LOGI(kTag, "uptime %lu s, health %s, heap free %lu / min %lu / block %lu", static_cast<unsigned long>(uptimeS()),
       componentStatusName(g_health.overall()), static_cast<unsigned long>(sysinfo::freeHeap()),
       static_cast<unsigned long>(sysinfo::minFreeHeap()), static_cast<unsigned long>(sysinfo::largestFreeBlock()));
}

void handleButton(uint32_t nowMs) {
  switch (g_button.update(board::serviceButtonPressed(), nowMs)) {
    case ButtonDetector::Event::LongPress:
      // Service mode (AP + web UI) is phase 5; for now only the LED feedback.
      LOGI(kTag, "service mode requested (not implemented yet)");
      g_led.setMode(StatusLed::Mode::ServiceMode);
      break;
    case ButtonDetector::Event::ShortPress:
      if (g_led.mode() == StatusLed::Mode::ServiceMode) {
        g_led.setMode(g_app.safeMode ? StatusLed::Mode::SafeMode : StatusLed::Mode::Health);
      }
      break;
    case ButtonDetector::Event::None: break;
  }
}

}  // namespace

void setup() {
  // The USB CDC default receive buffer is 256 bytes; a pasted config line (~900 bytes) arrives
  // within a millisecond and would be cut. Must be set before begin().
  Serial.setRxBufferSize(SerialConsole::kRxBufferSize);
  Serial.begin(115200);
  sysinfo::begin();
  logger::init(uptimeMs);
  logger::addSink(&g_serialLog);
#if SBH_DEV_BUILD
  logger::setLevel(LogLevel::Debug);
#endif
  g_led.begin();

  LOGI(kTag, "senseBox:home v2 firmware %s, reset reason: %s", SBH_FW_VERSION, sysinfo::resetReason());
  if (sysinfo::previousBreadcrumb()[0] != '\0') LOGW(kTag, "previous run stopped in: %s", sysinfo::previousBreadcrumb());

  persist::begin();
  persist::saveUnhealthyBoots(g_bootGuard.onBoot(persist::loadUnhealthyBoots(), sysinfo::lastResetWasAbnormal()));
  g_app.safeMode = g_bootGuard.safeMode();

  persist::DeviceIdentity id;
  if (persist::loadIdentity(id)) {
    LOGI(kTag, "device %s, hardware %s, batch %s", id.serial, id.hardware, id.batch);
  } else {
    LOGW(kTag, "no device identity (factory_id partition empty)");
  }

  loadConfig();

  if (g_app.safeMode) {
    LOGE(kTag, "SAFE MODE: %u crashes in a row without reaching a healthy state; sensors and uplinks stay off",
         g_bootGuard.unhealthyBoots());
    LOGE(kTag, "fix the cause (e.g. config-set), then 'reboot' to leave safe mode");
    g_led.setMode(StatusLed::Mode::SafeMode);
  } else {
    sysinfo::breadcrumb("setup:sensors");
    board::initSensorBus();
    logSensorBus();
    g_pipeline.addSink(&g_measurementLog);
    g_sensors.configure(g_config, drivers::table(), drivers::count());
    g_sensors.begin(millis());

    sysinfo::breadcrumb("setup:uplinks");
    startUplinks();
    g_led.setMode(StatusLed::Mode::Health);
  }

  g_console.begin();
  sysinfo::watchdogAddCurrentTask();
  sysinfo::breadcrumb("loop");
  LOGI(kTag, "setup done in %lu ms", static_cast<unsigned long>(millis()));
}

void loop() {
  const uint32_t nowMs = millis();
  sysinfo::watchdogFeed();

  if (!g_app.safeMode) {
    g_sensors.tick(nowMs);
    g_lora.poll(nowMs);
    if (g_cayenne != nullptr) g_cayenne->tick(nowMs);
  }
  if (g_bootGuard.tick(nowMs)) {
    persist::saveUnhealthyBoots(0);
    LOGI(kTag, "healthy for %lu min, boot counter reset", static_cast<unsigned long>(BootGuard::kHealthyAfterMs / 60000));
  }

  handleButton(nowMs);
  g_console.poll(nowMs);
  g_led.tick(nowMs, g_health.overall());
  periodicStatusLog(nowMs);

  vTaskDelay(pdMS_TO_TICKS(kLoopDelayMs));
}
