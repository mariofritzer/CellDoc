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
  float rLead[4] = {0, 0, 0, 0};    // Ω zwischen Spannungsmesspunkt und Zelle (Kabel, Federn, Kontakte)
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

  // Test-Standardwerte (Start-Dialog, BOOT-Taste, Home Assistant)
  float tI = 0;           // A, 0 = Gerätevorgabe
  float tCut = 3.0f;      // V
  uint8_t tCyc = 1;
  bool tPre = true;
  uint8_t tAfter = 1;     // 0 nichts, 1 Lagerspannung, 2 voll
  uint8_t tStorPct = 50;  // Lagerladung in % der gemessenen Kapazität
  uint8_t tRestAfter = 10;  // min Erholen nach dem Entladen

  // Laden und Sicherheit
  float chgA = 1.0f;     // Nennstrom des Lademoduls
  float fullA = 0.12f;   // voll, wenn der Ladestrom darunter fällt …
  float fullV = 4.15f;   // … und die Zelle mindestens so viel hat
  uint8_t chgMaxH = 8;   // längste Ladezeit, 0 = aus
  float maxV = 4.25f;    // Überspannung beim Laden
  float ocF = 1.25f;     // Überstrom: Faktor auf den Sollstrom
  float noTempA = 1.0f;  // Höchststrom ohne Zellfühler

  // Gerät und System
  String tz = "CET-1CEST,M3.5.0,M10.5.0/3";
  String ntp = "pool.ntp.org";

  // Anzeige und Töne
  bool bzKey = true, bzDone = true, bzErr = true, bzBoot = true, bzScan = true;
  bool led = true;
  uint8_t oledBr = 180;
  uint16_t oledOff = 0;  // min ohne Bedienung bis das Display dunkel wird, 0 = nie
  bool nightOn = false;
  uint16_t nightFrom = 22 * 60, nightTo = 7 * 60;  // Minuten nach Mitternacht
  bool nightBz = true, nightLed = true, nightOled = true;  // was nachts aus ist

  // Benachrichtigungen
  String ntfy;           // https://ntfy.sh/thema
  String tgTok, tgChat;  // Telegram-Bot
  bool nDone = true, nErr = true, nChg = false;

  // Netzwerk
  String ip, gw, mask, dns;  // leer = DHCP
  String ssid2, pass2;       // Ausweich-WLAN
  String apName, apPass;     // leer = CellDoc-XXXX / Standardpasswort

  // QR-Scanner: 0 = nur merken, 1 = Schacht zuordnen, 2 = zuordnen und Test starten
  uint8_t scanMode = 1;

  // Experten
  bool verbose = false;

  // Reine Oberflächen-Einstellungen (Profile, Zelltypen, Bewertung, Etiketten …) als JSON-Text
  String ui = "{}";

  bool night() const;              // gerade Nachtmodus?
  bool sound(char kind) const;     // k Taste, d fertig, e Fehler, b Start/WLAN, s Scan

  void toJson(JsonObject o, bool withSecrets) const;
  void fromJson(JsonObjectConst o);
  bool load();
  bool save() const;
};

extern Settings settings;

String addrToHex(const uint8_t* a);
bool hexToAddr(const char* s, uint8_t* a);
