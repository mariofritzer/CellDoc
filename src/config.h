// CellDoc – Pinbelegung und feste Grenzwerte
// Passt zur Hardware-Beschreibung in docs/hardware.md
#pragma once
#include <Arduino.h>

#ifndef CELLDOC_VERSION
#define CELLDOC_VERSION "dev"
#endif

namespace cfg {

// ---------- Pins ----------
#if CONFIG_IDF_TARGET_ESP32S3
// ESP32-S3-DevKitC-1 (N16R8): IO35–37 gehören zum PSRAM, IO19/20 zu USB, IO0/3/45/46 sind Boot-Pins
constexpr uint8_t PIN_SDA        = 8;
constexpr uint8_t PIN_SCL        = 9;
constexpr uint8_t PIN_ONEWIRE    = 4;
constexpr uint8_t PIN_LOAD_EN_N  = 5;
constexpr uint8_t PIN_RELAY_K1   = 6;
constexpr uint8_t PIN_RELAY_K2   = 7;
constexpr uint8_t PIN_CHG_CS_HI  = 15;
constexpr uint8_t PIN_CHG_CS_LO  = 16;
constexpr uint8_t PIN_CHG_CHRG   = 17;
constexpr uint8_t PIN_CHG_STDBY  = 18;
constexpr uint8_t PIN_FAN_PWM    = 10;
constexpr uint8_t PIN_FAN_TACH   = 11;
constexpr uint8_t PIN_BUZZER     = 12;
constexpr uint8_t PIN_BUTTON     = 0;
constexpr uint8_t PIN_SCAN_RX    = 13;
constexpr uint8_t PIN_SCAN_TX    = 14;
constexpr int8_t  PIN_RGB        = 48;  // RGB-LED auf dem Board (WS2812)
constexpr uint8_t PIN_BTN_PREV   = 1;   // Taster gegen GND (optional, interne Pull-ups)
constexpr uint8_t PIN_BTN_OK     = 2;
constexpr uint8_t PIN_BTN_NEXT   = 42;
#else
constexpr uint8_t PIN_SDA        = 21;
constexpr uint8_t PIN_SCL        = 22;
constexpr uint8_t PIN_ONEWIRE    = 4;
constexpr uint8_t PIN_LOAD_EN_N  = 16;  // LOW = Last frei (BSS138 gibt Sollwert und Gates frei)
constexpr uint8_t PIN_RELAY_K1   = 26;  // Laderelais, HIGH = Lader an Anschluss A
constexpr uint8_t PIN_RELAY_K2   = 27;  // Lastumschaltung, LOW = A (NC), HIGH = B (NO)
constexpr uint8_t PIN_CHG_CS_HI  = 32;  // HIGH = CS auf VIN = 4,2 V
constexpr uint8_t PIN_CHG_CS_LO  = 33;  // HIGH = CS auf GND = Lader aus
constexpr uint8_t PIN_CHG_CHRG   = 34;  // LOW = lädt
constexpr uint8_t PIN_CHG_STDBY  = 35;  // LOW = voll
constexpr uint8_t PIN_FAN_PWM    = 25;  // über AO3400, Signal invertiert
constexpr uint8_t PIN_FAN_TACH   = 39;
constexpr uint8_t PIN_BUZZER     = 17;
constexpr uint8_t PIN_BUTTON     = 0;   // BOOT-Taste = Not-Aus
// QR-Scanner (optional, GM861S, 3,3 V, 9600 Baud) – gleiche Pins bei CellDoc und Mini
constexpr uint8_t PIN_SCAN_RX    = 19;  // vom TX des Scanners
constexpr uint8_t PIN_SCAN_TX    = 18;  // zum RX des Scanners
constexpr int8_t  PIN_RGB        = -1;
constexpr uint8_t PIN_BTN_PREV   = 13;  // Taster gegen GND (optional, interne Pull-ups)
constexpr uint8_t PIN_BTN_OK     = 14;
constexpr uint8_t PIN_BTN_NEXT   = 23;
#endif

// ---------- I2C-Adressen ----------
constexpr uint8_t ADDR_INA_A   = 0x40;
constexpr uint8_t ADDR_INA_B   = 0x41;
constexpr uint8_t ADDR_DAC     = 0x60;
constexpr uint8_t ADDR_ADS_1   = 0x48;  // Balancer-Abgriffe 1–4
constexpr uint8_t ADDR_ADS_2   = 0x49;  // Balancer-Abgriffe 5–6
constexpr uint8_t ADDR_OLED    = 0x3C;  // SH1106 1,3" (optional)

// ---------- Messkette ----------
constexpr float SHUNT_OHM        = 0.010f;   // INA226-Shunt A und B
constexpr float LOAD_SENSE_OHM   = 0.10f;    // je Lastkanal
constexpr uint8_t LOAD_CHANNELS  = 2;
constexpr float DAC_DIVIDER      = 8.5f;     // 7,5 kΩ / 1 kΩ
constexpr float DAC_VREF         = 3.3f;
constexpr float BAL_DIVIDER      = 11.0f;    // 100 kΩ / 10 kΩ
constexpr uint8_t BAL_TAPS       = 6;

// ---------- Harte Grenzen (nicht per Weboberfläche änderbar) ----------
constexpr float MAX_POWER_W      = 30.0f;
constexpr float MAX_CURRENT_A    = 8.0f;
constexpr float MAX_CURRENT_PORT_A = 5.0f;   // Einzelzelle, F1 = 7,5 A
constexpr float MAX_PACK_V       = 26.0f;    // 6S voll + Reserve
constexpr float ABS_MIN_CELL_V   = 2.0f;     // darunter wird nie entladen
constexpr float OVERCURRENT_FACTOR = 1.25f;
constexpr uint32_t OVERCURRENT_MS  = 300;
constexpr float SINK_STOP_C      = 90.0f;
constexpr float SINK_WARN_C      = 75.0f;

// ---------- Zeitraster ----------
constexpr uint32_t INA_PERIOD_MS  = 40;
constexpr uint32_t TEMP_PERIOD_MS = 1000;
constexpr uint32_t CHG_THROTTLE_PERIOD_MS = 10000;

// ---------- Netzwerk ----------
constexpr const char* AP_PASSWORD = "celldoc1";
constexpr const char* HOSTNAME    = "celldoc";

}  // namespace cfg
