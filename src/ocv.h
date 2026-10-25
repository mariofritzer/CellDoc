// CellDoc – Ladezustand aus der Ruhespannung (grobe Kennlinien pro Zelle)
#pragma once
#include <string.h>
#include "profiles.h"

namespace ocv {

struct Pt { float v, soc; };

inline const Pt LIION[] = {{3.00f, 0},  {3.30f, 5},  {3.50f, 10}, {3.60f, 20}, {3.68f, 30}, {3.74f, 40},
                           {3.79f, 50}, {3.85f, 60}, {3.92f, 70}, {3.99f, 80}, {4.08f, 90}, {4.20f, 100}};
inline const Pt LIHV[] = {{3.20f, 0},  {3.50f, 8},  {3.65f, 20}, {3.75f, 35}, {3.82f, 50},
                          {3.92f, 65}, {4.05f, 80}, {4.18f, 90}, {4.35f, 100}};
inline const Pt LFP[] = {{2.50f, 0},  {3.00f, 5},  {3.20f, 15}, {3.25f, 30}, {3.28f, 50},
                         {3.30f, 70}, {3.33f, 90}, {3.40f, 97}, {3.60f, 100}};
inline const Pt LTO[] = {{1.80f, 0}, {2.10f, 10}, {2.25f, 30}, {2.35f, 50}, {2.45f, 70}, {2.55f, 90}, {2.80f, 100}};
inline const Pt LEAD[] = {{1.95f, 0}, {1.98f, 10}, {2.01f, 25}, {2.05f, 50}, {2.09f, 75}, {2.12f, 90}, {2.15f, 100}};
inline const Pt NIMH[] = {{1.00f, 0}, {1.15f, 10}, {1.20f, 30}, {1.25f, 60}, {1.30f, 85}, {1.40f, 100}};

template <int N>
inline float lookup(const Pt (&t)[N], float v) {
  if (v <= t[0].v) return 0;
  if (v >= t[N - 1].v) return 100;
  for (int k = 1; k < N; k++) {
    if (v <= t[k].v) {
      float f = (v - t[k - 1].v) / (t[k].v - t[k - 1].v);
      return t[k - 1].soc + f * (t[k].soc - t[k - 1].soc);
    }
  }
  return 100;
}

// Spannung pro Zelle -> Ladezustand in %
inline float socFromCellV(const Chemistry& c, float v) {
  if (strcmp(c.id, "lfp") == 0) return lookup(LFP, v);
  if (strcmp(c.id, "nimh") == 0) return lookup(NIMH, v);
  if (strcmp(c.id, "lihv") == 0) return lookup(LIHV, v);
  if (strcmp(c.id, "lto") == 0) return lookup(LTO, v);
  if (strcmp(c.id, "lead") == 0) return lookup(LEAD, v);
  return lookup(LIION, v);
}

}  // namespace ocv
