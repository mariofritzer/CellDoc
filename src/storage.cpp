#include "storage.h"
#include <LittleFS.h>
#include <algorithm>
#include <vector>

namespace storage {

static String rPath(uint32_t id) { return "/r/" + String(id) + ".json"; }
static String cPath(uint32_t id) { return "/c/" + String(id) + ".json"; }

bool begin() {
  if (!LittleFS.begin(true)) return false;
  if (!LittleFS.exists("/r")) LittleFS.mkdir("/r");
  if (!LittleFS.exists("/c")) LittleFS.mkdir("/c");
  if (!LittleFS.exists("/z")) LittleFS.mkdir("/z");  // Zellenbestand
  return true;
}

// Ältere Ergebnisse löschen, wenn der Speicher knapp wird
// Wird der Speicher knapp: zuerst die Kurven der ältesten Tests löschen (Messwerte bleiben),
// erst danach ganze alte Ergebnisse. Der Zellenbestand wird nie angetastet.
static uint32_t oldestIn(const char* dir) {
  uint32_t oldest = UINT32_MAX;
  File d = LittleFS.open(dir);
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    uint32_t id = String(f.name()).toInt();
    if (id && id < oldest) oldest = id;
  }
  return oldest;
}

static void makeRoom() {
  while (LittleFS.totalBytes() - LittleFS.usedBytes() < 60000) {
    uint32_t c = oldestIn("/c");
    if (c != UINT32_MAX) {
      LittleFS.remove(cPath(c));
      continue;
    }
    uint32_t r = oldestIn("/r");
    if (r == UINT32_MAX) return;
    removeResult(r);
  }
}

bool saveResult(uint32_t id, JsonDocument& summary, const CurvePoint* pts, int n) {
  makeRoom();
  File f = LittleFS.open(rPath(id), "w");
  if (!f) return false;
  serializeJson(summary, f);
  f.close();

  File c = LittleFS.open(cPath(id), "w");
  if (!c) return false;
  c.print('[');
  char buf[80];
  for (int k = 0; k < n; k++) {
    snprintf(buf, sizeof(buf), "%s[%lu,%.3f,%.3f,%.1f,%.1f]", k ? "," : "", (unsigned long)pts[k].t,
             pts[k].v, pts[k].i, pts[k].qmAh, isnan(pts[k].temp) ? 0.0f : pts[k].temp);
    c.print(buf);
  }
  c.print(']');
  c.close();
  return true;
}

void listResults(JsonArray out) {
  std::vector<uint32_t> ids;
  File dir = LittleFS.open("/r");
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    uint32_t id = String(f.name()).toInt();
    if (id) ids.push_back(id);
  }
  std::sort(ids.begin(), ids.end(), [](uint32_t a, uint32_t b) { return a > b; });
  for (uint32_t id : ids) {
    File f = LittleFS.open(rPath(id), "r");
    if (!f) continue;
    JsonDocument d;
    if (!deserializeJson(d, f)) out.add(d.as<JsonObject>());
    f.close();
  }
}

bool streamFile(uint32_t id, bool curve, Print& out) {
  File f = LittleFS.open(curve ? cPath(id) : rPath(id), "r");
  if (!f) return false;
  uint8_t buf[512];
  while (f.available()) {
    size_t n = f.read(buf, sizeof(buf));
    out.write(buf, n);
  }
  f.close();
  return true;
}

bool removeResult(uint32_t id) {
  bool a = LittleFS.remove(rPath(id));
  LittleFS.remove(cPath(id));
  return a;
}

float baselineCapacity(const String& name, uint32_t exceptId) {
  uint32_t best = UINT32_MAX;
  float cap = 0;
  File dir = LittleFS.open("/r");
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    uint32_t id = String(f.name()).toInt();
    if (!id || id == exceptId || id >= best) continue;
    JsonDocument d;
    if (deserializeJson(d, f)) continue;
    if (name == (d["name"] | "")) {
      best = id;
      cap = d["capMah"] | 0.0f;
    }
  }
  return cap;
}

size_t usedBytes() { return LittleFS.usedBytes(); }
size_t totalBytes() { return LittleFS.totalBytes(); }

}  // namespace storage
