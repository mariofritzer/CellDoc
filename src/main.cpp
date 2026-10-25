// CellDoc – ESP32-Akkuprüfstand
// https://github.com/mariofritzer/CellDoc
#include <Arduino.h>
#include "config.h"
#include "hal.h"
#include "settings.h"
#include "storage.h"
#include "tester.h"
#include "web.h"

void setup() {
  Serial.begin(115200);
  // Hardware zuerst in den sicheren Zustand bringen
  hal.begin();
  Serial.printf("\nCellDoc %s\n", CELLDOC_VERSION);
  if (!storage::begin()) Serial.println("LittleFS konnte nicht eingebunden werden");
  settings.load();
  hal.rescanTempSensors();
  tester.begin();
  web::begin();
  hal.beep(80);
}

void loop() {
  uint32_t now = millis();
  hal.update(now);
  tester.update(now);
  web::update();
}
