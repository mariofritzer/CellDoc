#include "web.h"
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include "config.h"
#include "mqtt.h"
#include "scanner.h"
#include "sync.h"
#include "settings.h"
#include "storage.h"
#include "web_device.h"
#include "web_index.h"  // wird beim Bauen aus web/index.html erzeugt

namespace web {

static WebServer srv(80);
static WebServer& server = srv;
static DNSServer dns;
static bool ap = false;
static bool restartPending = false;
static uint32_t restartAt = 0;
static bool otaOk = false;
static String otaError;

bool apMode() { return ap; }
WebServer& http() { return srv; }

// ---------------------------------------------------------------------------
// Hilfen

void sendJson(JsonDocument& d, int code) {
  String out;
  serializeJson(d, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", out);
}

void sendError(const String& msg, int code) {
  JsonDocument d;
  d["error"] = msg;
  sendJson(d, code);
}

void sendOk() {
  JsonDocument d;
  d["ok"] = true;
  sendJson(d);
}

bool readBody(JsonDocument& d) {
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
  b.remove("nextCellNo");
  b.remove("nextResultId");
  if (b["mqttPass"].is<const char*>() && !strlen(b["mqttPass"].as<const char*>())) b.remove("mqttPass");  // leer = unverändert
  settings.fromJson(b.as<JsonObjectConst>());
  settings.save();
  mqtt::reconfigure();
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

void addCommon(JsonObject o) {
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
  o["prefix"] = settings.cellPrefix;
  o["host"] = settings.hostname;
  o["mqtt"] = mqtt::status();
  scanner::fill(o);
  device::scanState(o);
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
    if (device::busy()) {
      otaError = "Während eines Tests ist kein Update möglich";
      return;
    }
    device::killAll();
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
  scanner::begin();
  WiFi.setHostname(settings.hostname.c_str());
  if (settings.wifiSsid.length()) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPass.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
      device::idle();  // Messungen laufen weiter, Last ist ohnehin aus
      delay(50);
    }
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WLAN verbunden: http://%s  (http://%s.local)\n", WiFi.localIP().toString().c_str(),
                  settings.hostname.c_str());
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.google.com");
  } else {
    startAp();
  }
  if (MDNS.begin(settings.hostname.c_str())) {
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("celldoc", "tcp", 80);  // damit Partnergeräte uns finden
  }

  server.on("/", HTTP_GET, handleIndex);
  server.on("/api/result", HTTP_GET, handleResult);
  server.on("/api/result/delete", HTTP_POST, handleResultDelete);
  server.on("/api/settings", HTTP_GET, handleSettingsGet);
  server.on("/api/settings", HTTP_POST, handleSettingsPost);
  device::routes();
  webdata::routes();
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
  mqtt::update();
  scanner::update();
  peersync::update();
  if (restartPending && millis() > restartAt) {
    device::killAll();
    delay(100);
    ESP.restart();
  }
}

}  // namespace web
