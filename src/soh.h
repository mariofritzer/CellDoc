// CellDoc – Lebenszahl (State of Health)
// Reines C++, getestet in test/test_soh.cpp
#pragma once
#include "profiles.h"

struct SohInput {
  const Chemistry* chem;
  uint8_t cells;        // in Serie, 1 = Einzelzelle
  float capMah;         // gemessen
  float riMilli;        // gemessen, ganzer Akku, DC bei ca. 50 %
  float nomCapMah;      // 0 = unbekannt
  float riNewMilli;     // 0 = unbekannt (ganzer Akku)
};

struct SohResult {
  int score;        // 0..100
  int low, high;    // Bereich, bei bekannten Zellen = score
  bool estimated;
  int capScore;     // 0..100
  int riScore;      // 0..100, -1 wenn kein Ri gemessen
  float capRatio;   // gemessen / Referenz
  float refCapMah;  // verwendete Referenz
};

namespace soh {

inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// 100 % Nennkapazität = 100 Punkte, 60 % = 0 Punkte
inline float capacityScore(float ratio) { return clamp01((ratio - 0.6f) / 0.4f) * 100.0f; }

// Neuwert = 100 Punkte, 2,5-facher Neuwert = 0 Punkte
inline float riScoreKnown(float ri, float riNew) {
  const float dead = riNew * 2.5f;
  return clamp01((dead - ri) / (dead - riNew)) * 100.0f;
}

// Feste Grenzen pro Zelle für unbekannte Zellen
inline float riScoreAbsolute(float riPerCell, const Chemistry& c) {
  return clamp01((c.riDeadMilli - riPerCell) / (c.riDeadMilli - c.riGoodMilli)) * 100.0f;
}

inline int roundi(float v) { return int(v + 0.5f); }

inline SohResult compute(const SohInput& in) {
  SohResult r{};
  const Chemistry& c = *in.chem;
  const uint8_t n = in.cells ? in.cells : 1;
  const bool haveRi = in.riMilli > 0;
  const bool capKnown = in.nomCapMah > 0;
  const bool riKnown = in.riNewMilli > 0;

  r.refCapMah = capKnown ? in.nomCapMah : c.defaultCapMah;
  r.capRatio = r.refCapMah > 0 ? in.capMah / r.refCapMah : 0;
  float cs = r.refCapMah > 0 ? capacityScore(r.capRatio) : -1;

  float rs = -1;
  if (haveRi) rs = riKnown ? riScoreKnown(in.riMilli, in.riNewMilli)
                           : riScoreAbsolute(in.riMilli / n, c);

  r.estimated = !(capKnown && riKnown);
  float score;
  if (cs >= 0 && rs >= 0) {
    const float wCap = capKnown ? 0.7f : 0.5f;
    score = wCap * cs + (1 - wCap) * rs;
  } else if (cs >= 0) {
    score = cs;
  } else if (rs >= 0) {
    score = rs;  // nur Innenwiderstand bekannt
  } else {
    score = 0;
  }
  r.capScore = cs >= 0 ? roundi(cs) : -1;
  r.riScore = rs >= 0 ? roundi(rs) : -1;
  r.score = roundi(score);

  if (r.estimated) {
    // Schätzung: Bereich umso breiter, je weniger bekannt ist
    float spread = 5 + 0.1f * score;
    if (cs < 0 || rs < 0) spread += 5;
    r.low = roundi(score - spread);
    r.high = roundi(score + spread);
    if (r.low < 0) r.low = 0;
    if (r.high > 100) r.high = 100;
  } else {
    r.low = r.high = r.score;
  }
  return r;
}

// Kurzurteil für die Anzeige
inline const char* verdict(int score) {
  if (score >= 90) return "Wie neu";
  if (score >= 75) return "Sehr gut";
  if (score >= 55) return "Gut gebraucht";
  if (score >= 35) return "Schwach";
  if (score >= 15) return "Nur für kleine Lasten";
  return "Ersetzen";
}

}  // namespace soh
