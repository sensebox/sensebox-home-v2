// Hardware test: minimal WiFi access point on the senseBox MCU S2 (no sensors, no LoRa).
// Control experiment for "the network is visible but nobody can connect": if this fails too,
// the cause is the board/radio/power, not the firmware's service mode.
//
//   pio run -e hwtest_wifi_ap -t upload && pio device monitor
//
// Opens two networks one after the other, 2 minutes each:
//   1. "senseBox-test-open"  without a password
//   2. "senseBox-test-wpa2"  password 12345678
// Every connection attempt is printed.

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>

namespace {

WebServer server(80);
uint32_t phaseStart = 0;
int phase = -1;

void onEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED: {
      const uint8_t* m = info.wifi_ap_staconnected.mac;
      Serial.printf("  client %02x:%02x:%02x:%02x:%02x:%02x associated\n", m[0], m[1], m[2], m[3], m[4], m[5]);
      break;
    }
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: {
      const uint8_t* m = info.wifi_ap_stadisconnected.mac;
      Serial.printf("  client %02x:%02x:%02x:%02x:%02x:%02x disconnected, reason %u\n", m[0], m[1], m[2], m[3], m[4],
                    m[5], info.wifi_ap_stadisconnected.reason);
      break;
    }
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
      Serial.printf("  client got IP %s\n", IPAddress(info.wifi_ap_staipassigned.ip.addr).toString().c_str());
      break;
    case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED:
      break;
    default:
      break;
  }
}

void startPhase(int p) {
  phase = p;
  phaseStart = millis();
  if (WiFi.getMode() & WIFI_AP) {
    WiFi.softAPdisconnect(false);  // keep WiFi on, only switch the network
    delay(200);
  }
  bool ok;
  if (p == 0) {
    ok = WiFi.softAP("senseBox-test-open");
    Serial.println("\n== phase 1: open network 'senseBox-test-open' (no password), 2 min");
  } else {
    ok = WiFi.softAP("senseBox-test-wpa2", "12345678");
    Serial.println("\n== phase 2: network 'senseBox-test-wpa2', password 12345678, 2 min");
  }
  Serial.printf("  softAP %s, IP %s, channel %u, TX power %.2f dBm, free heap %lu\n", ok ? "ok" : "FAILED",
                WiFi.softAPIP().toString().c_str(), static_cast<unsigned>(WiFi.channel()), WiFi.getTxPower() / 4.0f,
                static_cast<unsigned long>(ESP.getFreeHeap()));
  Serial.println("  connect, then open http://192.168.4.1");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(3000);  // time to open the serial monitor
  Serial.printf("senseBox MCU S2 WiFi AP hardware test (reset reason %d)\n", static_cast<int>(esp_reset_reason()));
  WiFi.onEvent(onEvent);
  // WiFi first: in Arduino-ESP32 3.x the network stack only exists once WiFi has started, and
  // opening the server socket before that crashes.
  startPhase(0);
  server.on("/", [] {
    server.send(200, "text/html", "<h1>senseBox MCU S2 WiFi test</h1><p>Connected. The board's WiFi works.</p>");
    Serial.println("  page served");
  });
  server.begin();
  Serial.println("  web server started");
}

void loop() {
  server.handleClient();
  if (millis() - phaseStart > 120000) startPhase(phase == 0 ? 1 : 0);
  static uint32_t lastInfo = 0;
  if (millis() - lastInfo > 15000) {
    lastInfo = millis();
    Serial.printf("  [%lu s] %u client(s) connected\n", static_cast<unsigned long>(millis() / 1000),
                  WiFi.softAPgetStationNum());
  }
  delay(2);
}
