#include "web.h"
#include "evlog.h"
#include "notify.h"
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include "config.h"
#include "improv.h"
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

void restartSoon(uint32_t ms) {
  restartPending = true;
  restartAt = millis() + ms;
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
  // Passwörter: leer = unverändert, löschen nur ausdrücklich
  for (const char* k : {"mqttPass", "tgTok", "pass2", "apPass"}) {
    if (b[k].is<const char*>() && !strlen(b[k].as<const char*>())) b.remove(k);
    if (b[String(k) + "Clear"] | false) b[k] = "";
  }
  String tz = settings.tz;
  settings.fromJson(b.as<JsonObjectConst>());
  settings.save();
  if (settings.tz != tz) {
    setenv("TZ", settings.tz.c_str(), 1);
    tzset();
  }
  mqtt::reconfigure();
  sendOk();
}

// Werkseinstellungen: "settings" = Einstellungen zurück (WLAN bleibt), "all" = alles löschen wie neu
static void handleFactory() {
  JsonDocument b;
  if (!readBody(b)) return sendError("Ungültige Anfrage");
  if (device::busy()) return sendError("Während eines Vorgangs nicht möglich", 409);
  String what = b["what"] | "";
  if (what == "settings") {
    // WLAN, Zähler, Kalibrierung und Fühlerzuordnung gehören zur Hardware und bleiben
    Settings old = settings;
    settings = Settings();
    settings.wifiSsid = old.wifiSsid;
    settings.wifiPass = old.wifiPass;
    settings.nextCellNo = old.nextCellNo;
    settings.nextResultId = old.nextResultId;
    memcpy(settings.vGain, old.vGain, sizeof(old.vGain));
    memcpy(settings.iGain, old.iGain, sizeof(old.iGain));
    memcpy(settings.iOffset, old.iOffset, sizeof(old.iOffset));
    memcpy(settings.rLead, old.rLead, sizeof(old.rLead));
    memcpy(settings.balGain, old.balGain, sizeof(old.balGain));
    memcpy(settings.sensorAddr, old.sensorAddr, sizeof(old.sensorAddr));
    memcpy(settings.sensorSet, old.sensorSet, sizeof(old.sensorSet));
    settings.save();
    evlog::warn(0, "Einstellungen auf Werkseinstellungen zurückgesetzt (WLAN, Kalibrierung, Fühler, Bestand und Ergebnisse bleiben)");
  } else if (what == "all") {
    device::killAll();
    LittleFS.format();
    WiFi.disconnect(true, true);
  } else {
    return sendError("Unbekannt");
  }
  sendOk();
  scheduleRestart();
}

static void handleNotifyTest() {
  if (!notify::configured()) return sendError("Zuerst ntfy oder Telegram eintragen");
  if (ap || WiFi.status() != WL_CONNECTED) return sendError("Kein Internet: CellDoc ist nicht mit dem WLAN verbunden", 409);
  notify::send('t', "Test", "Benachrichtigungen funktionieren.");
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
  // leeres Netzwerk = WLAN löschen: auch die im Funkchip gespeicherten Zugangsdaten verwerfen,
  // damit CellDoc nach dem Neustart nur den Hotspot öffnet
  if (!settings.wifiSsid.length()) {
    delay(200);
    WiFi.disconnect(true, true);
  }
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
  if (!otaOk) {
    evlog::err(0, "Firmware-Update aus Datei fehlgeschlagen: " + (otaError.length() ? otaError : String("unbekannt")));
    return sendError(otaError.length() ? otaError : "Update fehlgeschlagen", 500);
  }
  evlog::info(0, "Firmware-Update aus Datei eingespielt, starte neu");
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
    evlog::info(0, "Firmware-Update aus Datei gestartet, Laden und Last aus");
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

// ---------------------------------------------------------------------------
// Update direkt von GitHub: neueste Vorabversion „latest“ des Repos.
// Läuft in einer eigenen Task, damit Messung und Weboberfläche weiterlaufen.

#ifndef CELLDOC_ASSET
#define CELLDOC_ASSET "celldoc"
#endif
static const char* GH_BASE = "https://github.com/mariofritzer/CellDoc/releases/download/latest/";
static volatile bool ghRunning = false;
static volatile int ghPct = 0;
static bool ghInstall = false;
static String ghState;  // "", "geprueft", "fertig", "fehler"
static String ghErr, ghLatest;

static void ghTask(void*) {
  WiFiClientSecure cl;
  cl.setInsecure();
  String state, err, latest;
  if (!ghInstall) {
    HTTPClient http;
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.setTimeout(10000);
    if (http.begin(cl, String(GH_BASE) + "version.txt")) {
      int code = http.GET();
      if (code == 200) {
        latest = http.getString();
        latest.trim();
        state = "geprueft";
      } else {
        err = "GitHub antwortet nicht (" + String(code) + ")";
      }
      http.end();
    } else {
      err = "GitHub nicht erreichbar";
    }
  } else {
    httpUpdate.rebootOnUpdate(false);
    httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    httpUpdate.onProgress([](int cur, int total) { ghPct = total > 0 ? int(int64_t(cur) * 100 / total) : 0; });
    t_httpUpdate_return r = httpUpdate.update(cl, String(GH_BASE) + CELLDOC_ASSET "-firmware.bin");
    if (r == HTTP_UPDATE_OK) {
      state = "fertig";
      ghPct = 100;
    } else {
      err = httpUpdate.getLastErrorString();
    }
  }
  if (err.length()) state = "fehler";
  ghState = state;
  ghErr = err;
  if (latest.length()) ghLatest = latest;
  if (state == "fertig") scheduleRestart();
  ghRunning = false;
  vTaskDelete(nullptr);
}

static void handleGhStatus() {
  JsonDocument r;
  r["current"] = CELLDOC_VERSION;
  r["asset"] = CELLDOC_ASSET "-firmware.bin";
  r["running"] = (bool)ghRunning;
  r["install"] = ghInstall;
  r["pct"] = (int)ghPct;
  static bool ghLogged = true;
  if (ghRunning) ghLogged = false;
  if (!ghRunning && !ghLogged && ghInstall) {
    ghLogged = true;
    if (ghState == "fertig") evlog::info(0, "Firmware-Update von GitHub geladen, starte neu");
    else if (ghState == "fehler") evlog::err(0, "Firmware-Update von GitHub fehlgeschlagen: " + ghErr);
  }
  if (!ghRunning) {
    r["state"] = ghState;
    if (ghErr.length()) r["error"] = ghErr;
    if (ghLatest.length()) {
      r["latest"] = ghLatest;
      r["newer"] = ghLatest != CELLDOC_VERSION;
    }
  }
  sendJson(r);
}

static void handleGhStart() {
  JsonDocument b;
  if (!readBody(b)) return sendError("Ungültige Anfrage");
  if (ghRunning) return sendError("Läuft bereits", 409);
  if (ap || WiFi.status() != WL_CONNECTED) return sendError("Kein Internet: CellDoc ist nicht mit dem WLAN verbunden", 409);
  bool install = String(b["action"] | "") == "install";
  if (install) {
    if (device::busy()) return sendError("Während eines Tests ist kein Update möglich", 409);
    evlog::info(0, "Firmware-Update von GitHub gestartet, Laden und Last aus");
    device::killAll();
  }
  ghInstall = install;
  ghState = "";
  ghErr = "";
  ghPct = 0;
  ghRunning = true;
  if (xTaskCreate(ghTask, "ghupd", 12288, nullptr, 1, nullptr) != pdPASS) {
    ghRunning = false;
    return sendError("Zu wenig Speicher frei", 500);
  }
  sendOk();
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
  String apName = settings.apName.length() ? settings.apName : String(name);
  const char* apPass = settings.apPass.length() >= 8 ? settings.apPass.c_str() : cfg::AP_PASSWORD;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apName.c_str(), apPass);
#if CONFIG_IDF_TARGET_ESP32C3
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
#endif
  dns.start(53, "*", WiFi.softAPIP());
  Serial.printf("Hotspot %s, Passwort %s, http://%s\n", apName.c_str(), apPass,
                WiFi.softAPIP().toString().c_str());
}

void begin() {
  scanner::begin();
  WiFi.setHostname(settings.hostname.c_str());
  String joined;
  if (settings.wifiSsid.length()) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    // feste Adresse, sonst DHCP
    IPAddress ip, gw, mask, dnsIp;
    if (ip.fromString(settings.ip) && gw.fromString(settings.gw)) {
      if (!mask.fromString(settings.mask)) mask = IPAddress(255, 255, 255, 0);
      if (!dnsIp.fromString(settings.dns)) dnsIp = gw;
      WiFi.config(ip, gw, mask, dnsIp);
    }
    // erst das Haupt-WLAN, dann das Ausweich-WLAN
    for (int n = 0; n < 2 && WiFi.status() != WL_CONNECTED; n++) {
      const String& ssid = n ? settings.ssid2 : settings.wifiSsid;
      const String& pass = n ? settings.pass2 : settings.wifiPass;
      if (!ssid.length()) continue;
      if (n) {
        evlog::warn(0, "WLAN „" + settings.wifiSsid + "“ nicht erreichbar, versuche Ausweich-WLAN „" + ssid + "“");
        WiFi.disconnect();
        delay(100);
      }
      WiFi.begin(ssid.c_str(), pass.c_str());
#if CONFIG_IDF_TARGET_ESP32C3
      WiFi.setTxPower(WIFI_POWER_8_5dBm);  // C3 SuperMini: Antenne verträgt volle Sendeleistung schlecht
#endif
      uint32_t t0 = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
        device::idle();  // Messungen laufen weiter, Last ist ohnehin aus
        improv::update();
        delay(50);
      }
      if (WiFi.status() == WL_CONNECTED) joined = ssid;
    }
  }
  if (WiFi.status() == WL_CONNECTED) {
    evlog::info(0, "WLAN „" + joined + "“ verbunden, Adresse " + WiFi.localIP().toString());
    Serial.printf("WLAN verbunden: http://%s  (http://%s.local)\n", WiFi.localIP().toString().c_str(),
                  settings.hostname.c_str());
    configTzTime(settings.tz.c_str(), settings.ntp.c_str(), "time.google.com");
    device::signal(1);
  } else {
    evlog::add(settings.wifiSsid.length() ? 'w' : 'i', 0,
               settings.wifiSsid.length() ? "WLAN „" + settings.wifiSsid + "“ nicht erreichbar, Hotspot geöffnet"
                                          : String("Kein WLAN eingerichtet, Hotspot geöffnet"));
    startAp();
    device::signal(settings.wifiSsid.length() ? 3 : 2);
  }
  if (MDNS.begin(settings.hostname.c_str())) {
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("celldoc", "tcp", 80);  // damit Partnergeräte uns finden
  }

  evlog::routes();
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
  server.on("/api/factory", HTTP_POST, handleFactory);
  server.on("/api/notify/test", HTTP_POST, handleNotifyTest);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.on("/api/ghupdate", HTTP_GET, handleGhStatus);
  server.on("/api/ghupdate", HTTP_POST, handleGhStart);
  server.onNotFound(handleNotFound);
  server.begin();
}

// RGB-LED auf dem S3-Board: grün bereit, türkis lädt, orange entlädt, grün hell fertig,
// rot blinkend Fehler, blau blinkend Hotspot
static void statusLed() {
  if (cfg::PIN_RGB < 0) return;
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < 250) return;
  last = now;
  bool blink = (now / 500) % 2;
  uint8_t r = 0, g = 0, b = 0;
  uint8_t st = device::ledState();
  // ausgeschaltet oder nachts dunkel; Fehler leuchten trotzdem
  if (st != 4 && (!settings.led || (settings.night() && settings.nightLed))) {
#if CONFIG_IDF_TARGET_ESP32S3
    neopixelWrite(cfg::PIN_RGB, 0, 0, 0);
#endif
    return;
  }
  switch (st) {
    case 1: g = 14; b = 14; break;
    case 2: r = 24; g = 9; break;
    case 3: g = 30; break;
    case 4: r = blink ? 40 : 0; break;
    default:
      if (ap) b = blink ? 30 : 0;
      else g = 4;
  }
#if CONFIG_IDF_TARGET_ESP32S3
  neopixelWrite(cfg::PIN_RGB, r, g, b);
#endif
}

void update() {
  statusLed();
  if (ap) dns.processNextRequest();
  server.handleClient();
  improv::update();
  mqtt::update();
  scanner::update();
  peersync::update();
  // WLAN-Abbrüche festhalten (nicht im Hotspot-Modus)
  static bool wasUp = true;
  static uint32_t downSince = 0;
  if (!ap) {
    bool up = WiFi.status() == WL_CONNECTED;
    if (wasUp && !up) {
      downSince = millis();
      evlog::warn(0, "WLAN-Verbindung verloren");
    } else if (!wasUp && up) {
      evlog::info(0, "WLAN wieder verbunden nach " + String((millis() - downSince) / 1000) + " s");
    }
    wasUp = up;
  }
  if (restartPending && millis() > restartAt) {
    device::killAll();
    delay(100);
    ESP.restart();
  }
}

}  // namespace web
