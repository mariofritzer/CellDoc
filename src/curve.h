// CellDoc – Messpunkt der Lade-/Entladekurve (gemeinsam für CellDoc und Mini)
#pragma once
#include <stdint.h>

struct CurvePoint {
  uint32_t t;   // s seit Start
  float v, i, qmAh, temp;
};
