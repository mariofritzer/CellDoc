#include "sync.h"
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <vector>
#include "cells.h"
#include "settings.h"
#include "web.h"
#include "web_device.h"

namespace peersync {

struct Peer {
  String host;           // wie eingetragen
  IPAddress ip;          // aufgelöst (bei .local über mDNS)
  uint32_t ipAt = 0;
  bool ok = false;
  uint32_t lastOk = 0;   // millis
  uint32_t due = 0;      // nächster Abgleich (millis)
  uint32_t since = 0;    // höchste Empfangszeit, die wir von diesem Partner kennen
  uint32_t next = 0;     // seine nächste freie Nummer
  int merged = 0;
  String err;
};

static std::vector<Peer> peers;
static String peersCfg = "\x01";  // erzwingt Einlesen beim ersten Aufruf
static bool dirty = false;
static uint32_t dirtyAt = 0;
static size_t rr = 0;
static constexpr uint32_t PERIOD_MS = 120000;

static uint32_t nowEpoch() {
  time_t t = time(nullptr);
  return t > 1700000000 ? uint32_t(t) : 0;
}

static bool online() { return !web::apMode() && WiFi.status() == WL_CONNECTED; }

static void loadPeers() {
  if (peersCfg == settings.peers) return;
  peersCfg = settings.peers;
  std::vector<Peer> np;
  int start = 0;
  String s = settings.peers + ",";
  for (int i = 0; i < (int)s.length(); i++) {
    if (s[i] != ',') continue;
    String h = s.substring(start, i);
    h.trim();
    start = i + 1;
    if (!h.length() || h.equalsIgnoreCase(settings.hostname) || h.equalsIgnoreCase(settings.hostname + ".local")) continue;
    Peer p;
    p.host = h;
    for (auto& o : peers)
      if (o.host == h) p = o;  // Zustand behalten
    np.push_back(p);
    if (np.size() >= 4) break;
  }
  peers = np;
}

// Adresse für HTTP: IP direkt, „name“ oder „name.local“ über mDNS
static String addr(Peer& p) {
  String h = p.host;
  int colon = h.indexOf(':');
  String port = colon > 0 ? h.substring(colon) : "";
  if (colon > 0) h = h.substring(0, colon);
  IPAddress ip;
  if (ip.fromString(h)) return h + port;
  if (h.endsWith(".local")) h = h.substring(0, h.length() - 6);
  if (h.indexOf('.') >= 0) return h + port;  // normaler DNS-Name
  if (!p.ipAt || millis() - p.ipAt > 600000UL || p.ip == IPAddress()) {
    p.ip = MDNS.queryHost(h, 1000);
    p.ipAt = millis();
  }
  return p.ip == IPAddress() ? String("") : p.ip.toString() + port;
}

static bool request(Peer& p, const char* method, const String& path, const String& body, String* out, int timeout = 1500) {
  String a = addr(p);
  if (!a.length()) {
    p.ok = false;
    p.err = "nicht gefunden";
    return false;
  }
  WiFiClient cl;
  HTTPClient http;
  http.setConnectTimeout(800);
  http.setTimeout(timeout);
  if (!http.begin(cl, "http://" + a + path)) return false;
  int code = strcmp(method, "POST") == 0 ? (http.addHeader("Content-Type", "application/json"), http.POST(body)) : http.GET();
  bool ok = code == 200;
  if (ok && out) *out = http.getString();
  if (!ok) {
    p.ok = false;
    p.err = code < 0 ? "nicht erreichbar" : "Fehler " + String(code);
  }
  http.end();
  return ok;
}

bool merge(JsonDocument& d) {
  uint32_t no = d["no"] | 0u;
  if (!no) return false;
  JsonDocument l;
  bool has = cells::load(no, l);
  if (has) {
    String iuid = d["uid"] | "", luid = l["uid"] | "";
    if (iuid.length() && luid.length() && iuid != luid) {
      // dieselbe Nummer wurde auf zwei Geräten ohne Verbindung vergeben
      if (!l["conflict"].is<const char*>()) {
        l["conflict"] = String(d["dev"] | "?") + ": " + String(d["name"] | "");
        cells::saveRaw(no, l);
      }
      return false;
    }
    uint32_t iu = d["upd"] | 0u, lu = l["upd"] | 0u;
    if (iu < lu || (iu == lu && String(d["dev"] | "") <= String(l["dev"] | ""))) return false;
  }
  d["rcv"] = nowEpoch();
  cells::saveRaw(no, d);
  if (no >= settings.nextCellNo) {
    settings.nextCellNo = no + 1;
    settings.save();
  }
  return true;
}

// Geänderte Zellen eines Partners abholen, Element für Element (wenig RAM)
static void pull(Peer& p) {
  String a = addr(p);
  if (!a.length()) {
    p.ok = false;
    p.err = "nicht gefunden";
    return;
  }
  WiFiClient cl;
  HTTPClient http;
  http.useHTTP10(true);  // ohne Chunked-Encoding, damit der Datenstrom direkt lesbar ist
  http.setConnectTimeout(800);
  http.setTimeout(3000);
  uint32_t since = p.since > 2 ? p.since - 2 : 0;
  if (!http.begin(cl, "http://" + a + "/api/cells?since=" + String(since))) return;
  int code = http.GET();
  if (code != 200) {
    p.ok = false;
    p.err = code < 0 ? "nicht erreichbar" : "Fehler " + String(code);
    http.end();
    return;
  }
  Stream& st = http.getStream();
  int n = 0;
  if (st.find("[")) {
    for (;;) {
      JsonDocument d;
      if (deserializeJson(d, st)) break;
      uint32_t r = d["rcv"] | 0u;
      if (r > p.since) p.since = r;
      if (merge(d)) n++;
      if (!st.findUntil(",", "]")) break;
    }
  }
  http.end();
  p.ok = true;
  p.err = "";
  p.lastOk = millis();
  p.merged += n;
}

void changed() {
  dirty = true;
  dirtyAt = millis();
}

void poked() {
  for (auto& p : peers) p.due = millis() + 1500;
}

void syncNow() {
  for (auto& p : peers) {
    p.due = 0;
    p.since = 0;
  }
  dirty = true;
  dirtyAt = 0;
}

uint32_t peerNextNo() {
  loadPeers();
  uint32_t m = 0;
  if (!online()) return 0;
  for (auto& p : peers) {
    String r;
    if (!request(p, "GET", "/api/sync/next", "", &r, 1000)) continue;
    JsonDocument d;
    if (!deserializeJson(d, r)) {
      p.next = d["next"] | 0u;
      m = max(m, p.next);
    }
  }
  return m;
}

void reserve(uint32_t no) {
  if (!online()) return;
  for (auto& p : peers) request(p, "POST", "/api/sync/next", "{\"used\":" + String(no) + "}", nullptr, 1000);
}

void update() {
  loadPeers();
  if (peers.empty() || !online() || !nowEpoch()) return;
  uint32_t now = millis();
  // eigene Änderungen: Partner anstoßen, damit sie bald abholen
  if (dirty && now - dirtyAt > 2000) {
    dirty = false;
    for (auto& p : peers) request(p, "POST", "/api/sync/poke", "{}", nullptr, 1000);
  }
  // reihum einen Partner abgleichen; während eines Tests nur kleine Abgleiche (nicht den ersten vollständigen)
  Peer& p = peers[rr % peers.size()];
  rr++;
  if ((int32_t)(now - p.due) < 0) return;
  if (device::busy() && p.since == 0 && p.lastOk) return;
  pull(p);
  p.due = millis() + (p.ok ? PERIOD_MS : 30000);
}

void status(JsonObject o) {
  loadPeers();
  o["host"] = settings.hostname;
  o["time"] = nowEpoch() > 0;
  JsonArray a = o["peers"].to<JsonArray>();
  for (auto& p : peers) {
    JsonObject e = a.add<JsonObject>();
    e["host"] = p.host;
    e["ok"] = p.ok;
    e["ago"] = p.lastOk ? (millis() - p.lastOk) / 1000 : -1;
    e["err"] = p.err;
    e["merged"] = p.merged;
  }
}

void discover(JsonArray out) {
  int n = MDNS.queryService("celldoc", "tcp");
  for (int i = 0; i < n; i++) {
    String h = MDNS.hostname(i);
    if (h.equalsIgnoreCase(settings.hostname)) continue;
    JsonObject o = out.add<JsonObject>();
    o["host"] = h;
    o["ip"] = MDNS.IP(i).toString();
  }
}

}  // namespace peersync
