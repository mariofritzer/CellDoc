// CellDoc – Einstellungen (LittleFS /settings.json)
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

#ifdef CELLDOC_MINI
// Mini: Rollen 0..3 = Zelle in Schacht 1..4, 4 = Kühlkörper
enum SensorRole : uint8_t { SENSOR_CELL = 0, SENSOR_SINK = 4, SENSOR_ROLES = 5 };
#else
enum SensorRole : uint8_t { SENSOR_CELL = 0, SENSOR_PACK = 1, SENSOR_SINK = 2, SENSOR_ROLES = 3 };
#endif

struct Settings {
  String wifiSsid;
  String wifiPass;

  // Temperaturgrenzen Zelle/Pack
  float chargeWarnC = 40, chargeStopC = 45;
  float dischargeWarnC = 45, dischargeStopC = 55;
  // Kühlkörper (nach oben durch cfg::SINK_STOP_C begrenzt)
  float sinkWarnC = 75, sinkStopC = 90;

  // Kalibrierung: Index 0 = Anschluss A bzw. Schacht 1, 1 = Anschluss B bzw. Schacht 2 ...
  float vGain[4] = {1, 1, 1, 1};
  float iGain[4] = {1, 1, 1, 1};
  float iOffset[4] = {0, 0, 0, 0};  // A
  float balGain[6] = {1, 1, 1, 1, 1, 1};

  // DS18B20-Zuordnung
  uint8_t sensorAddr[SENSOR_ROLES][8] = {};
  bool sensorSet[SENSOR_ROLES] = {};

  uint16_t restMin = 30;
  bool riInOhm = false;
  bool buzzer = true;
  uint32_t nextCellNo = 1;    // nächste Zellnummer (Etikett)
  uint32_t nextResultId = 1;  // nächste Ergebnisnummer
  String cellPrefix = "CD";   // Zell-ID = Präfix-0042

  // MQTT für Home Assistant
  bool mqttOn = false;
  String mqttHost;
  uint16_t mqttPort = 1883;
  String mqttUser, mqttPass;
  String mqttBase = "celldoc";

  // Mehrere Geräte: eigener Name im Netz und Partnergeräte (Namen oder IPs, mit Komma getrennt)
#if defined(CELLDOC_NANO)
  String hostname = "celldoc-nano";
#elif defined(CELLDOC_MINI)
  String hostname = "celldoc-mini";
#else
  String hostname = "celldoc";
#endif
  String peers;

  // Mini: höchstens so viele Schächte laden gleichzeitig (0 = alle). Mini 4 am 5-V/5-A-Regler: 3
#if defined(CELLDOC_MINI) && defined(SLOTS) && SLOTS > 2
  uint8_t chargeSlots = 3;
#else
  uint8_t chargeSlots = 0;
#endif

  void toJson(JsonObject o, bool withSecrets) const;
  void fromJson(JsonObjectConst o);
  bool load();
  bool save() const;
};

extern Settings settings;

String addrToHex(const uint8_t* a);
bool hexToAddr(const char* s, uint8_t* a);
