// CellDoc – Einstellungen (LittleFS /settings.json)
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

enum SensorRole : uint8_t { SENSOR_CELL = 0, SENSOR_PACK = 1, SENSOR_SINK = 2, SENSOR_ROLES = 3 };

struct Settings {
  String wifiSsid;
  String wifiPass;

  // Temperaturgrenzen Zelle/Pack
  float chargeWarnC = 40, chargeStopC = 45;
  float dischargeWarnC = 45, dischargeStopC = 55;
  // Kühlkörper (nach oben durch cfg::SINK_STOP_C begrenzt)
  float sinkWarnC = 75, sinkStopC = 90;

  // Kalibrierung: Index 0 = Anschluss A, 1 = Anschluss B
  float vGain[2] = {1, 1};
  float iGain[2] = {1, 1};
  float iOffset[2] = {0, 0};  // A
  float balGain[6] = {1, 1, 1, 1, 1, 1};

  // DS18B20-Zuordnung
  uint8_t sensorAddr[SENSOR_ROLES][8] = {};
  bool sensorSet[SENSOR_ROLES] = {false, false, false};

  uint16_t restMin = 30;
  bool riInOhm = false;
  bool buzzer = true;
  uint32_t nextCellNo = 1;

  void toJson(JsonObject o, bool withSecrets) const;
  void fromJson(JsonObjectConst o);
  bool load();
  bool save() const;
};

extern Settings settings;

String addrToHex(const uint8_t* a);
bool hexToAddr(const char* s, uint8_t* a);
