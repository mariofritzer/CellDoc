// CellDoc – Schnittstelle zwischen gemeinsamer Web-API und Gerät
#pragma once

namespace device {
void routes();   // gerätespezifische Routen anmelden
bool busy();     // läuft ein Vorgang? (kein Update möglich)
void killAll();  // alles sicher abschalten
void idle();     // Messung weiterlaufen lassen, während auf das WLAN gewartet wird
}  // namespace device
