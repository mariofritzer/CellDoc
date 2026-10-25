#include "cells.h"
#include <LittleFS.h>
#include "profiles.h"
#include "settings.h"
#include "soh.h"

namespace cells {

static String path(uint32_t no) { return "/z/" + String(no) + ".json"; }

static uint32_t nowEpoch() {
  time_t t = time(nullptr);
  return t > 1700000000 ? uint32_t(t) : 0;
}

bool begin() {
  if (!LittleFS.exists("/z")) LittleFS.mkdir("/z");
  return true;
}

String makeId(uint32_t no) {
  char b[24];
  snprintf(b, sizeof(b), "%s-%04lu", settings.cellPrefix.c_str(), (unsigned long)no);
  return String(b);
}

uint32_t parseRef(const String& s) {
  // letzte Ziffernfolge zählt: „CD-0042“, „#42“, „42“
  int end = s.length() - 1;
  while (end >= 0 && !isdigit((unsigned char)s[end])) end--;
  if (end < 0) return 0;
  int start = end;
  while (start > 0 && isdigit((unsigned char)s[start - 1])) start--;
  return strtoul(s.substring(start, end + 1).c_str(), nullptr, 10);
}

bool exists(uint32_t no) { return no && LittleFS.exists(path(no)); }

bool load(uint32_t no, JsonDocument& d) {
  File f = LittleFS.open(path(no), "r");
  if (!f) return false;
  bool ok = !deserializeJson(d, f);
  f.close();
  return ok;
}

bool save(uint32_t no, JsonDocument& d) {
  File f = LittleFS.open(path(no), "w");
  if (!f) return false;
  serializeJson(d, f);  // eine Zeile, wichtig für die Datensicherung
  f.close();
  return true;
}

bool remove(uint32_t no) { return LittleFS.remove(path(no)); }

static void fillNew(JsonDocument& d, uint32_t no) {
  d["no"] = no;
  d["id"] = makeId(no);
  d["status"] = "ok";
  d["created"] = nowEpoch();
  d["tests"].to<JsonArray>();
}

static void setPreset(JsonDocument& d, const String& presetId) {
  d["preset"] = presetId;
  const CellPreset* p = findPreset(presetId.c_str());
  d["presetName"] = p ? p->name : "";
}

uint32_t create(JsonObjectConst info) {
  uint32_t no = settings.nextCellNo++;
  settings.save();
  JsonDocument d;
  fillNew(d, no);
  String name = info["name"] | "";
  name.trim();
  d["name"] = name.length() ? name : makeId(no);
  d["chem"] = info["chem"] | "liion";
  setPreset(d, String(info["preset"] | ""));
  d["cells"] = info["cells"] | 1;
  d["nomCapMah"] = info["nomCapMah"] | 0;
  d["riNew"] = info["riNew"] | 0;
  d["loc"] = info["loc"] | "";
  d["note"] = info["note"] | "";
  d["pack"] = "";
  save(no, d);
  return no;
}

String prepare(JobCell& c) {
  JsonDocument d;
  if (c.no) {
    if (!load(c.no, d)) return "Zelle " + makeId(c.no) + " nicht gefunden";
    // fehlende Angaben aus dem Bestand übernehmen, neue Angaben im Bestand merken
    String stored = d["name"] | "";
    if (!c.name.length()) c.name = stored;
    else d["name"] = c.name;
    if (!c.presetId.length()) c.presetId = String(d["preset"] | "");
    else setPreset(d, c.presetId);
    if (c.nomCapMah <= 0) c.nomCapMah = d["nomCapMah"] | 0.0f;
    else d["nomCapMah"] = c.nomCapMah;
    if (c.riNewMilli <= 0) c.riNewMilli = d["riNew"] | 0.0f;
    else d["riNew"] = c.riNewMilli;
    d["chem"] = c.chem;
    d["cells"] = c.cellCount;
  } else {
    c.no = settings.nextCellNo++;
    settings.save();
    fillNew(d, c.no);
    if (!c.name.length()) c.name = makeId(c.no);
    d["name"] = c.name;
    d["chem"] = c.chem;
    setPreset(d, c.presetId);
    d["cells"] = c.cellCount;
    d["nomCapMah"] = c.nomCapMah;
    d["riNew"] = c.riNewMilli;
    d["loc"] = "";
    d["note"] = "";
    d["pack"] = "";
  }
  c.id = d["id"] | makeId(c.no);
  save(c.no, d);
  return "";
}

void addResult(uint32_t no, JsonDocument& r) {
  JsonDocument d;
  if (!load(no, d)) return;
  JsonArray t = d["tests"].is<JsonArray>() ? d["tests"].as<JsonArray>() : d["tests"].to<JsonArray>();
  t.add(r["id"]);
  while (t.size() > 40) t.remove(0);
  JsonObjectConst s = r["soh"];
  JsonObject last = d["last"].to<JsonObject>();
  last["id"] = r["id"];
  last["time"] = r["time"];
  last["cap"] = r["capMah"];
  last["wh"] = r["energyWh"];
  float ri = r["ri50"] | 0.0f;
  if (ri <= 0) ri = r["ri100"] | 0.0f;
  last["ri"] = ri;
  int score = s["score"] | 0;
  last["score"] = score;
  last["low"] = s["low"] | score;
  last["high"] = s["high"] | score;
  last["est"] = s["estimated"] | false;
  last["grade"] = String(soh::grade(score));
  if (!d["first"].is<JsonObject>()) {
    JsonObject f = d["first"].to<JsonObject>();
    f["id"] = r["id"];
    f["time"] = r["time"];
    f["cap"] = r["capMah"];
  }
  save(no, d);
}

float firstCapacity(uint32_t no) {
  JsonDocument d;
  if (!load(no, d)) return 0;
  return d["first"]["cap"] | 0.0f;
}

String sdStart(uint32_t no, float v) {
  uint32_t t = nowEpoch();
  if (!t) return "Uhrzeit unbekannt. Bitte die Seite neu laden, dann übernimmt CellDoc die Zeit vom Browser.";
  if (v < 3.0f) return "Für den Selbstentladungstest die Zelle zuerst voll laden";
  JsonDocument d;
  if (!load(no, d)) return "Zelle nicht gefunden";
  JsonObject sd = d["sd"].to<JsonObject>();
  sd["state"] = "run";
  sd["v0"] = roundf(v * 1000) / 1000;
  sd["t0"] = t;
  save(no, d);
  return "";
}

String sdMeasure(uint32_t no, float v) {
  uint32_t t = nowEpoch();
  if (!t) return "Uhrzeit unbekannt. Bitte die Seite neu laden.";
  if (v < 1.0f) return "Keine Zelle erkannt";
  JsonDocument d;
  if (!load(no, d)) return "Zelle nicht gefunden";
  JsonObject sd = d["sd"];
  if (sd.isNull() || !(sd["t0"] | 0)) return "Für diese Zelle läuft kein Selbstentladungstest";
  uint32_t t0 = sd["t0"];
  if (t < t0 + 6 * 3600) return "Frühestens 6 Stunden nach dem Start messen, besser nach 7–14 Tagen";
  float v0 = sd["v0"];
  float days = (t - t0) / 86400.0f;
  float mvDay = (v0 - v) * 1000.0f / days;
  if (mvDay < 0) mvDay = 0;
  sd["state"] = "done";
  sd["v1"] = roundf(v * 1000) / 1000;
  sd["t1"] = t;
  sd["days"] = roundf(days * 10) / 10;
  sd["mvDay"] = roundf(mvDay * 10) / 10;
  sd["grade"] = mvDay < 3 ? "gut" : mvDay < 8 ? "auffällig" : "schlecht";
  sd["prelim"] = days < 7;
  save(no, d);
  return "";
}

String sdReset(uint32_t no) {
  JsonDocument d;
  if (!load(no, d)) return "Zelle nicht gefunden";
  d.remove("sd");
  save(no, d);
  return "";
}

void rescanCounters() {
  uint32_t maxCell = 0, maxRes = 0;
  File dir = LittleFS.open("/z");
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) maxCell = max(maxCell, (uint32_t)String(f.name()).toInt());
  dir = LittleFS.open("/r");
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) maxRes = max(maxRes, (uint32_t)String(f.name()).toInt());
  settings.nextCellNo = max(settings.nextCellNo, maxCell + 1);
  settings.nextResultId = max(settings.nextResultId, maxRes + 1);
  settings.save();
}

}  // namespace cells
