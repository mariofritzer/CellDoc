// CellDoc – gemeinsamer Zellenbestand mehrerer Geräte im selben WLAN
// Jedes Gerät hält eine vollständige Kopie. Geänderte Zellen werden von den Partnergeräten
// abgeholt (HTTP), die neuere Fassung gewinnt. Neue Nummern stimmen die Geräte vorher ab.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace peersync {
void update();                  // in loop()
void changed();                 // eine Zelle wurde hier geändert: Partner informieren
void poked();                   // ein Partner hat etwas geändert: bald abholen
void syncNow();                 // sofort mit allen Partnern abgleichen
uint32_t peerNextNo();          // höchste „nächste Nummer“ der erreichbaren Partner
void reserve(uint32_t no);      // Partnern mitteilen, dass diese Nummer vergeben ist
bool merge(JsonDocument& d);    // eine Zelle von einem Partner übernehmen
void status(JsonObject o);
void discover(JsonArray out);   // Geräte per mDNS suchen (dauert ein paar Sekunden)
}  // namespace peersync
