// CellDoc – Zellchemien und Zelltypen
// Reines C++, damit es auch auf dem PC getestet werden kann.
#pragma once
#include <stdint.h>
#include <string.h>

enum class ChargerMode : uint8_t { Off, Li42, Lfp36 };

struct Chemistry {
  const char* id;
  const char* name;
  float nominalV;      // pro Zelle
  float chargeV;       // Ladeschluss pro Zelle
  float maxV;          // Abbruch beim Laden pro Zelle
  float cutoffV;       // Entladeschluss pro Zelle (unter Last)
  float storageV;      // Lagerspannung pro Zelle (Ruhe)
  ChargerMode charger; // Off = kann auf Anschluss A nicht geladen werden
  float riGoodMilli;   // Schätzmodus: bis hier "wie neu" (pro Zelle, DC)
  float riDeadMilli;   // Schätzmodus: ab hier "verbraucht"
  float defaultCapMah; // Schätzmodus: Vergleichskapazität für 1S ohne Angabe
  bool lithium;        // Packs brauchen den Balancerstecker
};

// Werte pro Zelle. Li-HV wird vom TP5000 nur auf 4,2 V geladen.
inline const Chemistry CHEMISTRIES[] = {
  {"liion", "Li-Ion (NMC/NCA)", 3.6f, 4.20f, 4.25f, 3.00f, 3.80f, ChargerMode::Li42, 40, 150, 2600, true},
  {"lipo",  "LiPo",             3.7f, 4.20f, 4.25f, 3.20f, 3.85f, ChargerMode::Li42, 15, 60, 0, true},
  {"lihv",  "Li-HV",            3.8f, 4.35f, 4.40f, 3.20f, 3.85f, ChargerMode::Li42, 15, 60, 0, true},
  {"lfp",   "LiFePO4",          3.2f, 3.60f, 3.65f, 2.50f, 3.30f, ChargerMode::Lfp36, 30, 120, 1500, true},
  {"lto",   "LTO",              2.4f, 2.80f, 2.90f, 1.80f, 2.40f, ChargerMode::Off, 10, 40, 0, true},
  {"nimh",  "NiMH / NiCd",      1.2f, 1.45f, 1.55f, 1.00f, 1.25f, ChargerMode::Off, 30, 120, 2000, false},
  {"lead",  "Blei (AGM/Gel/Nass)", 2.0f, 2.40f, 2.50f, 1.80f, 2.15f, ChargerMode::Off, 5, 20, 0, false},
};
constexpr int CHEMISTRY_COUNT = sizeof(CHEMISTRIES) / sizeof(CHEMISTRIES[0]);

inline const Chemistry* findChemistry(const char* id) {
  for (int i = 0; i < CHEMISTRY_COUNT; i++)
    if (strcmp(CHEMISTRIES[i].id, id) == 0) return &CHEMISTRIES[i];
  return &CHEMISTRIES[0];
}

// Bekannte Zellen. Ri ist ein Richtwert für den DC-Innenwiderstand
// (1-s-Stromsprung) im Neuzustand. Datenblätter nennen meist den kleineren
// AC-Wert bei 1 kHz. Eigene Werte lassen sich in der Weboberfläche eintragen.
struct CellPreset {
  const char* id;
  const char* name;
  const char* chem;
  float capMah;
  float riNewMilli;
};

inline const CellPreset PRESETS[] = {
  {"s30q",  "Samsung INR18650-30Q",  "liion", 3000, 25},
  {"s25r",  "Samsung INR18650-25R",  "liion", 2500, 22},
  {"s35e",  "Samsung INR18650-35E",  "liion", 3450, 40},
  {"lgmj1", "LG INR18650-MJ1",       "liion", 3500, 40},
  {"lghg2", "LG INR18650-HG2",       "liion", 3000, 25},
  {"vtc6",  "Sony/Murata US18650VTC6", "liion", 3000, 20},
  {"ncrb",  "Panasonic NCR18650B",   "liion", 3350, 45},
  {"p28a",  "Molicel INR18650-P28A", "liion", 2800, 15},
  {"s40t",  "Samsung INR21700-40T",  "liion", 4000, 15},
  {"s50e",  "Samsung INR21700-50E",  "liion", 5000, 30},
  {"lfp26", "LiFePO4 26650 (3000 mAh)", "lfp", 3000, 25},
};
constexpr int PRESET_COUNT = sizeof(PRESETS) / sizeof(PRESETS[0]);

inline const CellPreset* findPreset(const char* id) {
  for (int i = 0; i < PRESET_COUNT; i++)
    if (strcmp(PRESETS[i].id, id) == 0) return &PRESETS[i];
  return nullptr;
}
