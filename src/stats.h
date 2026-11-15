// Zähler über die ganze Lebensdauer: Tests, Energie, Betriebsstunden
#pragma once
#include <Arduino.h>

namespace stats {
void begin();
// Strom (A, + = Laden) und Leistung (W) aller Anschlüsse/Schächte, regelmäßig aufrufen
void tick(uint32_t now, const float* amps, const float* watts, int n);
void testDone();            // ein Test ist fertig
void routes();              // GET /api/stats, POST /api/stats/reset
}  // namespace stats
