// CellDoc Mini – gescannte Zellen den Schächten zuordnen
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace assign {
void onScan(uint32_t no, const String& id);
void tick(uint32_t now);
uint32_t no(int slot);           // 0 = keine Zelle zugeordnet
const String& id(int slot);
void clear(int slot);
void stateJson(JsonObject root, JsonArray slots);
}  // namespace assign
