// CellDoc Mini – 18650-Prüfstand mit 1, 2 oder 4 Schächten, CellDoc Nano mit einem Schacht
// https://github.com/mariofritzer/CellDoc
#include <Arduino.h>
#include "config.h"
#include "display.h"
#include "hal_mini.h"
#include "settings.h"
#include "slot.h"
#include "storage.h"
#include "web.h"
#include "web_device.h"

void setup() {
  Serial.begin(115200);
  hal.begin();  // Last und Lader zuerst sicher aus
  Serial.printf("\n%s · %s\n", device::model(), CELLDOC_VERSION);
  if (!storage::begin()) Serial.println("LittleFS konnte nicht eingebunden werden");
  settings.load();
  hal.rescanTempSensors();
  for (int s = 0; s < mcfg::NSLOTS; s++) slots[s].begin(s);
  display::begin();
  web::begin();
  hal.beep(80);
}

void loop() {
  uint32_t now = millis();
  hal.update(now);
  for (int s = 0; s < mcfg::NSLOTS; s++) slots[s].update(now);
  display::update(now);
  web::update();
}
