// Bounds the memory the WiFi driver may take (service mode on the MCU S2, which has no PSRAM).
//
// The Arduino WiFi library initialises WiFi with up to 32 dynamic RX buffers (~1.6 KB each) and
// AMPDU reordering. While the main loop is busy (e.g. writing an uploaded firmware to flash),
// incoming frames pile up in those buffers until the heap is gone: a firmware upload failed at
// 88 KB with 208 bytes of heap left. With a fixed, small number of buffers WiFi drops frames
// instead and TCP sends them again, which is slower but never runs out of memory.
//
// The Arduino library has no setting for this, so esp_wifi_init() is wrapped at link time
// (-Wl,--wrap=esp_wifi_init in platformio.ini) and the configuration adjusted here.

#include <esp_wifi.h>

namespace {
constexpr int kDynamicRxBuffers = 10;  // TCP window (4 segments) + ACKs + a few other frames
constexpr int kDynamicTxBuffers = 16;
}  // namespace

extern "C" esp_err_t __real_esp_wifi_init(const wifi_init_config_t* config);

extern "C" esp_err_t __wrap_esp_wifi_init(const wifi_init_config_t* config) {
  wifi_init_config_t cfg = *config;
  cfg.ampdu_rx_enable = 0;  // no reordering queue of up to 16 frames per client
  cfg.ampdu_tx_enable = 0;
  if (cfg.dynamic_rx_buf_num > kDynamicRxBuffers) cfg.dynamic_rx_buf_num = kDynamicRxBuffers;
  if (cfg.dynamic_tx_buf_num > kDynamicTxBuffers) cfg.dynamic_tx_buf_num = kDynamicTxBuffers;
  return __real_esp_wifi_init(&cfg);
}
