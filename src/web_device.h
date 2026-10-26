// CellDoc – Schnittstelle zwischen gemeinsamer Web-API und Gerät
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

namespace device {
void routes();   // gerätespezifische Routen anmelden
bool busy();     // läuft ein Vorgang? (kein Update möglich)
void killAll();  // alles sicher abschalten
void idle();     // Messung weiterlaufen lassen, während auf das WLAN gewartet wird

void fillState(JsonObject o);  // Inhalt von /api/state ohne die gemeinsamen Felder
// Ruhespannung am gewählten Anschluss bzw. Schacht (für die Selbstentladung), leer = ok
String cellVoltage(JsonDocument& body, float& v);
bool cellInUse(uint32_t no);

// Home Assistant
struct MqttEntity {
  String obj, name, tpl, unit, dclass;
  bool button;  // Taste statt Sensor, tpl = Befehl
  MqttEntity(const String& o, const String& n, const String& t, const String& u = "", const String& d = "", bool b = false)
      : obj(o), name(n), tpl(t), unit(u), dclass(d), button(b) {}
};
const char* model();
// Status für die RGB-LED des S3-Boards: 0 bereit, 1 lädt, 2 entlädt, 3 fertig, 4 Fehler
uint8_t ledState();
uint32_t scannedCell();  // vom QR-Scanner gewählte Zelle (0 = keine)

// QR-Scanner am Gerät: no = Zelle (0 = unbekannter Code)
void onScan(uint32_t no, const String& id);
void scanTick(uint32_t now);
void scanState(JsonObject o);  // gerätespezifische Zuordnung für /api/state
void mqttEntities(std::vector<MqttEntity>& out);
void mqttCommand(const String& cmd);
}  // namespace device
