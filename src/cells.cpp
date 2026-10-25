#include "cells.h"
#include <LittleFS.h>
#include "profiles.h"
#include "settings.h"
#include "soh.h"
#include "sync.h"

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

bool exists(uint32_t no) {
  if (!no || !LittleFS.exists(path(no))) return false;
  JsonDocument d;
  return load(no, d) && !(d["deleted"] | false);
}

bool load(uint32_t no, JsonDocument& d) {
  File f = LittleFS.open(path(no), "r");
  if (!f) return false;
  bool ok = !deserializeJson(d, f);
  f.close();
  return ok;
}

bool saveRaw(uint32_t no, JsonDocument& d) {
  File f = LittleFS.open(path(no), "w");
  if (!f) return false;
  serializeJson(d, f);  // eine Zeile, wichtig für die Datensicherung
  f.close();
  return true;
}

bool save(uint32_t no, JsonDocument& d) {
  // Änderungszeit + Gerät: beim Abgleich gewinnt die neuere Fassung
  uint32_t t = nowEpoch();
  uint32_t old = d["upd"] | 0u;
  d["upd"] = t > old ? t : old + 1;
  d["dev"] = settings.hostname;
  d["rcv"] = t;
  bool ok = saveRaw(no, d);
  peersync::changed();
  return ok;
}

bool remove(uint32_t no) {
  JsonDocument d;
  if (!load(no, d) || (d["deleted"] | false)) return false;
  JsonDocument t;
  t["no"] = no;
  t["id"] = d["id"];
  t["uid"] = d["uid"];
  t["deleted"] = true;
  return save(no, t);
}

uint32_t allocate() {
  uint32_t n = max(settings.nextCellNo, peersync::peerNextNo());
  settings.nextCellNo = n + 1;
  settings.save();
  peersync::reserve(n);
  return n;
}

static void fillNew(JsonDocument& d, uint32_t no) {
  d["no"] = no;
  d["id"] = makeId(no);
  d["uid"] = settings.hostname + "-" + String(nowEpoch()) + "-" + String(esp_random() & 0xffff, HEX);
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
  uint32_t no = allocate();
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
    if (!load(c.no, d) || (d["deleted"] | false)) return "Zelle " + makeId(c.no) + " nicht gefunden";
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
    c.no = allocate();
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
  // Verlauf mit Gerät, damit Partnergeräte die Tests auch sehen
  JsonArray h = d["hist"].is<JsonArray>() ? d["hist"].as<JsonArray>() : d["hist"].to<JsonArray>();
  JsonObject e = h.add<JsonObject>();
  e["dev"] = settings.hostname;
  e["id"] = r["id"];
  e["t"] = r["time"];
  e["cap"] = r["capMah"];
  e["ri"] = ri;
  e["score"] = score;
  while (h.size() > 20) h.remove(0);
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

void setRest(uint32_t no, float v, const char* src) {
  uint32_t t = nowEpoch();
  JsonDocument d;
  if (!t || !load(no, d) || (d["deleted"] | false)) return;
  JsonObject r = d["rest"].to<JsonObject>();
  r["v"] = roundf(v * 1000) / 1000;
  r["t"] = t;
  r["src"] = src;
  save(no, d);
}

String check(uint32_t no, float v, JsonObject out) {
  uint32_t t = nowEpoch();
  if (!t) return "Uhrzeit unbekannt";
  if (v < 1.0f) return "Keine Zelle erkannt";
  JsonDocument d;
  if (!load(no, d) || (d["deleted"] | false)) return "Zelle nicht gefunden";
  out["v"] = roundf(v * 1000) / 1000;
  JsonObject r = d["rest"];
  if (!r.isNull() && (r["t"] | 0u) && t > (r["t"] | 0u)) {
    float pv = r["v"] | 0.0f, days = (t - (r["t"] | 0u)) / 86400.0f;
    float dv = (v - pv) * 1000;
    out["prevV"] = pv;
    out["prevT"] = r["t"];
    out["days"] = roundf(days * 10) / 10;
    out["dv"] = roundf(dv);
    if (days >= 0.5f) out["mvDay"] = roundf(-dv / days * 10) / 10;
    JsonObject c = d["check"].to<JsonObject>();
    c["v"] = out["v"];
    c["t"] = t;
    c["prevV"] = pv;
    c["days"] = out["days"];
    c["dv"] = out["dv"];
    if (days >= 0.5f) c["mvDay"] = out["mvDay"];
  }
  JsonObject nr = d["rest"].to<JsonObject>();
  nr["v"] = roundf(v * 1000) / 1000;
  nr["t"] = t;
  nr["src"] = "check";
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
