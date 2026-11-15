#include "stats.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

#include "config_mini.h"
#include "hal_mini.h"
#include "web.h"

using namespace mcfg;

namespace stats {

static const char* FILE_PATH = "/stats.json";
static uint32_t tests = 0, since = 0;
static double whIn = 0, whOut = 0, mahIn = 0, mahOut = 0, hours = 0;
static uint32_t lastMs = 0, lastSave = 0;
static bool dirty = false;

static void save() {
  JsonDocument d;
  d["tests"] = tests;
  d["whIn"] = whIn;
  d["whOut"] = whOut;
  d["mahIn"] = mahIn;
  d["mahOut"] = mahOut;
  d["hours"] = hours;
  d["since"] = since;
  File f = LittleFS.open(FILE_PATH, "w");
  if (!f) return;
  serializeJson(d, f);
  f.close();
  dirty = false;
}

void begin() {
  File f = LittleFS.open(FILE_PATH, "r");
  if (!f) return;
  JsonDocument d;
  if (!deserializeJson(d, f)) {
    tests = d["tests"] | 0u;
    whIn = d["whIn"] | 0.0;
    whOut = d["whOut"] | 0.0;
    mahIn = d["mahIn"] | 0.0;
    mahOut = d["mahOut"] | 0.0;
    hours = d["hours"] | 0.0;
    since = d["since"] | 0u;
  }
  f.close();
}

void update(uint32_t now) {
  if (!lastMs) { lastMs = now; lastSave = now; return; }
  if (now - lastMs < 1000) return;
  double dt = (now - lastMs) / 3600000.0;  // h
  lastMs = now;
  hours += dt;
  if (!since) {
    time_t t = time(nullptr);
    if (t > 1700000000) since = uint32_t(t);
  }
  for (int s = 0; s < NSLOTS; s++) {
    const SlotMeas& m = hal.m(s);
    if (!m.ok || fabsf(m.i) < 0.01f) continue;
    if (m.i > 0) { mahIn += m.i * 1000 * dt; whIn += m.p * dt; }
    else { mahOut += -m.i * 1000 * dt; whOut += fabsf(m.p) * dt; }
    dirty = true;
  }
  // alle 15 min sichern, damit der Flash geschont wird
  if (now - lastSave > 900000UL) {
    lastSave = now;
    save();
  }
}

void testDone() {
  tests++;
  save();
}

static void handleGet() {
  JsonDocument d;
  d["tests"] = tests;
  d["whIn"] = whIn;
  d["whOut"] = whOut;
  d["mahIn"] = mahIn;
  d["mahOut"] = mahOut;
  d["hours"] = hours;
  d["since"] = since;
  d["uptime"] = millis() / 1000;
  String out;
  serializeJson(d, out);
  web::http().send(200, "application/json", out);
}

static void handleReset() {
  tests = 0;
  whIn = whOut = mahIn = mahOut = hours = 0;
  time_t t = time(nullptr);
  since = t > 1700000000 ? uint32_t(t) : 0;
  save();
  web::http().send(200, "application/json", "{\"ok\":true}");
}

void routes() {
  web::http().on("/api/stats", HTTP_GET, handleGet);
  web::http().on("/api/stats/reset", HTTP_POST, handleReset);
}

}  // namespace stats
