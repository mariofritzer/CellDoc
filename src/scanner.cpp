#include "scanner.h"
#include <driver/gpio.h>
#include "cells.h"
#include "config.h"
#include "web_device.h"

namespace scanner {

static String buf;
static String lastCode;
static uint32_t lastAt = 0, seq = 0, lastNo = 0;
static String lastId;
static bool lastOk = false, present = false;

void begin() {
  Serial2.begin(9600, SERIAL_8N1, cfg::PIN_SCAN_RX, cfg::PIN_SCAN_TX);
  gpio_pullup_en((gpio_num_t)cfg::PIN_SCAN_RX);  // ohne Scanner kein Rauschen
}

// Erkennt Etiketten von CellDoc: Link „…/#z42“, „CD-0042“ oder nur die Zahl
static uint32_t parse(const String& code) {
  int z = code.indexOf("#z");
  if (z >= 0) return strtoul(code.c_str() + z + 2, nullptr, 10);
  for (char c : code)
    if (!(isalnum((unsigned char)c) || c == '-' || c == '#' || c == ' ')) return 0;
  return cells::parseRef(code);
}

static void handle(const String& code) {
  uint32_t now = millis();
  // im Dauerbetrieb liest der Scanner denselben Code mehrfach
  if (code == lastCode && now - lastAt < 3000) {
    lastAt = now;
    return;
  }
  lastCode = code;
  lastAt = now;
  uint32_t no = parse(code);
  lastOk = no && cells::exists(no);
  lastNo = lastOk ? no : 0;
  lastId = lastOk ? cells::makeId(no) : code.substring(0, 40);
  seq++;
  JsonDocument d;
  if (lastOk && cells::load(no, d)) lastId = String(d["id"] | lastId.c_str());
  device::onScan(lastNo, lastId);
}

void update() {
  while (Serial2.available()) {
    char c = Serial2.read();
    present = true;
    if (c == '\r' || c == '\n') {
      buf.trim();
      if (buf.length()) handle(buf);
      buf = "";
    } else if (c >= 32 && buf.length() < 96) {
      buf += c;
    }
  }
  device::scanTick(millis());
}

void fill(JsonObject o) {
  o["scanner"] = present;
  if (!seq) return;
  JsonObject s = o["scan"].to<JsonObject>();
  s["seq"] = seq;
  s["ok"] = lastOk;
  s["no"] = lastNo;
  s["id"] = lastId;
  s["age"] = (millis() - lastAt) / 1000;
}

}  // namespace scanner
