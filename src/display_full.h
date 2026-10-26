// CellDoc – optionales OLED (SH1106 1,3") mit drei Tastern für den großen Prüfstand
#pragma once
#include <Arduino.h>

namespace display {
void begin();
void update(uint32_t now);
bool available();
}  // namespace display
