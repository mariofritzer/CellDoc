// CellDoc – gerätespezifische Web-API (großer Prüfstand)
#include <ArduinoJson.h>
#include <WebServer.h>
#include "config.h"
#include "hal.h"
#include "profiles.h"
#include "settings.h"
#include "tester.h"
#include "web.h"
#include "web_device.h"

using web::readBody;
using web::sendError;
using web::sendJson;
using web::sendOk;
#define server web::http()

namespace {

static void handleState() {
  JsonDocument d;
  JsonObject o = d.to<JsonObject>();
  web::addCommon(o);
  tester.stateJson(o);
  sendJson(d);
}

static void handleCurve() {
  JsonDocument d;
  tester.curveJson(d.to<JsonArray>());
  sendJson(d);
}

static void handleInfo() {
  // Zelltypen und Chemien für die Weboberfläche
  JsonDocument d;
  JsonArray ch = d["chemistries"].to<JsonArray>();
  for (int k = 0; k < CHEMISTRY_COUNT; k++) {
    const Chemistry& c = CHEMISTRIES[k];
    JsonObject o = ch.add<JsonObject>();
    o["id"] = c.id;
    o["name"] = c.name;
    o["nominalV"] = c.nominalV;
    o["chargeV"] = c.chargeV;
    o["cutoffV"] = c.cutoffV;
    o["storageV"] = c.storageV;
    o["chargeable"] = c.charger != ChargerMode::Off;
    o["lithium"] = c.lithium;
  }
  JsonArray pr = d["presets"].to<JsonArray>();
  for (int k = 0; k < PRESET_COUNT; k++) {
    const CellPreset& p = PRESETS[k];
    JsonObject o = pr.add<JsonObject>();
    o["id"] = p.id;
    o["name"] = p.name;
    o["chem"] = p.chem;
    o["capMah"] = p.capMah;
    o["riNew"] = p.riNewMilli;
  }
  JsonObject lim = d["limits"].to<JsonObject>();
  lim["maxPowerW"] = cfg::MAX_POWER_W;
  lim["maxCurrentA"] = cfg::MAX_CURRENT_PORT_A;
  lim["maxCurrentB"] = cfg::MAX_CURRENT_A;
  d["nextCellNo"] = settings.nextCellNo;
  sendJson(d);
}

static bool parseJob(JsonDocument& b, JobConfig& c) {
  c.port = String(b["port"] | "A") == "B" ? PORT_B : PORT_A;
  c.chem = findChemistry(b["chem"] | "liion");
  c.cells = b["cells"] | 1;
  c.name = String(b["name"] | "");
  c.name.trim();
  if (c.name.length() > 40) c.name = c.name.substring(0, 40);
  c.presetId = String(b["preset"] | "");
  c.nomCapMah = b["nomCapMah"] | 0.0f;
  c.riNewMilli = b["riNew"] | 0.0f;
  c.currentA = b["currentA"] | 0.5f;
  c.cutoffV = b["cutoffV"] | 0.0f;
  c.preCharge = b["preCharge"] | true;
  c.restMin = b["restMin"] | settings.restMin;
  String after = b["after"] | "full";
  c.after = after == "none" ? After::None : after == "storage" ? After::Storage : After::Full;
  // Zelltyp-Vorgabe übernimmt Chemie, Kapazität und Ri, wenn nichts anderes angegeben ist
  const CellPreset* p = findPreset(c.presetId.c_str());
  if (p) {
    c.chem = findChemistry(p->chem);
    if (c.nomCapMah <= 0) c.nomCapMah = p->capMah;
    if (c.riNewMilli <= 0) c.riNewMilli = p->riNewMilli;
  }
  // Grenzen für den Entladeschluss: nie unter die sichere Grenze der Chemie
  if (c.cutoffV > 0 && c.cutoffV < c.chem->cutoffV - 0.3f) c.cutoffV = c.chem->cutoffV - 0.3f;
  return true;
}

static void handleStart() {
  JsonDocument b;
  if (!readBody(b)) return sendError("Ungültige Anfrage");
  JobConfig c;
  parseJob(b, c);
  String mode = b["mode"] | "test";
  String e;
  if (mode == "charge") e = tester.startManualCharge(c);
  else if (mode == "discharge") e = tester.startManualDischarge(c);
  else e = tester.startTest(c);
  if (e.length()) return sendError(e, 409);
  sendOk();
}

static void handleStop() {
  tester.stop("Von Hand gestoppt");
  sendOk();
}

static void handleAck() {
  tester.acknowledge();
  sendOk();
}

static void handleDiag() {
  JsonDocument d;
  d["inaA"] = hal.inaOk(PORT_A);
  d["inaB"] = hal.inaOk(PORT_B);
  d["dac"] = hal.m().dacOk;
  d["balancer"] = hal.m().balOk;
  JsonArray taps = d["taps"].to<JsonArray>();
  for (int k = 0; k < 6; k++) taps.add(roundf(hal.m().tap[k] * 1000) / 1000);
  TempSensorInfo s[8];
  int n = hal.tempSensors(s, 8);
  JsonArray ts = d["sensors"].to<JsonArray>();
  static const char* ROLE[] = {"cell", "pack", "sink"};
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

static void handleSensors() {
  JsonDocument b;
  if (!readBody(b)) return sendError("Ungültige Anfrage");
  static const char* ROLE[] = {"cell", "pack", "sink"};
  for (int r = 0; r < SENSOR_ROLES; r++) {
    const char* h = b[ROLE[r]] | "";
    settings.sensorSet[r] = hexToAddr(h, settings.sensorAddr[r]);
  }
  settings.save();
  sendOk();
}

// Kalibrierung: gemessener Istwert (Multimeter) gegen angezeigten Wert
static void handleCalibrate() {
  JsonDocument b;
  if (!readBody(b)) return sendError("Ungültige Anfrage");
  String what = b["what"] | "";
  float actual = b["actual"] | 0.0f;
  int port = String(b["port"] | "A") == "B" ? 1 : 0;
  const PortMeas& pm = hal.m().port[port];
  if (what == "reset") {
    for (int i = 0; i < 4; i++) {
      settings.vGain[i] = settings.iGain[i] = 1;
      settings.iOffset[i] = 0;
    }
    for (float& g : settings.balGain) g = 1;
  } else if (what == "voltage") {
    if (pm.v < 1 || actual < 1) return sendError("Für den Spannungsabgleich muss ein Akku angeschlossen sein");
    settings.vGain[port] *= actual / pm.v;
  } else if (what == "current") {
    float shown = fabsf(pm.i);
    if (shown < 0.2f || actual < 0.2f) return sendError("Für den Stromabgleich mindestens 0,2 A fließen lassen");
    settings.iGain[port] *= actual / shown;
  } else if (what == "zero") {
    if (tester.phase() != Phase::Idle && tester.phase() != Phase::Done && tester.phase() != Phase::Error)
      return sendError("Nullabgleich nur ohne laufenden Vorgang");
    settings.iOffset[port] += pm.i;
  } else if (what == "balancer") {
    int tap = b["tap"] | 0;
    if (tap < 0 || tap >= 6) return sendError("Ungültiger Abgriff");
    float shown = hal.m().tap[tap];
    if (shown < 1 || actual < 1) return sendError("Am Abgriff liegt keine Spannung an");
    settings.balGain[tap] *= actual / shown;
  } else {
    return sendError("Unbekannte Kalibrierung");
  }
  // Grenzen über fromJson erzwingen
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
  Phase p = tester.phase();
  return p != Phase::Idle && p != Phase::Done && p != Phase::Error;
}

void killAll() { hal.killAll(); }

void idle() { hal.update(millis()); }

}  // namespace device
