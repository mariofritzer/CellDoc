// CellDoc – Zellenbestand, Selbstentladung, Uhrzeit, Datensicherung (gemeinsam für CellDoc und Mini)
#include <LittleFS.h>
#include <WebServer.h>
#include <sys/time.h>
#include <algorithm>
#include <vector>
#include "cells.h"
#include "config.h"
#include "mqtt.h"
#include "settings.h"
#include "sync.h"
#include "web.h"
#include "web_device.h"

using web::readBody;
using web::sendError;
using web::sendJson;
using web::sendOk;
#define server web::http()

namespace {

// ---------------------------------------------------------------------------
// Dateien ohne Umweg über den RAM ausliefern

std::vector<uint32_t> idsIn(const char* dir) {
  std::vector<uint32_t> ids;
  File d = LittleFS.open(dir);
  if (!d) return ids;
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    uint32_t id = String(f.name()).toInt();
    if (id) ids.push_back(id);
  }
  return ids;
}

void beginChunked(const char* type) {
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, type, "");
}

void sendRaw(File& f) {
  static char buf[768];
  size_t n;
  while ((n = f.read((uint8_t*)buf, sizeof(buf))) > 0) server.sendContent(buf, n);
}

void streamArray(const char* dir, bool newestFirst) {
  std::vector<uint32_t> ids = idsIn(dir);
  std::sort(ids.begin(), ids.end());
  if (newestFirst) std::reverse(ids.begin(), ids.end());
  beginChunked("application/json");
  server.sendContent("[");
  bool first = true;
  for (uint32_t id : ids) {
    File f = LittleFS.open(String(dir) + "/" + id + ".json", "r");
    if (!f || !f.size()) continue;
    if (!first) server.sendContent(",");
    first = false;
    sendRaw(f);
    f.close();
  }
  server.sendContent("]");
  server.sendContent("");
}

uint32_t cellArg(JsonDocument* b) {
  if (b) {
    if ((*b)["no"].is<uint32_t>()) return (*b)["no"];
    if ((*b)["no"].is<const char*>()) return cells::parseRef((*b)["no"].as<const char*>());
  }
  if (server.hasArg("no")) return cells::parseRef(server.arg("no"));
  return 0;
}

// ---------------------------------------------------------------------------
// Zellenbestand

// Ohne ?since: alle Zellen ohne gelöschte (für die Oberfläche).
// Mit ?since=t: alle seit t hier eingegangenen Änderungen inklusive Grabsteinen (für Partnergeräte).
void handleCells() {
  bool forSync = server.hasArg("since");
  uint32_t since = server.arg("since").toInt();
  std::vector<uint32_t> ids = idsIn("/z");
  std::sort(ids.begin(), ids.end());
  beginChunked("application/json");
  server.sendContent("[");
  bool first = true;
  for (uint32_t id : ids) {
    File f = LittleFS.open("/z/" + String(id) + ".json", "r");
    if (!f || !f.size()) continue;
    if (forSync) {
      JsonDocument flt, d;
      flt["rcv"] = true;
      deserializeJson(d, f, DeserializationOption::Filter(flt));
      if (since && (d["rcv"] | 0u) <= since) {
        f.close();
        continue;
      }
      f.seek(0);
      if (!first) server.sendContent(",");
      first = false;
      sendRaw(f);
    } else {
      String c = f.readString();
      if (c.indexOf("\"deleted\":true") < 0) {
        if (!first) server.sendContent(",");
        first = false;
        server.sendContent(c);
      }
    }
    f.close();
  }
  server.sendContent("]");
  server.sendContent("");
}

void handleCell() {
  uint32_t no = cellArg(nullptr);
  File f = LittleFS.open("/z/" + String(no) + ".json", "r");
  if (!no || !f) return sendError("Zelle nicht gefunden", 404);
  String c = f.readString();
  f.close();
  if (c.indexOf("\"deleted\":true") >= 0) return sendError("Zelle wurde gelöscht", 404);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", c);
}

void handleCellCheck() {
  JsonDocument b;
  if (!readBody(b)) return sendError("Ungültige Anfrage");
  uint32_t no = cellArg(&b);
  if (!cells::exists(no)) return sendError("Zelle nicht gefunden", 404);
  float v = 0;
  String e = device::cellVoltage(b, v);
  JsonDocument r;
  if (!e.length()) e = cells::check(no, v, r.to<JsonObject>());
  if (e.length()) return sendError(e, 409);
  r["ok"] = true;
  sendJson(r);
}

// --- Abgleich mit Partnergeräten
void handleSyncNextGet() {
  JsonDocument r;
  r["next"] = settings.nextCellNo;
  r["host"] = settings.hostname;
  sendJson(r);
}

void handleSyncNextPost() {
  JsonDocument b;
  if (!readBody(b)) return sendError("Ungültige Anfrage");
  uint32_t used = b["used"] | 0u;
  if (used >= settings.nextCellNo) {
    settings.nextCellNo = used + 1;
    settings.save();
  }
  sendOk();
}

void handleSyncStatus() {
  JsonDocument r;
  peersync::status(r.to<JsonObject>());
  sendJson(r);
}

void handleSyncDiscover() {
  JsonDocument r;
  peersync::discover(r.to<JsonArray>());
  sendJson(r);
}

const char* STATUS[] = {"ok", "verbaut", "defekt", "entsorgt"};
bool validStatus(const String& s) {
  for (const char* x : STATUS)
    if (s == x) return true;
  return false;
}

String clip(const char* s, size_t n) {
  String r = s ? s : "";
  r.trim();
  if (r.length() > n) r = r.substring(0, n);
  return r;
}

void applyFields(JsonDocument& d, JsonDocument& b) {
  if (b["name"].is<const char*>()) {
    String n = clip(b["name"].as<const char*>(), 40);
    d["name"] = n.length() ? n : String(d["id"] | "");
  }
  if (b["loc"].is<const char*>()) d["loc"] = clip(b["loc"].as<const char*>(), 40);
  if (b["note"].is<const char*>()) d["note"] = clip(b["note"].as<const char*>(), 200);
  if (b["pack"].is<const char*>()) d["pack"] = clip(b["pack"].as<const char*>(), 40);
  if (b["status"].is<const char*>() && validStatus(b["status"].as<const char*>())) d["status"] = b["status"];
}

void handleCellSave() {
  JsonDocument b;
  if (!readBody(b)) return sendError("Ungültige Anfrage");
  uint32_t no = cellArg(&b);
  if (!no) {
    no = cells::create(b.as<JsonObjectConst>());
    JsonDocument d;
    cells::load(no, d);
    applyFields(d, b);
    cells::save(no, d);
  } else {
    JsonDocument d;
    if (!cells::exists(no) || !cells::load(no, d)) return sendError("Zelle nicht gefunden", 404);
    applyFields(d, b);
    cells::save(no, d);
  }
  JsonDocument r;
  r["ok"] = true;
  r["no"] = no;
  r["id"] = cells::makeId(no);
  sendJson(r);
}

void handleCellsBatch() {
  JsonDocument b;
  if (!readBody(b)) return sendError("Ungültige Anfrage");
  int n = 0;
  for (JsonVariant v : b["nos"].as<JsonArray>()) {
    uint32_t no = v.as<uint32_t>();
    JsonDocument d;
    if (!cells::exists(no) || !cells::load(no, d)) continue;
    applyFields(d, b);
    cells::save(no, d);
    n++;
  }
  JsonDocument r;
  r["ok"] = true;
  r["changed"] = n;
  sendJson(r);
}

void handleCellDelete() {
  JsonDocument b;
  readBody(b);
  uint32_t no = cellArg(&b);
  if (device::cellInUse(no)) return sendError("Die Zelle wird gerade geprüft", 409);
  if (!cells::remove(no)) return sendError("Zelle nicht gefunden", 404);
  sendOk();
}

void handleSelfDischarge() {
  JsonDocument b;
  if (!readBody(b)) return sendError("Ungültige Anfrage");
  uint32_t no = cellArg(&b);
  if (!cells::exists(no)) return sendError("Zelle nicht gefunden", 404);
  String action = b["action"] | "";
  String e;
  if (action == "reset") {
    e = cells::sdReset(no);
  } else {
    float v = 0;
    e = device::cellVoltage(b, v);
    if (!e.length()) e = action == "start" ? cells::sdStart(no, v) : cells::sdMeasure(no, v);
  }
  if (e.length()) return sendError(e, 409);
  sendOk();
}

// Ohne Internet kennt CellDoc die Uhrzeit nicht: dann übernimmt es die Zeit vom Browser
void handleTime() {
  JsonDocument b;
  if (!readBody(b)) return sendError("Ungültige Anfrage");
  uint32_t t = b["t"] | 0u;
  time_t now = time(nullptr);
  if (t > 1700000000 && now < 1700000000) {
    timeval tv{time_t(t), 0};
    settimeofday(&tv, nullptr);
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
  }
  sendOk();
}

// ---------------------------------------------------------------------------
// Datensicherung: Textdatei, eine Zeile je Datei: „Pfad<TAB>Inhalt“

const char* BACKUP_MAGIC = "CELLDOC-BACKUP 1";

void sendDir(const char* dir) {
  std::vector<uint32_t> ids = idsIn(dir);
  std::sort(ids.begin(), ids.end());
  for (uint32_t id : ids) {
    String p = String(dir) + "/" + id + ".json";
    File f = LittleFS.open(p, "r");
    if (!f || !f.size()) continue;
    server.sendContent(p + "\t");
    sendRaw(f);
    f.close();
    server.sendContent("\n");
  }
}

void handleBackup() {
  bool curves = server.arg("curves") == "1";
  time_t now = time(nullptr);
  char date[16] = "";
  if (now > 1700000000) strftime(date, sizeof(date), "-%Y-%m-%d", localtime(&now));
  server.sendHeader("Content-Disposition", String("attachment; filename=\"celldoc-sicherung") + date + ".txt\"");
  beginChunked("text/plain; charset=utf-8");
  server.sendContent(String(BACKUP_MAGIC) + " " + CELLDOC_VERSION + "\n");
  JsonDocument s;
  settings.toJson(s.to<JsonObject>(), false);
  String line = "/settings.json\t";
  serializeJson(s, line);
  server.sendContent(line + "\n");
  sendDir("/z");
  sendDir("/r");
  if (curves) sendDir("/c");
  server.sendContent("");
}

// Wiederherstellung zeilenweise beim Hochladen, ohne die ganze Datei im RAM
struct Restore {
  enum { HEAD, PATH, DATA, SKIP } st = HEAD;
  String head, pathBuf, settingsBuf;
  File f;
  bool isSettings = false;
  int files = 0;
  String err;
} rs;

bool validPath(const String& p) {
  if (p == "/settings.json") return true;
  if (!(p.startsWith("/z/") || p.startsWith("/r/") || p.startsWith("/c/")) || !p.endsWith(".json")) return false;
  String n = p.substring(3, p.length() - 5);
  if (!n.length() || n.length() > 8) return false;
  for (char c : n)
    if (!isdigit((unsigned char)c)) return false;
  return true;
}

void endLine() {
  if (rs.st == Restore::DATA) {
    if (rs.isSettings) {
      JsonDocument d;
      if (!deserializeJson(d, rs.settingsBuf)) {
        d.remove("wifiSsid");
        d.remove("wifiPass");
        settings.fromJson(d.as<JsonObjectConst>());
        settings.save();
      }
      rs.settingsBuf = "";
    } else if (rs.f) {
      rs.f.close();
    }
    rs.files++;
  }
  rs.st = Restore::PATH;
  rs.pathBuf = "";
  rs.isSettings = false;
}

void restoreChunk(const uint8_t* buf, size_t n) {
  size_t i = 0;
  while (i < n && !rs.err.length()) {
    char c = buf[i];
    switch (rs.st) {
      case Restore::HEAD:
        if (c == '\n') {
          if (!rs.head.startsWith(BACKUP_MAGIC)) rs.err = "Das ist keine CellDoc-Sicherung";
          rs.st = Restore::PATH;
        } else if (rs.head.length() < 64) {
          rs.head += c;
        }
        i++;
        break;
      case Restore::PATH:
        if (c == '\t') {
          if (validPath(rs.pathBuf)) {
            rs.isSettings = rs.pathBuf == "/settings.json";
            if (!rs.isSettings) rs.f = LittleFS.open(rs.pathBuf, "w");
            rs.st = Restore::DATA;
          } else {
            rs.st = Restore::SKIP;
          }
        } else if (c == '\n') {
          rs.pathBuf = "";
        } else if (c != '\r' && rs.pathBuf.length() < 32) {
          rs.pathBuf += c;
        }
        i++;
        break;
      case Restore::DATA: {
        // bis zum Zeilenende am Stück schreiben
        size_t j = i;
        while (j < n && buf[j] != '\n') j++;
        if (rs.isSettings) {
          for (size_t k = i; k < j && rs.settingsBuf.length() < 4096; k++) rs.settingsBuf += char(buf[k]);
        } else if (rs.f) {
          rs.f.write(buf + i, j - i);
        }
        i = j;
        if (j < n) {
          endLine();
          i++;
        }
        break;
      }
      case Restore::SKIP:
        if (c == '\n') endLine();
        i++;
        break;
    }
  }
}

void handleRestoreUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    rs = Restore();
    if (device::busy()) rs.err = "Während eines Vorgangs ist keine Wiederherstellung möglich";
    if (!LittleFS.exists("/z")) LittleFS.mkdir("/z");
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!rs.err.length()) restoreChunk(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
    if (rs.st == Restore::DATA) endLine();
    if (rs.f) rs.f.close();
    if (up.status == UPLOAD_FILE_ABORTED) rs.err = "Hochladen abgebrochen";
  }
}

void handleRestoreDone() {
  if (rs.err.length()) return sendError(rs.err, 400);
  cells::rescanCounters();
  mqtt::reconfigure();
  JsonDocument r;
  r["ok"] = true;
  r["files"] = rs.files;
  sendJson(r);
}

}  // namespace

namespace webdata {

void routes() {
  server.on("/api/results", HTTP_GET, [] { streamArray("/r", true); });
  server.on("/api/cells", HTTP_GET, handleCells);
  server.on("/api/cell", HTTP_GET, handleCell);
  server.on("/api/cell", HTTP_POST, handleCellSave);
  server.on("/api/cells/batch", HTTP_POST, handleCellsBatch);
  server.on("/api/cell/delete", HTTP_POST, handleCellDelete);
  server.on("/api/cell/sd", HTTP_POST, handleSelfDischarge);
  server.on("/api/cell/check", HTTP_POST, handleCellCheck);
  server.on("/api/sync/next", HTTP_GET, handleSyncNextGet);
  server.on("/api/sync/next", HTTP_POST, handleSyncNextPost);
  server.on("/api/sync/poke", HTTP_POST, [] {
    peersync::poked();
    sendOk();
  });
  server.on("/api/sync/now", HTTP_POST, [] {
    peersync::syncNow();
    sendOk();
  });
  server.on("/api/sync/status", HTTP_GET, handleSyncStatus);
  server.on("/api/sync/discover", HTTP_GET, handleSyncDiscover);
  server.on("/api/time", HTTP_POST, handleTime);
  server.on("/api/backup", HTTP_GET, handleBackup);
  server.on("/api/restore", HTTP_POST, handleRestoreDone, handleRestoreUpload);
}

}  // namespace webdata
