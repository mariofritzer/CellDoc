// CellDoc Mini – Pinbelegung und Grenzwerte (passend zu docs/img/schaltplan-mini-*.svg)
#pragma once
#include <Arduino.h>

#ifndef SLOTS
#define SLOTS 2
#endif

namespace mcfg {

constexpr uint8_t NSLOTS = SLOTS;

constexpr uint8_t PIN_SDA = 21;
constexpr uint8_t PIN_SCL = 22;
constexpr uint8_t PIN_ONEWIRE = 4;
constexpr uint8_t PIN_LOAD_EN_N = 16;  // LOW = Last frei (BSS138-Klemmen offen)
constexpr uint8_t PIN_CHG[4] = {26, 27, 32, 33};  // HIGH = Lader des Schachts an
constexpr uint8_t PIN_FAN = 25;
constexpr uint8_t PIN_BUZZER = 17;
constexpr uint8_t PIN_BTN_PREV = 34;
constexpr uint8_t PIN_BTN_OK = 35;
constexpr uint8_t PIN_BTN_NEXT = 39;
constexpr uint8_t PIN_BOOT = 0;  // BOOT-Taste = Not-Aus

constexpr uint8_t ADDR_INA0 = 0x40;   // Schacht k: 0x40 + k
constexpr uint8_t ADDR_DAC = 0x60;    // MCP4728
constexpr uint8_t ADDR_OLED = 0x3C;   // SH1106

constexpr float SHUNT_OHM = 0.010f;
constexpr float LOAD_SENSE_OHM = 0.10f;
constexpr float DAC_DIVIDER = 8.5f;  // 7,5 kΩ / 1 kΩ
constexpr float DAC_VREF = 3.3f;

// Entladestrom je Schacht: 2 A bei 1–2 Schächten, 1 A bei 4 (Wärme)
constexpr float MAX_CURRENT_A = NSLOTS > 2 ? 1.0f : 2.0f;
constexpr float MAX_TOTAL_W = 16.0f;
constexpr float CHG_MAX_A = 1.2f;  // TP4056 mit 1,2 kΩ
constexpr float MAX_CELL_V = 4.30f;
constexpr float OVERCURRENT_FACTOR = 1.25f;
constexpr uint32_t OVERCURRENT_MS = 300;
constexpr float SINK_STOP_C = 85.0f;

constexpr uint32_t INA_PERIOD_MS = 40;
constexpr uint32_t TEMP_PERIOD_MS = 1000;
constexpr uint32_t CHG_THROTTLE_PERIOD_MS = 10000;

}  // namespace mcfg
