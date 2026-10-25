// CellDoc Mini – optionales OLED (SH1106 128×64) mit drei Tastern
#include "display.h"
#include <U8g2lib.h>
#include <WiFi.h>
#include "drivers.h"
#include "settings.h"
#include "slot.h"

using namespace mcfg;

namespace display {

static U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
static bool present = false;
static uint8_t sel = 0;
static bool detail = false;
static uint32_t lastDraw = 0, lastInput = 0;

bool available() { return present; }

void begin() {
  present = i2c::present(ADDR_OLED);
  if (!present) return;
  u8g2.setBusClock(400000);
  u8g2.begin();
  u8g2.enableUTF8Print();
  u8g2.setContrast(180);
}

static const char* shortPhase(Phase p) {
  switch (p) {
    case Phase::Idle: return "bereit";
    case Phase::PreCharge: return "Laden";
    case Phase::Rest: return "Ruhe";
    case Phase::Discharge: return "Entl.";
    case Phase::RestAfter: return "Erhol.";
    case Phase::Recharge: return "Laden";
    case Phase::Done: return "fertig";
    case Phase::Error: return "FEHLER";
    case Phase::ManualCharge: return "Laden";
    case Phase::ManualDischarge: return "Entl.";
  }
  return "";
}

// Standardtest über die Taster: unbekannte 18650, halber Maximalstrom, danach Lagerspannung
static void quickStart(uint8_t s) {
  SlotJob j;
  j.currentA = 0.5f;
  j.preCharge = true;
  j.restMin = settings.restMin;
  j.after = After::Storage;
  String e = slots[s].startTest(j);
  if (e.length()) hal.alarm(); else hal.beep(80);
}

static void drawOverview() {
  u8g2.setFont(u8g2_font_6x10_tf);
  char buf[32];
  // Kopfzeile: Name und IP
  u8g2.drawUTF8(0, 9, "CellDoc Mini");
  String ip = WiFi.getMode() == WIFI_AP ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  u8g2.drawUTF8(128 - u8g2.getUTF8Width(ip.c_str()), 9, ip.c_str());
  u8g2.drawHLine(0, 11, 128);
  int rowH = NSLOTS > 2 ? 13 : 24;
  for (int s = 0; s < NSLOTS; s++) {
    int y = 13 + s * rowH;
    const SlotMeas& m = hal.m(s);
    Slot& sl = slots[s];
    bool on = s == sel;
    if (on) {
      u8g2.setDrawColor(1);
      u8g2.drawBox(0, y, 128, rowH - 1);
      u8g2.setDrawColor(0);
    }
    float soc = sl.socPercent();
    if (m.v > 0.5f)
      snprintf(buf, sizeof(buf), "%d %.2fV %3s%% %s", s + 1, m.v, soc >= 0 ? String(int(soc)).c_str() : "--", shortPhase(sl.phase()));
    else
      snprintf(buf, sizeof(buf), "%d  leer", s + 1);
    u8g2.drawUTF8(2, y + 10, buf);
    if (NSLOTS <= 2) {
      // zweite Zeile: mAh oder Lebenszahl
      if (sl.phase() == Phase::Done && sl.lastScore() >= 0)
        snprintf(buf, sizeof(buf), "  Lebenszahl %d %%", sl.lastScore());
      else if (sl.active())
        snprintf(buf, sizeof(buf), "  %.0f mAh  %.2f A", sl.mah(), m.i);
      else
        buf[0] = 0;
      u8g2.drawUTF8(2, y + 21, buf);
    }
    u8g2.setDrawColor(1);
  }
}

static void drawDetail() {
  u8g2.setFont(u8g2_font_6x10_tf);
  char buf[32];
  Slot& sl = slots[sel];
  const SlotMeas& m = hal.m(sel);
  snprintf(buf, sizeof(buf), "Schacht %d · %s", sel + 1, shortPhase(sl.phase()));
  u8g2.drawUTF8(0, 9, buf);
  u8g2.drawHLine(0, 11, 128);
  u8g2.setFont(u8g2_font_logisoso16_tf);
  snprintf(buf, sizeof(buf), "%.3fV", m.v);
  u8g2.drawUTF8(0, 32, buf);
  u8g2.setFont(u8g2_font_6x10_tf);
  snprintf(buf, sizeof(buf), "%.3f A  %.1f°C", m.i, isnan(m.tCell) ? 0.0f : m.tCell);
  u8g2.drawUTF8(0, 44, buf);
  if (sl.phase() == Phase::Error) {
    u8g2.drawUTF8(0, 56, sl.error().substring(0, 21).c_str());
  } else if (sl.phase() == Phase::Done && sl.lastScore() >= 0) {
    snprintf(buf, sizeof(buf), "Lebenszahl %d %%", sl.lastScore());
    u8g2.drawUTF8(0, 56, buf);
  } else if (sl.active()) {
    snprintf(buf, sizeof(buf), "%.0f mAh", sl.mah());
    u8g2.drawUTF8(0, 56, buf);
  } else {
    u8g2.drawUTF8(0, 56, "OK: Test starten");
  }
}

void update(uint32_t now) {
  BtnEvent e = hal.button();
  if (e != BtnEvent::None) lastInput = now;
  switch (e) {
    case BtnEvent::Prev: sel = (sel + NSLOTS - 1) % NSLOTS; break;
    case BtnEvent::Next: sel = (sel + 1) % NSLOTS; break;
    case BtnEvent::Ok: {
      Slot& sl = slots[sel];
      if (sl.phase() == Phase::Done || sl.phase() == Phase::Error) sl.acknowledge();
      else if (sl.phase() == Phase::Idle && detail) quickStart(sel);
      else detail = !detail;
      break;
    }
    case BtnEvent::OkLong:
      if (slots[sel].active()) {
        slots[sel].stop("Am Gerät gestoppt");
        hal.beep(300);
      }
      break;
    default: break;
  }
  if (!present || now - lastDraw < 250) return;
  lastDraw = now;
  // nach 2 Minuten ohne Taste zurück zur Übersicht
  if (detail && now - lastInput > 120000) detail = false;
  u8g2.clearBuffer();
  if (detail) drawDetail(); else drawOverview();
  u8g2.sendBuffer();
}

}  // namespace display
