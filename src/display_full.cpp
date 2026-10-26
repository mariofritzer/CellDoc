// CellDoc – OLED-Anzeige und Taster am großen Prüfstand
// Seiten (◀ / ▶): Live, Temperaturen, Balancer, Netzwerk
// OK: Ergebnis/Fehler bestätigen, auf „Live“ ohne Vorgang: Standardtest an Anschluss A
// OK lang: laufenden Vorgang stoppen
#include "display_full.h"
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include "config.h"
#include "drivers.h"
#include "cells.h"
#include "hal.h"
#include "settings.h"
#include "soh.h"
#include "tester.h"
#include "web.h"
#include "web_device.h"

namespace display {

static U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
static bool present = false;
static uint8_t page = 0;
static constexpr uint8_t PAGES = 4;
static uint32_t lastDraw = 0;

// ---------------------------------------------------------------- Taster
enum class Btn : uint8_t { None, Prev, Next, Ok, OkLong };
static const uint8_t PINS[3] = {cfg::PIN_BTN_PREV, cfg::PIN_BTN_OK, cfg::PIN_BTN_NEXT};
static uint8_t stable = 0, last = 0;
static uint32_t changedAt = 0, okDownAt = 0;
static bool okLongSent = false;

static Btn readButtons(uint32_t now) {
  uint8_t raw = 0;
  for (int k = 0; k < 3; k++)
    if (digitalRead(PINS[k]) == LOW) raw |= 1 << k;
  if (raw != last) {
    last = raw;
    changedAt = now;
  }
  if (now - changedAt < 30) return Btn::None;  // entprellen
  Btn ev = Btn::None;
  uint8_t pressed = raw & ~stable, released = stable & ~raw;
  if (pressed & 1) ev = Btn::Prev;
  if (pressed & 4) ev = Btn::Next;
  if (pressed & 2) {
    okDownAt = now;
    okLongSent = false;
  }
  if ((raw & 2) && !okLongSent && now - okDownAt > 1200) {
    okLongSent = true;
    ev = Btn::OkLong;
  }
  if ((released & 2) && !okLongSent) ev = Btn::Ok;
  stable = raw;
  return ev;
}

// ---------------------------------------------------------------- Anzeige
static const char* phaseText(const char* p) {
  static const char* const map[][2] = {{"idle", "bereit"}, {"precharge", "Laden"}, {"rest", "Ruhen"}, {"discharge", "Entladen"},
                                       {"restafter", "Erholen"}, {"recharge", "Laden"}, {"done", "fertig"}, {"error", "FEHLER"},
                                       {"charge", "Laden"}, {"manualdischarge", "Entladen"}};
  for (auto& m : map)
    if (!strcmp(m[0], p)) return m[1];
  return p;
}

static void header(const char* left, const char* right) {
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawUTF8(0, 9, left);
  if (right && *right) u8g2.drawUTF8(128 - u8g2.getUTF8Width(right), 9, right);
  u8g2.drawHLine(0, 11, 128);
}

static void centered(int y, const char* s) { u8g2.drawUTF8((128 - u8g2.getUTF8Width(s)) / 2, y, s); }

static void drawLive(JsonObject s) {
  char buf[40];
  const char* ph = s["phase"] | "idle";
  JsonObject job = s["job"];
  bool portB = String(job["port"] | "A") == "B" || (!strcmp(ph, "idle") && (s["ports"][0]["v"] | 0.0f) < 0.5f && (s["ports"][1]["v"] | 0.0f) > 0.5f);
  JsonObject p = s["ports"][portB ? 1 : 0];
  String cell = job["cellId"] | "";
  if (!strcmp(ph, "idle") && device::scannedCell()) cell = cells::makeId(device::scannedCell());
  snprintf(buf, sizeof(buf), "%s %s", portB ? "B" : "A", phaseText(ph));
  int cyc = job["cycles"] | 1;
  char right[24] = "";
  if (cyc > 1 && strcmp(ph, "done") && strcmp(ph, "idle")) snprintf(right, sizeof(right), "Zykl. %d/%d", s["cycle"] | 1, cyc);
  else snprintf(right, sizeof(right), "%s", cell.c_str());
  header(buf, right);

  if (!strcmp(ph, "error")) {
    u8g2.setFont(u8g2_font_6x10_tf);
    String e = s["error"] | "";
    u8g2.drawUTF8(0, 26, e.substring(0, 21).c_str());
    u8g2.drawUTF8(0, 38, e.substring(21, 42).c_str());
    u8g2.drawUTF8(0, 60, "OK: bestätigen");
    return;
  }
  if (!strcmp(ph, "done") && s["last"]["score"].is<int>()) {
    // fertig: Zellnummer groß für das Etikett
    int sc = s["last"]["score"];
    u8g2.setFont(u8g2_font_logisoso16_tf);
    String id = s["last"]["cellId"] | "";
    centered(34, id.c_str());
    u8g2.setFont(u8g2_font_6x10_tf);
    snprintf(buf, sizeof(buf), "%.0f mAh  Note %c  %d %%", s["last"]["cap"] | 0.0f, soh::grade(sc), sc);
    centered(48, buf);
    u8g2.drawUTF8(0, 62, "OK: bestätigen");
    return;
  }
  float v = p["v"] | 0.0f, i = p["i"] | 0.0f;
  u8g2.setFont(u8g2_font_logisoso16_tf);
  if (v > 0.3f) snprintf(buf, sizeof(buf), "%.3fV", v);
  else snprintf(buf, sizeof(buf), "--- V");
  u8g2.drawUTF8(0, 32, buf);
  int soc = s["soc"] | -1;
  if (soc >= 0 && v > 0.3f) {
    u8g2.setFont(u8g2_font_6x10_tf);
    snprintf(buf, sizeof(buf), "%d%%", soc);
    u8g2.drawUTF8(128 - u8g2.getUTF8Width(buf), 30, buf);
  }
  u8g2.setFont(u8g2_font_6x10_tf);
  bool active = strcmp(ph, "idle") != 0;
  if (active) snprintf(buf, sizeof(buf), "%+.2fA  %.0f mAh", i, s["mah"] | 0.0f);
  else snprintf(buf, sizeof(buf), "%+.2fA", i);
  u8g2.drawUTF8(0, 46, buf);
  float tc = s["temps"]["cell"] | NAN, tk = s["temps"]["sink"] | NAN;
  if (!active && v > 2.5f) snprintf(buf, sizeof(buf), "OK: Test an A  %s", isnan(tc) ? "" : (String(tc, 0) + "°C").c_str());
  else snprintf(buf, sizeof(buf), "Zelle %s  Kühl %s", isnan(tc) ? "--" : String(tc, 1).c_str(), isnan(tk) ? "--" : String(tk, 0).c_str());
  u8g2.drawUTF8(0, 60, buf);
}

static void drawTemps(JsonObject s) {
  char buf[32];
  header("Temperaturen", "2/4");
  const char* names[] = {"cell", "pack", "sink"};
  const char* labels[] = {"Zelle (A)", "Pack (B)", "Kühlkörper"};
  for (int k = 0; k < 3; k++) {
    float t = s["temps"][names[k]] | NAN;
    snprintf(buf, sizeof(buf), "%-13s %s", labels[k], isnan(t) ? "  --" : (String(t, 1) + " °C").c_str());
    u8g2.drawUTF8(0, 24 + k * 12, buf);
  }
  snprintf(buf, sizeof(buf), "Lüfter %d%%  %d U/min", s["fan"]["duty"] | 0, s["fan"]["rpm"] | 0);
  u8g2.drawUTF8(0, 62, buf);
}

static void drawBal(JsonObject s) {
  char buf[32];
  header("Balancer (B)", "3/4");
  JsonArray c = s["bal"]["cells"];
  if (c.size() < 2) {
    u8g2.drawUTF8(0, 34, "Kein Pack erkannt");
    return;
  }
  float mn = 9, mx = 0;
  for (size_t k = 0; k < c.size() && k < 6; k++) {
    float v = c[k];
    mn = min(mn, v);
    mx = max(mx, v);
    snprintf(buf, sizeof(buf), "%d: %.3f", int(k + 1), v);
    u8g2.drawUTF8((k % 2) * 64, 24 + (k / 2) * 12, buf);
  }
  snprintf(buf, sizeof(buf), "Differenz %.0f mV", (mx - mn) * 1000);
  u8g2.drawUTF8(0, 62, buf);
}

static void drawNet() {
  header("Netzwerk", "4/4");
  bool ap = web::apMode();
  String ip = ap ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  u8g2.drawUTF8(0, 24, ap ? "Hotspot" : "WLAN");
  u8g2.drawUTF8(0, 36, (ap ? WiFi.softAPSSID() : WiFi.SSID()).substring(0, 21).c_str());
  u8g2.setFont(u8g2_font_7x13B_tf);
  u8g2.drawUTF8(0, 51, ip.c_str());
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawUTF8(0, 63, ap ? "Passwort celldoc1" : (settings.hostname + ".local").c_str());
}

// ---------------------------------------------------------------- Bedienung
static void quickStart() {
  JobConfig c;
  c.port = PORT_A;
  c.chem = findChemistry("liion");
  c.cells = 1;
  c.cellNo = device::scannedCell();  // gescannte Zelle, sonst neue
  c.currentA = 0.5f;
  c.preCharge = true;
  c.restMin = settings.restMin;
  c.after = After::Storage;
  String e = tester.startTest(c);
  if (e.length()) hal.alarm();
  else hal.beep(80);
}

bool available() { return present; }

void begin() {
  for (uint8_t p : PINS) pinMode(p, INPUT_PULLUP);
  present = i2c::present(cfg::ADDR_OLED);
  if (!present) return;
  u8g2.setBusClock(400000);
  u8g2.begin();
  u8g2.enableUTF8Print();
  u8g2.setContrast(180);
}

void update(uint32_t now) {
  Btn b = readButtons(now);
  Phase ph = tester.phase();
  switch (b) {
    case Btn::Prev: page = (page + PAGES - 1) % PAGES; break;
    case Btn::Next: page = (page + 1) % PAGES; break;
    case Btn::Ok:
      if (ph == Phase::Done || ph == Phase::Error) tester.acknowledge();
      else if (ph == Phase::Idle && page == 0) quickStart();
      else page = 0;
      break;
    case Btn::OkLong:
      if (tester.active()) {
        tester.stop("Am Gerät gestoppt");
        hal.beep(300);
      }
      break;
    default: break;
  }
  if (b != Btn::None) lastDraw = 0;
  if (!present || now - lastDraw < 300) return;
  lastDraw = now;
  JsonDocument d;
  JsonObject s = d.to<JsonObject>();
  device::fillState(s);
  // bei Ergebnis oder Fehler immer die Live-Seite zeigen
  if (ph == Phase::Done || ph == Phase::Error) page = 0;
  u8g2.clearBuffer();
  switch (page) {
    case 1: drawTemps(s); break;
    case 2: drawBal(s); break;
    case 3: drawNet(); break;
    default: drawLive(s);
  }
  u8g2.sendBuffer();
}

}  // namespace display
