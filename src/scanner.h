// CellDoc – optionaler QR-Scanner (GM861S, GM65 …) an Serial2
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace scanner {
void begin();
void update();
void fill(JsonObject o);  // letzter Scan für /api/state
}  // namespace scanner
