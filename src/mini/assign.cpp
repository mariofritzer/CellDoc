#include "assign.h"
#include "cells.h"
#include "config_mini.h"
#include "hal_mini.h"
#include "slot.h"

using namespace mcfg;

namespace assign {

// Zelle gilt als eingesetzt ab 2,5 V, als entnommen unter 0,5 V
static constexpr float IN_V = 2.5f, OUT_V = 0.5f;
static constexpr uint32_t PENDING_MS = 120000;

static uint32_t slotNo[4] = {};
static String slotId[4];
static bool inserted[4] = {};
static uint32_t pendNo = 0, pendAt = 0;
static uint32_t checkAt[4] = {};
static JsonDocument chk[4];
static String pendId;

uint32_t no(int s) { return slotNo[s]; }
const String& id(int s) { return slotId[s]; }
void clear(int s) {
  slotNo[s] = 0;
  slotId[s] = "";
  checkAt[s] = 0;
  chk[s].clear();
}

bool check(int s, float& dv, float& days) {
  if (!chk[s]["dv"].is<float>()) return false;
  dv = chk[s]["dv"];
  days = chk[s]["days"];
  return true;
}

static void put(int s, uint32_t n, const String& i) {
  for (int k = 0; k < NSLOTS; k++)
    if (slotNo[k] == n) clear(k);
  slotNo[s] = n;
  slotId[s] = i;
  chk[s].clear();
  checkAt[s] = millis() + 2500;  // Spannungscheck, sobald sich die Spannung gesetzt hat
  hal.beep(40);
}

void onScan(uint32_t n, const String& i) {
  if (!n) {
    hal.beep(400);
    return;
  }
  hal.beep(60);
  // genau ein freier Schacht mit eingesetzter, noch nicht zugeordneter Zelle: sofort zuordnen
  int cand = -1, count = 0;
  for (int s = 0; s < NSLOTS; s++) {
    if (slots[s].active() || slotNo[s] || !inserted[s]) continue;
    cand = s;
    count++;
  }
  if (count == 1) {
    put(cand, n, i);
    pendNo = 0;
    return;
  }
  // sonst: in den nächsten Schacht, in den eine Zelle eingesetzt wird
  pendNo = n;
  pendId = i;
  pendAt = millis();
}

void tick(uint32_t now) {
  if (pendNo && now - pendAt > PENDING_MS) pendNo = 0;
  for (int s = 0; s < NSLOTS; s++) {
    float v = hal.m(s).v;
    bool was = inserted[s];
    if (!was && v > IN_V) {
      inserted[s] = true;
      if (pendNo && !slots[s].active()) {
        put(s, pendNo, pendId);
        pendNo = 0;
      }
    } else if (was && v < OUT_V) {
      inserted[s] = false;
      clear(s);
    }
    // läuft der Vorgang mit dieser Zelle, ist die Zuordnung erledigt
    if (slotNo[s] && slots[s].active()) clear(s);
    if (slotNo[s] && checkAt[s] && inserted[s] && (int32_t)(now - checkAt[s]) >= 0) {
      checkAt[s] = 0;
      chk[s].clear();
      cells::check(slotNo[s], v, chk[s].to<JsonObject>());
    }
  }
}

void stateJson(JsonObject root, JsonArray arr) {
  for (int s = 0; s < NSLOTS && s < (int)arr.size(); s++) {
    if (!slotNo[s]) continue;
    JsonObject a = arr[s]["assigned"].to<JsonObject>();
    a["no"] = slotNo[s];
    a["id"] = slotId[s];
    if (chk[s]["dv"].is<float>()) a["check"] = chk[s].as<JsonObjectConst>();
  }
  if (pendNo) {
    JsonObject p = root["pending"].to<JsonObject>();
    p["no"] = pendNo;
    p["id"] = pendId;
  }
}

}  // namespace assign
