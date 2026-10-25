#include "web.h"
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include "config.h"
#include "hal.h"
#include "profiles.h"
#include "settings.h"
#include "storage.h"
#include "tester.h"
#include "web_index.h"  // wird beim Bauen aus web/index.html erzeugt

namespace web {

static WebServer server(80);
static DNSServer dns;
static bool ap = false;
static bool restartPending = false;
static uint32_t restartAt = 0;
static bool otaOk = false;
static String otaError;

bool apMode() { return ap; }

// ---------------------------------------------------------------------------
// Hilfen

static void sendJson(JsonDocument& d, int code = 200) {
  String out;
  serializeJson(d, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", out);
}

static void sendError(const String& msg, int code = 400) {
  JsonDocument d;
  d["error"] = msg;
  sendJson(d, code);
}

static void sendOk() {
  JsonDocument d;
  d["ok"] = true;
  sendJson(d);
}

static bool readBody(JsonDocument& d) {
  if (!server.hasArg("plain")) return false;
  return !deserializeJson(d, server.arg("plain"));
}

static void scheduleRestart() {
  restartPending = true;
  restartAt = millis() + 1500;
}

// ---------------------------------------------------------------------------
// Handler

static void handleIndex() {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html; charset=utf-8", (const char*)INDEX_GZ, INDEX_GZ_LEN);
}

static void handleState() {
  JsonDocument d;
  JsonObject o = d.to<JsonObject>();
  o["version"] = CELLDOC_VERSION;
  o["uptime"] = millis() / 1000;
  JsonObject w = o["wifi"].to<JsonObject>();
  w["ap"] = ap;
  w["ssid"] = ap ? WiFi.softAPSSID() : WiFi.SSID();
  w["ip"] = ap ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  w["rssi"] = ap ? 0 : WiFi.RSSI();
  time_t now = time(nullptr);
  o["time"] = now > 1700000000 ? uint32_t(now) : 0;
  o["riInOhm"] = settings.riInOhm;
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

static void handleResults() {
  JsonDocument d;
  storage::listResults(d.to<JsonArray>());
  sendJson(d);
}

static void handleResult() {
  uint32_t id = server.arg("id").toInt();
  bool curve = server.arg("curve") == "1";
  String path = String(curve ? "/c/" : "/r/") + id + ".json";
  File f = LittleFS.open(path, "r");
  if (!f) return sendError("Ergebnis nicht gefunden", 404);
  server.sendHeader("Cache-Control", "no-store");
  server.streamFile(f, "application/json");
  f.close();
}

static void handleResultDelete() {
  uint32_t id = server.arg("id").toInt();
  if (!storage::removeResult(id)) return sendError("Ergebnis nicht gefunden", 404);
  sendOk();
}

static void handleSettingsGet() {
  JsonDocument d;
  settings.toJson(d.to<JsonObject>(), false);
  d["storageUsed"] = storage::usedBytes();
  d["storageTotal"] = storage::totalBytes();
  sendJson(d);
}

static void handleSettingsPost() {
  JsonDocument b;
  if (!readBody(b)) return sendError("Ungültige Anfrage");
  // WLAN-Daten laufen über /api/wifi
  b.remove("wifiSsid");
  b.remove("wifiPass");
  settings.fromJson(b.as<JsonObjectConst>());
  settings.save();
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
    for (int i = 0; i < 2; i++) {
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

static void handleWifiScan() {
  JsonDocument d;
  int n = WiFi.scanNetworks();
  JsonArray a = d.to<JsonArray>();
  for (int k = 0; k < n && k < 20; k++) {
    JsonObject o = a.add<JsonObject>();
    o["ssid"] = WiFi.SSID(k);
    o["rssi"] = WiFi.RSSI(k);
    o["open"] = WiFi.encryptionType(k) == WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  sendJson(d);
}

static void handleWifiSave() {
  JsonDocument b;
  if (!readBody(b)) return sendError("Ungültige Anfrage");
  settings.wifiSsid = String(b["ssid"] | "");
  settings.wifiPass = String(b["pass"] | "");
  settings.save();
  sendOk();
  scheduleRestart();
}

static void handleRestart() {
  sendOk();
  scheduleRestart();
}

static bool jobRunning() {
  Phase p = tester.phase();
  return p != Phase::Idle && p != Phase::Done && p != Phase::Error;
}

static void handleUpdateDone() {
  if (!otaOk) return sendError(otaError.length() ? otaError : "Update fehlgeschlagen", 500);
  sendOk();
  scheduleRestart();
}

static void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    otaOk = false;
    otaError = "";
    if (jobRunning()) {
      otaError = "Während eines Tests ist kein Update möglich";
      return;
    }
    hal.killAll();
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) otaError = Update.errorString();
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (otaError.length()) return;
    if (Update.write(up.buf, up.currentSize) != up.currentSize) otaError = Update.errorString();
  } else if (up.status == UPLOAD_FILE_END) {
    if (otaError.length()) {
      Update.abort();
      return;
    }
    if (Update.end(true)) otaOk = true;
    else otaError = Update.errorString();
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    otaError = "Upload abgebrochen";
  }
}

static void handleNotFound() {
  if (ap) {
    // Captive Portal: alles auf die Startseite umleiten
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
    return;
  }
  sendError("Nicht gefunden", 404);
}

// ---------------------------------------------------------------------------

static void startAp() {
  ap = true;
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char name[24];
  snprintf(name, sizeof(name), "CellDoc-%02X%02X", mac[4], mac[5]);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(name, cfg::AP_PASSWORD);
  dns.start(53, "*", WiFi.softAPIP());
  Serial.printf("Hotspot %s, Passwort %s, http://%s\n", name, cfg::AP_PASSWORD,
                WiFi.softAPIP().toString().c_str());
}

void begin() {
  WiFi.setHostname(cfg::HOSTNAME);
  if (settings.wifiSsid.length()) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPass.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
      hal.update(millis());  // Messungen laufen weiter, Last ist ohnehin aus
      delay(50);
    }
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WLAN verbunden: http://%s  (http://%s.local)\n", WiFi.localIP().toString().c_str(),
                  cfg::HOSTNAME);
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.google.com");
  } else {
    startAp();
  }
  if (MDNS.begin(cfg::HOSTNAME)) MDNS.addService("http", "tcp", 80);

  server.on("/", HTTP_GET, handleIndex);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/curve", HTTP_GET, handleCurve);
  server.on("/api/info", HTTP_GET, handleInfo);
  server.on("/api/start", HTTP_POST, handleStart);
  server.on("/api/stop", HTTP_POST, handleStop);
  server.on("/api/ack", HTTP_POST, handleAck);
  server.on("/api/results", HTTP_GET, handleResults);
  server.on("/api/result", HTTP_GET, handleResult);
  server.on("/api/result/delete", HTTP_POST, handleResultDelete);
  server.on("/api/settings", HTTP_GET, handleSettingsGet);
  server.on("/api/settings", HTTP_POST, handleSettingsPost);
  server.on("/api/diag", HTTP_GET, handleDiag);
  server.on("/api/sensors", HTTP_POST, handleSensors);
  server.on("/api/sensors/rescan", HTTP_POST, [] {
    hal.rescanTempSensors();
    sendOk();
  });
  server.on("/api/calibrate", HTTP_POST, handleCalibrate);
  server.on("/api/wifi/scan", HTTP_GET, handleWifiScan);
  server.on("/api/wifi", HTTP_POST, handleWifiSave);
  server.on("/api/restart", HTTP_POST, handleRestart);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.onNotFound(handleNotFound);
  server.begin();
}

void update() {
  if (ap) dns.processNextRequest();
  server.handleClient();
  if (restartPending && millis() > restartAt) {
    hal.killAll();
    delay(100);
    ESP.restart();
  }
}

}  // namespace web
