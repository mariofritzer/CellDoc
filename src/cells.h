// CellDoc – Zellenbestand: jede Zelle bekommt eine feste ID fürs Etikett
// /z/<nr>.json  {no, id, name, chem, preset, presetName, cells, nomCapMah, riNew,
//               loc, note, status, pack, created, tests[], first{}, last{}, sd{}}
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace cells {

// Daten, die ein Vorgang für seine Zelle braucht
struct JobCell {
  uint32_t no = 0;      // 0 = neue Zelle anlegen
  String id;            // z. B. CD-0042
  String name;
  String presetId;
  const char* chem = "liion";
  uint8_t cellCount = 1;
  float nomCapMah = 0;
  float riNewMilli = 0;
};

bool begin();
String makeId(uint32_t no);
uint32_t parseRef(const String& s);  // „CD-0042“, „#42“, „42“ → 42, 0 = ungültig
bool exists(uint32_t no);
bool load(uint32_t no, JsonDocument& d);
bool save(uint32_t no, JsonDocument& d);
bool remove(uint32_t no);
uint32_t create(JsonObjectConst info);  // neue Zelle ohne Test (z. B. zum Vorab-Etikettieren)

// Vor einem Vorgang: vorhandene Zelle laden oder neu anlegen, fehlende Angaben gegenseitig ergänzen
String prepare(JobCell& c);

// Nach dem Test: Ergebnis eintragen und Zusammenfassung aktualisieren
void addResult(uint32_t no, JsonDocument& result);
float firstCapacity(uint32_t no);

// Selbstentladung: Startspannung merken, später erneut messen
String sdStart(uint32_t no, float v);
String sdMeasure(uint32_t no, float v);
String sdReset(uint32_t no);

// Zähler nach Wiederherstellung neu setzen
void rescanCounters();

}  // namespace cells
