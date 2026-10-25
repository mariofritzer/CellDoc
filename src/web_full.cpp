// CellDoc – gerätespezifische Web-API (großer Prüfstand)
#include <ArduinoJson.h>
#include <WebServer.h>
#include "cells.h"
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
  device::fillState(o);
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
  c.cellNo = b["cell"].is<const char*>() ? cells::parseRef(b["cell"].as<const char*>()) : (b["cell"] | 0u);
  c.nomCapMah = b["nomCapMah"] | 0.0f;
  c.riNewMilli = b["riNew"] | 0.0f;
  c.currentA = b["currentA"] | 0.5f;
  c.cutoffV = b["cutoffV"] | 0.0f;
  c.preCharge = b["preCharge"] | true;
  c.restMin = b["restMin"] | settings.restMin;
  c.cycles = b["cycles"] | 1;
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

bool busy() { return tester.active(); }

void fillState(JsonObject o) { tester.stateJson(o); }

String cellVoltage(JsonDocument& b, float& v) {
  Port p = String(b["port"] | "A") == "B" ? PORT_B : PORT_A;
  if (tester.active()) return "Während eines Vorgangs nicht möglich";
  int n = b["cells"] | 1;
  if (n < 1) n = 1;
  const PortMeas& pm = hal.m().port[p];
  if (!pm.ok) return "Strommesser antwortet nicht";
  v = pm.v / n;
  return "";
}

bool cellInUse(uint32_t no) { return tester.active() && tester.cellNo() == no; }

const char* model() { return "CellDoc"; }

void mqttEntities(std::vector<MqttEntity>& e) {
  e.push_back({"phase", "Status", "phase:value_json.phase", "", ""});
  e.push_back({"zelle", "Zelle", "{{ value_json.job.cellId | default('') }}", "", ""});
  e.push_back({"spannung_a", "Spannung A", "{{ value_json.ports[0].v }}", "V", "voltage"});
  e.push_back({"strom_a", "Strom A", "{{ value_json.ports[0].i }}", "A", "current"});
  e.push_back({"spannung_b", "Spannung B", "{{ value_json.ports[1].v }}", "V", "voltage"});
  e.push_back({"strom_b", "Strom B", "{{ value_json.ports[1].i }}", "A", "current"});
  e.push_back({"ladung", "Ladung", "{{ value_json.mah | default(0) | round(0) }}", "mAh", ""});
  e.push_back({"ladezustand", "Ladezustand", "{{ value_json.soc if value_json.soc >= 0 else None }}", "%", "battery"});
  e.push_back({"temp_zelle", "Temperatur Zelle", "{{ value_json.temps.cell | default(None) }}", "°C", "temperature"});
  e.push_back({"temp_pack", "Temperatur Pack", "{{ value_json.temps.pack | default(None) }}", "°C", "temperature"});
  e.push_back({"temp_kuehlkoerper", "Temperatur Kühlkörper", "{{ value_json.temps.sink | default(None) }}", "°C", "temperature"});
  e.push_back({"luefter", "Lüfter", "{{ value_json.fan.rpm }}", "rpm", ""});
  e.push_back({"letzte_zelle", "Letzte Zelle", "{{ value_json.last.cellId | default('') }}", "", ""});
  e.push_back({"letzte_kapazitaet", "Letzte Kapazität", "{{ value_json.last.cap | default(None) }}", "mAh", ""});
  e.push_back({"letzte_lebenszahl", "Letzte Lebenszahl", "{{ value_json.last.score | default(None) }}", "%", ""});
  e.push_back(MqttEntity("stopp", "Stoppen", "stop", "", "", true));
}

void mqttCommand(const String& cmd) {
  if (cmd == "stop") tester.stop("Über Home Assistant gestoppt");
}

// Gescannte Zelle wird im Startdialog vorausgewählt (10 Minuten lang)
static uint32_t scanNo = 0, scanAt = 0, checkAt = 0;
static String scanId;
static bool checked = false;
static JsonDocument checkRes;  // Ergebnis des Spannungschecks

void onScan(uint32_t no, const String& id) {
  if (!no) {
    hal.beep(400);
    return;
  }
  hal.beep(60);
  scanNo = no;
  scanId = id;
  scanAt = millis();
  checkAt = 0;
  checked = false;
  checkRes.clear();
}

void scanTick(uint32_t now) {
  if (scanNo && (now - scanAt > 600000UL || (tester.active() && tester.cellNo() == scanNo))) scanNo = 0;
  if (!scanNo || checked || tester.active()) return;
  // Spannungscheck: steckt die gescannte Zelle an A, nach 2,5 s Ruhespannung vergleichen
  const PortMeas& pm = hal.m().port[PORT_A];
  if (pm.v < 2.5f) {
    checkAt = 0;
    return;
  }
  if (!checkAt) checkAt = now + 2500;
  if ((int32_t)(now - checkAt) < 0) return;
  checked = true;
  checkRes.clear();
  cells::check(scanNo, pm.v, checkRes.to<JsonObject>());
}

void scanState(JsonObject o) {
  if (!scanNo) return;
  JsonObject a = o["assigned"].to<JsonObject>();
  a["no"] = scanNo;
  a["id"] = scanId;
  if (checked && checkRes["dv"].is<float>()) a["check"] = checkRes.as<JsonObjectConst>();
}

void killAll() { hal.killAll(); }

void idle() { hal.update(millis()); }

}  // namespace device
