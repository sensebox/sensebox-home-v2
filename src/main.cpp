// senseBox:home v2 firmware - composition root.
//
// This file only wires the modules together; it contains no sensor- or target-specific logic.
// Architecture: implementation-docs/IMPLEMENTATION_PLAN.md §4.

#include <Arduino.h>
#include <string.h>

#include "App.h"
#include "Board.h"
#include "BootGuard.h"
#include "BootMode.h"
#include "ButtonDetector.h"
#include "CayenneUplink.h"
#include "Clock.h"
#include "ConfigCodec.h"
#include "ConfigEditor.h"
#include "Drivers.h"
#include "Health.h"
#include "LatestValues.h"
#include "LmicTransport.h"
#include "Log.h"
#include "PersistentStore.h"
#include "Pipeline.h"
#include "SdCardFs.h"
#include "SelfTest.h"
#include "SdStore.h"
#include "SensorManager.h"
#include "SerialConsole.h"
#include "ServiceMode.h"
#include "SinkFanout.h"
#include "StationConfig.h"
#include "StatusCapture.h"
#include "StatusLed.h"
#include "SystemInfo.h"
#include "Updater.h"
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
LatestValues g_latest;  // for the status snapshot taken before service mode

Clock g_clock(uptimeMs);
HealthRegistry g_health(uptimeS);
config::StationConfig g_config;
Pipeline g_pipeline(g_config);
SensorManager g_sensors(g_clock, g_health, g_pipeline);
LmicTransport g_lora(g_clock, g_health);
CayenneUplink* g_cayenne = nullptr;  // created at setup if configured
SdCardFs g_sdCard;
SdStore g_sd(g_sdCard, g_clock, g_health);
SinkFanout g_windowMeans;  // 10-min means of the uplink: log + SD card
SelfTest g_selfTest;       // after an OTA update: keep the new firmware or roll back
updater::RunningInfo g_running{};

StatusLed g_led;
ButtonDetector g_button;
BootGuard g_bootGuard;
App g_app{g_clock, g_health, g_config, g_sensors, g_lora, g_sd, g_selfTest, false, false};
void enterServiceMode(uint32_t nowMs);
SerialConsole g_console(g_app, enterServiceMode);
ServiceMode g_service;

HealthRegistry::Id g_configHealth = HealthRegistry::kInvalid;
uint32_t g_lastStatusLogMs = 0;
bool g_serviceRequested = false;  // BOOT held long enough; restart when it is released

void loadConfig() {
  g_configHealth = g_health.add("config");
  config::setDefaults(g_config);

  // Only needed during boot, so it lives on the heap and is freed again.
  const size_t cap = cfgedit::kJsonCap;
  auto buffer = sysinfo::makeTempBuffer(cap);
  if (!buffer) {
    g_health.reportError(g_configHealth, 3, "out of memory loading config");
    LOGE(kTag, "out of memory loading config");
    return;
  }
  char* json = buffer.get();
  size_t len = persist::loadConfigJson(json, cap);
  const char* source = "nvs";
#if SBH_DEV_BUILD
  if (len == 0) {
    strncpy(json, olpeTemplateJson, cap - 1);
    json[cap - 1] = '\0';
    len = strlen(json);
    source = "built-in Olpe template (dev build)";
  }
#endif
  if (len == 0) {
    g_health.reportError(g_configHealth, 1, "no configuration stored", ComponentStatus::Degraded);
    LOGW(kTag, "no configuration stored: station is unconfigured");
    return;
  }

  const config::DecodeResult r = config::decode(json, len, g_config);
  if (!r.ok) {
    // Never delete a config we can't read: keep it as backup, run unconfigured.
    persist::backupConfigJson(json);
    g_health.reportError(g_configHealth, 2, r.error);
    LOGE(kTag, "config from %s rejected: %s (kept as backup)", source, r.error);
    return;
  }
  if (r.migrated) {
    persist::backupConfigJson(json);
    if (config::encode(g_config, json, cap) > 0) persist::saveConfigJson(json);
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
    g_cayenne->setAggregateSink(&g_windowMeans);
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

// Saves the status of normal operation and restarts into service mode (plan §4.8).
void enterServiceMode(uint32_t nowMs) {
  LOGI(kTag, "restarting into service mode");
  g_sd.flushNow(nowMs);
  captureStatus(g_app, g_latest, nowMs, bootmode::snapshot());
  bootmode::requestServiceMode();
  Serial.flush();
  delay(100);
  ESP.restart();
}

bool configAccepted() {
  ComponentHealth h;
  return !g_health.get(g_configHealth, h) || h.status != ComponentStatus::Error;
}

// After an OTA update (plan §4.9): keep the new firmware once it has run 2 min, or roll back.
void runSelfTest(uint32_t nowMs) {
  switch (g_selfTest.tick(nowMs, configAccepted())) {
    case SelfTest::Result::Passed:
      if (updater::markValid()) {
        LOGI("ota", "new firmware %s passed the self-test and is kept", SBH_FW_VERSION);
      } else {
        LOGE("ota", "could not mark the firmware as valid");
      }
      break;
    case SelfTest::Result::Failed:
      LOGE("ota", "self-test failed (configuration rejected): going back to the previous firmware");
      g_sd.flushNow(nowMs);
      Serial.flush();
      updater::rollback();
      break;
    default: break;
  }
}

// BOOT held >= 3 s: the LED turns purple, and the restart happens when the button is released.
// Restarting while GPIO0 (a strapping pin) is still held could start the ROM download mode.
void handleButton(uint32_t nowMs) {
  if (g_button.update(board::serviceButtonPressed(), nowMs) == ButtonDetector::Event::LongPress) {
    if (g_selfTest.running()) {
      // A restart now would roll the fresh update back.
      LOGW(kTag, "firmware update is being checked: service mode available in %lu s",
           static_cast<unsigned long>(g_selfTest.secondsLeft(nowMs)));
      return;
    }
    g_serviceRequested = true;
    g_led.setMode(StatusLed::Mode::ServiceMode);
    LOGI(kTag, "service mode requested: release the button");
  }
  if (g_serviceRequested && !g_button.pressed()) enterServiceMode(nowMs);
}

void startServiceMode() {
  sysinfo::breadcrumb("service");
  g_led.setMode(StatusLed::Mode::ServiceMode);
  // The web server can wait up to 5 s for a slow client (e.g. a POST body that stalls); with the
  // default 5 s watchdog that would end in a crash reset.
  sysinfo::watchdogSetTimeout(15000);
  const StatusSnapshot& snap = bootmode::snapshot();
  const bool haveSnapshot = snapshot::isValid(snap);
  LOGI(kTag, "SERVICE MODE: sensors and LoRaWAN are off%s", haveSnapshot ? "" : " (no saved status)");

  persist::DeviceIdentity id{};
  persist::loadIdentity(id);
  ServiceMode::Options options;
  options.running = &g_config;
  options.snapshot = haveSnapshot ? &snap : nullptr;
  options.deviceSerial = id.serial;
  options.boardRevision = id.hardware;
  options.apTimeoutMin = g_config.service.apTimeoutMin;
  if (!g_service.begin(options)) {
    LOGE(kTag, "service mode could not start; console still works, 'reboot' to return to normal mode");
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
  g_running = updater::bootInfo();
  LOGI(kTag, "running from %s%s", g_running.partition, g_running.pendingVerify ? ", NEW FIRMWARE: self-test running" : "");
  LOGD(kTag, "image marker: %s", g_running.marker);
  if (g_running.previousRolledBack) LOGW("ota", "an earlier firmware update was rolled back");
  if (sysinfo::previousBreadcrumb()[0] != '\0') LOGW(kTag, "previous run stopped in: %s", sysinfo::previousBreadcrumb());

  // One-shot: consumed now, so any later restart is a normal start again.
  g_app.serviceMode = bootmode::consumeServiceModeRequest();
  if (!g_app.serviceMode) snapshot::invalidate(bootmode::snapshot());

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

  // The self-test runs in normal (and safe) mode. A fresh update that starts in service mode is
  // rolled back by the next restart, because nothing marks it valid.
  g_selfTest.begin(g_running.pendingVerify && !g_app.serviceMode);

  if (g_app.serviceMode) {
    startServiceMode();
  } else if (g_app.safeMode) {
    LOGE(kTag, "SAFE MODE: %u crashes in a row without reaching a healthy state; sensors and uplinks stay off",
         g_bootGuard.unhealthyBoots());
    LOGE(kTag, "fix the cause (e.g. config-set), then 'reboot' to leave safe mode");
    g_led.setMode(StatusLed::Mode::SafeMode);
  } else {
    // SD card first, so the boot log is on the card. The sink is registered before begin():
    // lines logged during the mount go straight into the SD buffer.
    sysinfo::breadcrumb("setup:sd");
    SdStore::Options sdOptions;
    sdOptions.bootId = persist::nextBootId();
    sdOptions.storage = g_config.storage;
    sdOptions.firmware = SBH_FW_VERSION;
    sdOptions.station = g_config.name;
    logger::addSink(&g_sd);
    g_sd.begin(sdOptions, millis());

    sysinfo::breadcrumb("setup:sensors");
    board::initSensorBus();
    logSensorBus();
    g_pipeline.addSink(&g_measurementLog);
    g_pipeline.addSink(&g_latest);
    g_pipeline.addSink(&g_sd);  // raw and corrected values
    g_windowMeans.add(&g_measurementLog);
    g_windowMeans.add(&g_sd);
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

  if (g_app.serviceMode) {
    // Nothing of normal operation runs; the BOOT button is ignored (leave by restarting).
    g_service.loop(nowMs);
    if (Serial.available() > 0) g_service.markActivity();
    g_console.poll(nowMs);
    g_led.tick(nowMs, g_health.overall());
    vTaskDelay(pdMS_TO_TICKS(kLoopDelayMs));
    return;
  }

  if (!g_app.safeMode) {
    g_sensors.tick(nowMs);
    g_lora.poll(nowMs);
    if (g_cayenne != nullptr) g_cayenne->tick(nowMs);
    g_sd.tick(nowMs);
  }
  if (g_bootGuard.tick(nowMs)) {
    persist::saveUnhealthyBoots(0);
    LOGI(kTag, "healthy for %lu min, boot counter reset", static_cast<unsigned long>(BootGuard::kHealthyAfterMs / 60000));
  }

  runSelfTest(nowMs);
  handleButton(nowMs);
  g_console.poll(nowMs);
  g_led.tick(nowMs, g_health.overall());
  periodicStatusLog(nowMs);

  vTaskDelay(pdMS_TO_TICKS(kLoopDelayMs));
}
