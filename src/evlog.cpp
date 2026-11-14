#include "evlog.h"

#include <LittleFS.h>
#include <WebServer.h>
#include <esp_system.h>
#include <time.h>

#include "web.h"

namespace evlog {

static const char* CUR = "/log.txt";
static const char* OLD = "/log.old";
static const size_t MAX_SIZE = 12 * 1024;  // je Datei, zusammen also höchstens ~24 KB
static bool ready = false;

String num(float v, int dec) {
  String s = String(v, dec);
  s.replace(".", ",");
  return s;
}

static const char* resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "eingeschaltet";
    case ESP_RST_EXT: return "Reset-Taste";
    case ESP_RST_SW: return "Neustart durch die Software (Update, Einstellungen)";
    case ESP_RST_PANIC: return "Absturz der Firmware";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "Watchdog (Firmware hing)";
    case ESP_RST_DEEPSLEEP: return "Aufwachen aus dem Tiefschlaf";
    case ESP_RST_BROWNOUT: return "Spannungseinbruch der Versorgung (Brownout)";
    case ESP_RST_SDIO: return "SDIO";
#if ESP_IDF_VERSION_MAJOR >= 4 && defined(ESP_RST_USB)
    case ESP_RST_USB: return "USB";
#endif
    default: return "unbekannt";
  }
}

void begin() {
  ready = true;
  add(esp_reset_reason() == ESP_RST_POWERON || esp_reset_reason() == ESP_RST_SW ? 'i' : 'w', 0,
      String("Gestartet, Firmware ") + CELLDOC_VERSION + ". Grund: " + resetReason());
}

void add(char level, int slot, const String& text) {
  String t = text;
  t.replace("\n", " ");
  t.replace("\t", " ");
  Serial.printf("[log] %c S%d %s\n", level, slot, t.c_str());
  if (!ready) return;
  time_t now = time(nullptr);
  String line = (now > 1700000000 ? String(uint32_t(now)) : "u" + String(millis() / 1000)) + "\t" + String(slot) +
                "\t" + level + "\t" + t + "\n";
  File f = LittleFS.open(CUR, "a");
  if (!f) return;
  size_t size = f.size() + line.length();
  f.print(line);
  f.close();
  if (size > MAX_SIZE) {
    LittleFS.remove(OLD);
    LittleFS.rename(CUR, OLD);
  }
}

void clear() {
  LittleFS.remove(CUR);
  LittleFS.remove(OLD);
  add('i', 0, "Protokoll gelöscht");
}

static void sendFile(const char* path) {
  WebServer& server = web::http();
  File f = LittleFS.open(path, "r");
  if (!f) return;
  char buf[512];
  while (f.available()) {
    size_t n = f.readBytes(buf, sizeof(buf));
    if (!n) break;
    server.sendContent(buf, n);
  }
  f.close();
}

static void handleGet() {
  WebServer& server = web::http();
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/plain; charset=utf-8", "");
  sendFile(OLD);
  sendFile(CUR);
  server.sendContent("");
}

static void handleClear() {
  clear();
  web::http().send(200, "application/json", "{\"ok\":true}");
}

void routes() {
  web::http().on("/api/log", HTTP_GET, handleGet);
  web::http().on("/api/log/clear", HTTP_POST, handleClear);
}

}  // namespace evlog
