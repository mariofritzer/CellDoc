// CellDoc – ESP32-Akkuprüfstand
// https://github.com/mariofritzer/CellDoc
#include <Arduino.h>
#include "config.h"
#include "display_full.h"
#include "hal.h"
#include "settings.h"
#include "evlog.h"
#include "stats.h"
#include "storage.h"
#include "tester.h"
#include "web.h"

namespace device { void hwTick(uint32_t now); }  // web_full.cpp: Hardware-Test

void setup() {
  Serial.begin(115200);
  // Hardware zuerst in den sicheren Zustand bringen
  hal.begin();
  Serial.printf("\nCellDoc %s\n", CELLDOC_VERSION);
  if (!storage::begin()) Serial.println("LittleFS konnte nicht eingebunden werden");
  evlog::begin();
  settings.load();
  stats::begin();
  hal.rescanTempSensors();
  tester.begin();
  display::begin();  // optionales OLED mit drei Tastern
  web::begin();  // meldet sich mit 1/2/3 Tönen: WLAN verbunden / Hotspot / WLAN nicht erreichbar
}

void loop() {
  uint32_t now = millis();
  hal.update(now);
  tester.update(now);
  display::update(now);
  web::update();
  {
    const Meas& m = hal.m();
    float a[2], w[2];
    for (int k = 0; k < 2; k++) { a[k] = m.port[k].ok ? m.port[k].i : NAN; w[k] = m.port[k].p; }
    stats::tick(now, a, w, 2);
  }
  device::hwTick(now);
}
