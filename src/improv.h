// CellDoc – WLAN-Einrichtung über den Webflasher (Improv Serial)
#pragma once
#include <Arduino.h>

namespace improv {
void update();  // liest Befehle vom USB-Port, im loop() aufrufen
}
