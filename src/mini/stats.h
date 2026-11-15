// Zähler über die ganze Lebensdauer: Tests, Energie, Betriebsstunden
#pragma once
#include <Arduino.h>

namespace stats {
void begin();
void update(uint32_t now);  // integriert Lade-/Entladeenergie aller Schächte
void testDone();            // ein Test ist fertig
void routes();              // GET /api/stats, POST /api/stats/reset
}  // namespace stats
