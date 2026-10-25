// PC-Test der Lebenszahl: g++ -std=c++17 -I src test/test_soh.cpp -o /tmp/t && /tmp/t
#include <cstdio>
#include <cstdlib>
#include "soh.h"

static int fails = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      std::printf("FEHLER Zeile %d: %s\n", __LINE__, #cond);          \
      fails++;                                                        \
    }                                                                 \
  } while (0)

int main() {
  const Chemistry* li = findChemistry("liion");

  // Neue Zelle: volle Kapazität, Ri = Neuwert
  auto a = soh::compute({li, 1, 3000, 25, 3000, 25});
  CHECK(a.score == 100 && !a.estimated && a.low == 100);

  // Beispiel aus der Vorschau: LG MJ1, 2838 / 3500 mAh, 38 / 35 mΩ
  auto b = soh::compute({li, 1, 2838, 38, 3500, 35});
  std::printf("MJ1: cap %d ri %d -> %d\n", b.capScore, b.riScore, b.score);
  CHECK(b.capScore == 53 && b.riScore == 94 && b.score == 65);

  // Unter 60 % Kapazität und doppelter Ri
  auto c = soh::compute({li, 1, 1500, 90, 3000, 30});
  CHECK(c.capScore == 0 && c.score <= 10);

  // Unbekannte Laptopzelle: 1612 mAh, 112 mΩ
  auto d = soh::compute({li, 1, 1612, 112, 0, 0});
  std::printf("unbekannt: %d (%d-%d) cap %d ri %d\n", d.score, d.low, d.high, d.capScore, d.riScore);
  CHECK(d.estimated && d.low <= d.score && d.score <= d.high);

  // 3S-LiPo ohne Ri-Neuwert: Ri pro Zelle wird verwendet
  const Chemistry* lipo = findChemistry("lipo");
  auto e = soh::compute({lipo, 3, 2100, 3 * 15.0f, 2200, 0});
  CHECK(e.riScore == 100 && e.estimated);

  // Ohne Ri-Messung zählt nur die Kapazität
  auto f = soh::compute({li, 1, 2700, 0, 3000, 25});
  CHECK(f.riScore == -1 && f.score == 75);

  CHECK(findPreset("s30q") && findPreset("gibtsnicht") == nullptr);

  std::printf(fails ? "%d Fehler\n" : "Alle Tests bestanden\n", fails);
  return fails ? 1 : 0;
}
