// CellDoc – Ergebnisse im LittleFS
// /r/<id>.json  Zusammenfassung
// /c/<id>.json  Kurve [[t,v,i,mAh,temp],...]
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "tester.h"

namespace storage {
bool begin();
bool saveResult(uint32_t id, JsonDocument& summary, const CurvePoint* pts, int n);
void listResults(JsonArray out);           // neueste zuerst, nur Zusammenfassungen
bool streamFile(uint32_t id, bool curve, Print& out);
bool removeResult(uint32_t id);
float baselineCapacity(const String& name, uint32_t exceptId);  // erster Test mit gleichem Namen, 0 = keiner
size_t usedBytes();
size_t totalBytes();
}  // namespace storage
