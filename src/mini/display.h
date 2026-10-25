// CellDoc Mini – optionales OLED mit Tastern
#pragma once
#include <Arduino.h>

namespace display {
void begin();
void update(uint32_t now);
bool available();
}  // namespace display
