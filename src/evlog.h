// Ereignisprotokoll: was ist wann und warum passiert (Start, Phasen, Ende, Abbrüche, System)
#pragma once
#include <Arduino.h>

namespace evlog {

void begin();  // nach storage::begin(), schreibt den Starteintrag mit Neustartgrund
// slot: 0 = System, 1.. = Schacht; level: 'i' Info, 'w' Warnung, 'e' Fehler
void add(char level, int slot, const String& text);
inline void info(int slot, const String& t) { add('i', slot, t); }
inline void warn(int slot, const String& t) { add('w', slot, t); }
inline void err(int slot, const String& t) { add('e', slot, t); }
void clear();
void routes();  // GET /api/log (Text, eine Zeile je Eintrag), POST /api/log/clear

// Zahl mit Komma für Protokolltexte
String num(float v, int dec);

}  // namespace evlog
