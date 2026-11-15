// CellDoc Mini – 18650-Prüfstand mit 1, 2 oder 4 Schächten, CellDoc Nano mit einem Schacht
// https://github.com/mariofritzer/CellDoc
#include <Arduino.h>
#include "config.h"
#include "display.h"
#include "hal_mini.h"
#include "settings.h"
#include "slot.h"
#include "evlog.h"
#include "storage.h"
#include "stats.h"
#include "web.h"
#include "web_device.h"

namespace device { void hwTick(uint32_t now); }  // web_mini.cpp: Hardware-Test

void setup() {
  Serial.begin(115200);
  hal.begin();  // Last und Lader zuerst sicher aus
  Serial.printf("\n%s · %s\n", device::model(), CELLDOC_VERSION);
  if (!storage::begin()) Serial.println("LittleFS konnte nicht eingebunden werden");
  evlog::begin();
  settings.load();
  stats::begin();
  hal.rescanTempSensors();
  for (int s = 0; s < mcfg::NSLOTS; s++) slots[s].begin(s);
  display::begin();
  web::begin();  // meldet sich mit 1/2/3 Tönen: WLAN verbunden / Hotspot / WLAN nicht erreichbar
}

void loop() {
  uint32_t now = millis();
  hal.update(now);
  for (int s = 0; s < mcfg::NSLOTS; s++) slots[s].update(now);
  display::update(now);
  web::update();
  {
    float a[mcfg::NSLOTS], w[mcfg::NSLOTS];
    for (int s = 0; s < mcfg::NSLOTS; s++) { const SlotMeas& m = hal.m(s); a[s] = m.ok ? m.i : NAN; w[s] = m.p; }
    stats::tick(now, a, w, mcfg::NSLOTS);
  }
  device::hwTick(now);
}
