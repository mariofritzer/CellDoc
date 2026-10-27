// CellDoc Mini/Nano – Pinbelegung und Grenzwerte (passend zu docs/img/schaltplan-mini-*.svg bzw. schaltplan-nano.svg)
#pragma once
#include <Arduino.h>

#ifdef CELLDOC_NANO
#undef SLOTS
#define SLOTS 1
#endif

#ifndef SLOTS
#define SLOTS 2
#endif

namespace mcfg {

constexpr uint8_t NSLOTS = SLOTS;

#if defined(CELLDOC_NANO)
// ESP32-C3 SuperMini: IO2/8/9 sind Boot-Pins (8 = blaue LED, 9 = BOOT-Taste), IO18/19 = USB
constexpr uint8_t PIN_SDA = 5;
constexpr uint8_t PIN_SCL = 6;
constexpr uint8_t PIN_ONEWIRE = 4;
constexpr uint8_t PIN_LOAD_PWM = 3;   // Gate AO3400 über 1 kΩ, 10 kΩ nach GND: HIGH = Last an
constexpr uint8_t PIN_CHG[4] = {7, 7, 7, 7};  // HIGH = Lader an (nur Index 0 genutzt)
constexpr uint8_t PIN_LED = 8;        // blaue LED auf dem Board, LOW = an
constexpr uint8_t PIN_BUZZER = 10;    // optional
constexpr uint8_t PIN_BTN_OK = 9;     // BOOT-Taste: kurz = Test starten/bestätigen, lang = stoppen
#elif CONFIG_IDF_TARGET_ESP32S3
// ESP32-S3-DevKitC-1 (N16R8): IO35–37 gehören zum PSRAM, IO19/20 zu USB, IO0/3/45/46 sind Boot-Pins
constexpr uint8_t PIN_SDA = 8;
constexpr uint8_t PIN_SCL = 9;
constexpr uint8_t PIN_ONEWIRE = 4;
constexpr uint8_t PIN_LOAD_EN_N = 5;
constexpr uint8_t PIN_CHG[4] = {6, 7, 15, 16};
constexpr uint8_t PIN_FAN = 10;
constexpr uint8_t PIN_BUZZER = 12;
constexpr uint8_t PIN_BTN_PREV = 17;
constexpr uint8_t PIN_BTN_OK = 18;
constexpr uint8_t PIN_BTN_NEXT = 11;
#else
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
#endif
#if !defined(CELLDOC_NANO)
constexpr uint8_t PIN_BOOT = 0;  // BOOT-Taste = Not-Aus
#endif

// INA226 je Schacht: A0/A1 nur an GND oder VCC, das können alle gängigen Module
constexpr uint8_t ADDR_INA[4] = {0x40, 0x41, 0x44, 0x45};
constexpr uint8_t ADDR_DAC = 0x60;    // MCP4728
constexpr uint8_t ADDR_OLED = 0x3C;   // SH1106

constexpr float SHUNT_OHM = 0.010f;
constexpr float LOAD_SENSE_OHM = 0.10f;
constexpr float DAC_DIVIDER = 8.5f;  // 7,5 kΩ / 1 kΩ
constexpr float DAC_VREF = 3.3f;

#ifdef CELLDOC_NANO
// Nano: fester Lastwiderstand, Strom per PWM (10 kHz) geregelt, INA226 mittelt
constexpr float LOAD_OHM = 3.0f;          // Lastwiderstand + MOSFET + Leitungen (Schätzwert für die Vorsteuerung)
constexpr uint32_t LOAD_PWM_HZ = 10000;
constexpr float MAX_CURRENT_A = 1.2f;     // bei 3 Ω bis ca. 3,6 V konstant, darunter bleibt der Strom unter dem Sollwert
constexpr float DEFAULT_CURRENT_A = 0.8f; // bis zur Abschaltspannung echter Konstantstrom
constexpr float MAX_TOTAL_W = 6.0f;
#else
// Entladestrom je Schacht: 2 A bei 1–2 Schächten, 1 A bei 4 (Wärme)
constexpr float MAX_CURRENT_A = NSLOTS > 2 ? 1.0f : 2.0f;
constexpr float DEFAULT_CURRENT_A = 0.5f;
constexpr float MAX_TOTAL_W = 16.0f;
#endif
constexpr float CHG_MAX_A = 1.2f;  // TP4056 mit 1,2 kΩ
constexpr float MAX_CELL_V = 4.30f;
constexpr float OVERCURRENT_FACTOR = 1.25f;
constexpr uint32_t OVERCURRENT_MS = 300;
#ifdef CELLDOC_NANO
constexpr float SINK_STOP_C = 120.0f;  // optionaler Fühler am Lastwiderstand (Alugehäuse verträgt mehr)
#else
constexpr float SINK_STOP_C = 85.0f;
#endif

constexpr uint32_t INA_PERIOD_MS = 40;
constexpr uint32_t TEMP_PERIOD_MS = 1000;
constexpr uint32_t CHG_THROTTLE_PERIOD_MS = 10000;

}  // namespace mcfg
