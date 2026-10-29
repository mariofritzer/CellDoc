// CellDoc – WLAN-Einrichtung über den Webflasher (Improv Serial, https://www.improv-wifi.com/serial/)
// Der Webflasher (ESP Web Tools) fragt nach dem Flashen über USB nach dem Gerät und bietet
// „WLAN verbinden“ an. Pakete: "IMPROV" · Version 1 · Typ · Länge · Daten · Prüfsumme
#include "improv.h"
#include <WiFi.h>
#include "config.h"
#include "settings.h"
#include "web.h"
#include "web_device.h"

namespace improv {

enum : uint8_t { T_STATE = 1, T_ERROR = 2, T_RPC = 3, T_RESULT = 4 };
enum : uint8_t { S_READY = 2, S_PROVISIONING = 3, S_PROVISIONED = 4 };
enum : uint8_t { E_NONE = 0, E_INVALID = 1, E_UNKNOWN_RPC = 2, E_CONNECT = 3, E_UNKNOWN = 0xFF };
enum : uint8_t { C_WIFI = 1, C_STATE = 2, C_INFO = 3, C_SCAN = 4 };

static uint8_t buf[256];
static uint16_t len = 0;

static void send(uint8_t type, const uint8_t* data, uint8_t n) {
  uint8_t h[9] = {'I', 'M', 'P', 'R', 'O', 'V', 1, type, n};
  uint8_t sum = 0;
  for (uint8_t c : h) sum += c;
  for (int k = 0; k < n; k++) sum += data[k];
  Serial.write(h, sizeof(h));
  if (n) Serial.write(data, n);
  Serial.write(sum);
  Serial.write('\n');
  Serial.flush();
}

static void sendByte(uint8_t type, uint8_t v) { send(type, &v, 1); }

// RPC-Ergebnis: Befehl · Gesamtlänge · (Länge · Text)…
static void result(uint8_t cmd, std::initializer_list<String> strs) {
  uint8_t d[250];
  uint8_t n = 2;
  for (const String& s : strs) {
    uint8_t l = min<size_t>(s.length(), 60);
    if (size_t(n) + 1 + l > sizeof(d)) break;
    d[n++] = l;
    memcpy(d + n, s.c_str(), l);
    n += l;
  }
  d[0] = cmd;
  d[1] = n - 2;
  send(T_RESULT, d, n);
}

static String url() { return "http://" + WiFi.localIP().toString() + "/"; }

static bool connected() { return !web::apMode() && WiFi.status() == WL_CONNECTED; }

static const char* chip() {
#if CONFIG_IDF_TARGET_ESP32S3
  return "ESP32-S3";
#elif CONFIG_IDF_TARGET_ESP32C3
  return "ESP32-C3";
#else
  return "ESP32";
#endif
}

static void provision(const String& ssid, const String& pass) {
  sendByte(T_STATE, S_PROVISIONING);
  device::killAll();  // während der Umstellung sicher aus
  bool wasAp = web::apMode();
  WiFi.mode(wasAp ? WIFI_AP_STA : WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
#if CONFIG_IDF_TARGET_ESP32C3
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
#endif
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    device::idle();
    delay(50);
  }
  if (WiFi.status() != WL_CONNECTED) {
    sendByte(T_ERROR, E_CONNECT);
    sendByte(T_STATE, S_READY);
    // altes WLAN bzw. Hotspot wiederherstellen
    if (settings.wifiSsid.length() && !wasAp) WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPass.c_str());
    else WiFi.mode(WIFI_AP);
    return;
  }
  settings.wifiSsid = ssid;
  settings.wifiPass = pass;
  settings.save();
  Serial.printf("WLAN über den Webflasher eingerichtet: %s\n", url().c_str());
  sendByte(T_STATE, S_PROVISIONED);
  result(C_WIFI, {url()});
  web::restartSoon(3000);  // mit dem neuen WLAN sauber neu starten (Hotspot aus, Uhrzeit, MQTT)
}

static void handle(const uint8_t* p, uint8_t n) {
  uint8_t type = p[7];
  const uint8_t* d = p + 9;
  if (type != T_RPC || n < 2 || d[1] + 2 > n) {
    sendByte(T_ERROR, E_INVALID);
    return;
  }
  switch (d[0]) {
    case C_WIFI: {
      // Daten: Befehl · Länge · SSID-Länge · SSID · Passwort-Länge · Passwort
      if (n < 4) { sendByte(T_ERROR, E_INVALID); return; }
      uint8_t sl = d[2];
      if (4 + sl > n) { sendByte(T_ERROR, E_INVALID); return; }
      String ssid((const char*)d + 3, sl);
      uint8_t pl = d[3 + sl];
      if (4 + sl + pl > n) { sendByte(T_ERROR, E_INVALID); return; }
      String pass((const char*)d + 4 + sl, pl);
      sendByte(T_ERROR, E_NONE);
      provision(ssid, pass);
      break;
    }
    case C_STATE:
      sendByte(T_ERROR, E_NONE);
      sendByte(T_STATE, connected() ? S_PROVISIONED : S_READY);
      if (connected()) result(C_STATE, {url()});
      break;
    case C_INFO:
      sendByte(T_ERROR, E_NONE);
      result(C_INFO, {"CellDoc", CELLDOC_VERSION, chip(), device::model()});
      break;
    case C_SCAN: {
      sendByte(T_ERROR, E_NONE);
      int k = WiFi.scanNetworks();
      for (int i = 0; i < k && i < 20; i++) {
        if (WiFi.SSID(i).isEmpty()) continue;
        result(C_SCAN, {WiFi.SSID(i), String(WiFi.RSSI(i)), WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "NO" : "YES"});
      }
      WiFi.scanDelete();
      result(C_SCAN, {});
      break;
    }
    default:
      sendByte(T_ERROR, E_UNKNOWN_RPC);
  }
}

void update() {
  while (Serial.available()) {
    uint8_t c = Serial.read();
    // Anfang suchen: "IMPROV"
    static const char hdr[] = "IMPROV";
    if (len < 6) {
      if (c == (uint8_t)hdr[len]) buf[len++] = c;
      else len = c == 'I' ? (buf[0] = c, 1) : 0;
      continue;
    }
    buf[len++] = c;
    if (len == 7 && c != 1) { len = 0; continue; }  // nur Version 1
    if (len >= 9 && len == 9 + buf[8] + 1) {
      uint8_t sum = 0;
      for (int k = 0; k < len - 1; k++) sum += buf[k];
      uint8_t n = buf[8];
      bool ok = sum == buf[len - 1];
      len = 0;
      if (ok) handle(buf, n);
      else sendByte(T_ERROR, E_INVALID);
    }
    if (len >= sizeof(buf)) len = 0;
  }
}

}  // namespace improv
