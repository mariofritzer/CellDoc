// CellDoc Mini – gerätespezifische Web-API (mehrere Schächte)
#include <ArduinoJson.h>
#include <WebServer.h>
#include "assign.h"
#include "cells.h"
#include "display.h"
#include "hal_mini.h"
#include "profiles.h"
#include "settings.h"
#include "slot.h"
#include "web.h"
#include "web_device.h"

using namespace mcfg;
using web::readBody;
using web::sendError;
using web::sendJson;
using web::sendOk;
#define server web::http()

namespace {

int slotArg(JsonDocument* b = nullptr) {
  int s = 1;
  if (b && (*b)["slot"].is<int>()) s = (*b)["slot"];
  else if (server.hasArg("slot")) s = server.arg("slot").toInt();
  if (s < 1 || s > NSLOTS) s = 1;
  return s - 1;
}

void handleState() {
  JsonDocument d;
  JsonObject o = d.to<JsonObject>();
  web::addCommon(o);
  device::fillState(o);
  sendJson(d);
}

void fillStateImpl(JsonObject o) {
  o["mini"] = true;
  o["slotCount"] = NSLOTS;
  o["oled"] = display::available();
  JsonObject lim = o["limits"].to<JsonObject>();
  lim["chargeWarn"] = settings.chargeWarnC;
  lim["chargeStop"] = settings.chargeStopC;
  lim["dischargeWarn"] = settings.dischargeWarnC;
  lim["dischargeStop"] = settings.dischargeStopC;
  lim["sinkWarn"] = settings.sinkWarnC;
  lim["sinkStop"] = min(settings.sinkStopC, SINK_STOP_C);
  JsonObject f = o["fan"].to<JsonObject>();
  f["duty"] = hal.fanDuty();
  f["rpm"] = 0;
  JsonObject b = o["bal"].to<JsonObject>();
  b["ok"] = false;
  b["cells"].to<JsonArray>();
  JsonArray arr = o["slots"].to<JsonArray>();
  for (int s = 0; s < NSLOTS; s++) slots[s].stateJson(arr.add<JsonObject>());
  assign::stateJson(o, arr);
}

void handleCurve() {
  JsonDocument d;
  slots[slotArg()].curveJson(d.to<JsonArray>());
  sendJson(d);
}

void handleInfo() {
  JsonDocument d;
  JsonArray ch = d["chemistries"].to<JsonArray>();
  const Chemistry& c = *findChemistry("liion");
  JsonObject co = ch.add<JsonObject>();
  co["id"] = c.id;
  co["name"] = c.name;
  co["nominalV"] = c.nominalV;
  co["chargeV"] = c.chargeV;
  co["cutoffV"] = c.cutoffV;
  co["storageV"] = c.storageV;
  co["chargeable"] = true;
  co["lithium"] = true;
  JsonArray pr = d["presets"].to<JsonArray>();
  for (int k = 0; k < PRESET_COUNT; k++) {
    const CellPreset& p = PRESETS[k];
    if (strcmp(p.chem, "liion") != 0 || !strstr(p.name, "18650")) continue;
    JsonObject o = pr.add<JsonObject>();
    o["id"] = p.id;
    o["name"] = p.name;
    o["chem"] = p.chem;
    o["capMah"] = p.capMah;
    o["riNew"] = p.riNewMilli;
  }
  JsonObject lim = d["limits"].to<JsonObject>();
  lim["maxCurrentA"] = MAX_CURRENT_A;
  lim["maxCurrentB"] = 0;
  lim["maxPowerW"] = MAX_TOTAL_W;
  d["slots"] = NSLOTS;
  d["mini"] = true;
  d["nextCellNo"] = settings.nextCellNo;
  sendJson(d);
}

void handleStart() {
  JsonDocument b;
  if (!readBody(b)) return sendError("Ungültige Anfrage");
  int s = slotArg(&b);
  SlotJob j;
  j.name = String(b["name"] | "");
  j.name.trim();
  if (j.name.length() > 40) j.name = j.name.substring(0, 40);
  j.presetId = String(b["preset"] | "");
  j.cellNo = b["cell"].is<const char*>() ? cells::parseRef(b["cell"].as<const char*>()) : (b["cell"] | 0u);
  j.nomCapMah = b["nomCapMah"] | 0.0f;
  j.riNewMilli = b["riNew"] | 0.0f;
  j.currentA = b["currentA"] | 0.5f;
  j.cutoffV = b["cutoffV"] | 3.0f;
  j.preCharge = b["preCharge"] | true;
  j.restMin = b["restMin"] | settings.restMin;
  j.cycles = b["cycles"] | 1;
  String after = b["after"] | "storage";
  j.after = after == "none" ? After::None : after == "full" ? After::Full : After::Storage;
  const CellPreset* p = findPreset(j.presetId.c_str());
  if (p) {
    if (j.nomCapMah <= 0) j.nomCapMah = p->capMah;
    if (j.riNewMilli <= 0) j.riNewMilli = p->riNewMilli;
  }
  String mode = b["mode"] | "test";
  String e = mode == "charge" ? slots[s].startCharge(j) : mode == "discharge" ? slots[s].startDischarge(j) : slots[s].startTest(j);
  if (e.length()) return sendError(e, 409);
  sendOk();
}

void handleStop() {
  JsonDocument b;
  readBody(b);
  slots[slotArg(&b)].stop("Von Hand gestoppt");
  sendOk();
}

void handleAck() {
  JsonDocument b;
  readBody(b);
  slots[slotArg(&b)].acknowledge();
  sendOk();
}

const char* ROLE[] = {"cell1", "cell2", "cell3", "cell4", "sink"};

void handleDiag() {
  JsonDocument d;
  JsonArray ina = d["ina"].to<JsonArray>();
  for (int s = 0; s < NSLOTS; s++) ina.add(hal.inaOk(s));
  d["inaA"] = hal.inaOk(0);
  d["inaB"] = NSLOTS > 1 ? hal.inaOk(1) : false;
  d["dac"] = hal.dacOk();
  d["oled"] = display::available();
  d["balancer"] = false;
  d["taps"].to<JsonArray>();
  HalMini::SensorInfo s[8];
  int n = hal.tempSensors(s, 8);
  JsonArray ts = d["sensors"].to<JsonArray>();
  for (int k = 0; k < n; k++) {
    JsonObject o = ts.add<JsonObject>();
    o["addr"] = addrToHex(s[k].addr);
    o["t"] = s[k].t;
    o["role"] = s[k].role >= 0 ? ROLE[s[k].role] : "";
  }
  d["heap"] = ESP.getFreeHeap();
  d["chip"] = ESP.getChipModel();
  sendJson(d);
}

void handleSensors() {
  JsonDocument b;
  if (!readBody(b)) return sendError("Ungültige Anfrage");
  for (int r = 0; r < SENSOR_ROLES; r++) {
    const char* h = b[ROLE[r]] | "";
    settings.sensorSet[r] = hexToAddr(h, settings.sensorAddr[r]);
  }
  settings.save();
  sendOk();
}

void handleCalibrate() {
  JsonDocument b;
  if (!readBody(b)) return sendError("Ungültige Anfrage");
  String what = b["what"] | "";
  float actual = b["actual"] | 0.0f;
  int s = slotArg(&b);
  const SlotMeas& m = hal.m(s);
  if (what == "reset") {
    for (int i = 0; i < 4; i++) {
      settings.vGain[i] = settings.iGain[i] = 1;
      settings.iOffset[i] = 0;
    }
  } else if (what == "voltage") {
    if (m.v < 1 || actual < 1) return sendError("Für den Spannungsabgleich muss eine Zelle eingesetzt sein");
    settings.vGain[s] *= actual / m.v;
  } else if (what == "current") {
    float shown = fabsf(m.i);
    if (shown < 0.2f || actual < 0.2f) return sendError("Für den Stromabgleich mindestens 0,2 A fließen lassen");
    settings.iGain[s] *= actual / shown;
  } else if (what == "zero") {
    if (slots[s].active()) return sendError("Nullabgleich nur ohne laufenden Vorgang");
    settings.iOffset[s] += m.i;
  } else {
    return sendError("Unbekannte Kalibrierung");
  }
  JsonDocument tmp;
  settings.toJson(tmp.to<JsonObject>(), true);
  settings.fromJson(tmp.as<JsonObjectConst>());
  settings.save();
  sendOk();
}

}  // namespace

namespace device {

void routes() {
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/curve", HTTP_GET, handleCurve);
  server.on("/api/info", HTTP_GET, handleInfo);
  server.on("/api/start", HTTP_POST, handleStart);
  server.on("/api/stop", HTTP_POST, handleStop);
  server.on("/api/ack", HTTP_POST, handleAck);
  server.on("/api/diag", HTTP_GET, handleDiag);
  server.on("/api/sensors", HTTP_POST, handleSensors);
  server.on("/api/sensors/rescan", HTTP_POST, [] {
    hal.rescanTempSensors();
    sendOk();
  });
  server.on("/api/calibrate", HTTP_POST, handleCalibrate);
}

bool busy() {
  for (int s = 0; s < NSLOTS; s++)
    if (slots[s].active()) return true;
  return false;
}

void killAll() { hal.killAll(); }

void fillState(JsonObject o) { fillStateImpl(o); }

String cellVoltage(JsonDocument& b, float& v) {
  int s = slotArg(&b);
  if (slots[s].active()) return "Schacht " + String(s + 1) + " ist gerade belegt";
  if (!hal.inaOk(s)) return "Strommesser von Schacht " + String(s + 1) + " antwortet nicht";
  v = hal.m(s).v;
  return "";
}

bool cellInUse(uint32_t no) {
  for (int s = 0; s < NSLOTS; s++)
    if (slots[s].active() && slots[s].cellNo() == no) return true;
  return false;
}

uint8_t ledState() {
  uint8_t st = 0;
  for (int s = 0; s < NSLOTS; s++) {
    Phase p = slots[s].phase();
    uint8_t v = p == Phase::Error ? 4 : (p == Phase::Discharge || p == Phase::ManualDischarge) ? 2 : p == Phase::Idle ? 0 : p == Phase::Done ? 3 : 1;
    // Fehler vor Entladen vor Laden vor Fertig
    static const uint8_t prio[] = {0, 2, 3, 1, 4};
    if (prio[v] > prio[st]) st = v;
  }
  return st;
}

const char* model() {
  static char m[16];
  snprintf(m, sizeof(m), "CellDoc Mini %d", NSLOTS);
  return m;
}

void mqttEntities(std::vector<MqttEntity>& e) {
  for (int s = 0; s < NSLOTS; s++) {
    String k = String(s + 1), p = "value_json.slots[" + String(s) + "]", o = "s" + k + "_", n = "Schacht " + k + " ";
    e.push_back({o + "phase", n + "Status", "phase:" + p + ".phase", "", ""});
    e.push_back({o + "zelle", n + "Zelle", "{{ " + p + ".job.cellId | default('') }}", "", ""});
    e.push_back({o + "spannung", n + "Spannung", "{{ " + p + ".ports[0].v }}", "V", "voltage"});
    e.push_back({o + "strom", n + "Strom", "{{ " + p + ".ports[0].i }}", "A", "current"});
    e.push_back({o + "ladung", n + "Ladung", "{{ " + p + ".mah | default(0) | round(0) }}", "mAh", ""});
    e.push_back({o + "ladezustand", n + "Ladezustand", "{{ " + p + ".soc if " + p + ".soc >= 0 else None }}", "%", "battery"});
    e.push_back({o + "temp", n + "Temperatur", "{{ " + p + ".temps.cell | default(None) }}", "°C", "temperature"});
    e.push_back({o + "kapazitaet", n + "Letzte Kapazität", "{{ " + p + ".last.cap | default(None) }}", "mAh", ""});
    e.push_back({o + "lebenszahl", n + "Letzte Lebenszahl", "{{ " + p + ".last.score | default(None) }}", "%", ""});
    e.push_back(MqttEntity(o + "stopp", n + "stoppen", "stop" + k, "", "", true));
  }
  e.push_back({"temp_kuehlkoerper", "Temperatur Kühlkörper", "{{ value_json.slots[0].temps.sink | default(None) }}", "°C", "temperature"});
}

void onScan(uint32_t no, const String& id) { assign::onScan(no, id); }
void scanTick(uint32_t now) { assign::tick(now); }
void scanState(JsonObject) {}  // steht schon in den Schächten

void mqttCommand(const String& cmd) {
  if (!cmd.startsWith("stop")) return;
  int s = cmd.substring(4).toInt();
  for (int k = 0; k < NSLOTS; k++)
    if (s == 0 || s == k + 1) slots[k].stop("Über Home Assistant gestoppt");
}

void idle() { hal.update(millis()); }

}  // namespace device
