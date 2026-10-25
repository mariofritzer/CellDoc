// CellDoc – MQTT mit automatischer Erkennung in Home Assistant
#pragma once
#include <Arduino.h>

namespace mqtt {
void update();               // in loop() aufrufen
void reconfigure();          // nach geänderten Einstellungen neu verbinden
const char* status();        // "aus", "verbindet", "verbunden", "Fehler …"
}  // namespace mqtt
