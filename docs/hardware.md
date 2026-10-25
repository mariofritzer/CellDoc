# CellDoc – Hardware

Schaltung, Pinbelegung und Einkaufsliste. Aufbau auf Lochraster oder eigener Platine, **nicht auf dem Steckbrett** (bis 8 A).

| Eckdaten | |
|---|---|
| Last | max. 30 W, max. 8 A (Anschluss A max. 5 A) |
| Packs | bis 6S / 25,2 V, Einzelzellen über Balancer |
| Laden | nur 1S an Anschluss A, 1 A, Li-Ion/LiPo (4,2 V) oder LiFePO4 (3,6 V) |
| Versorgung | 12 V / 3 A |

## Schaltplan

[![Schaltplan](img/schaltplan.svg)](img/schaltplan.svg)

Erzeugt mit `python3 tools/wiring_gen.py docs/img/schaltplan.svg`. Netzlabel mit gleichem Namen sind verbunden, dicke Linien führen Akku- und Laststrom.

## Aufbau

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="img/aufbau-dark.jpg">
  <img src="img/aufbau-light.jpg" alt="3D-Ansicht des Aufbaus">
</picture>

## Anschlussplan

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="img/anschlussplan-dark.jpg">
  <img src="img/anschlussplan-light.jpg" alt="Anschlussplan in der Draufsicht">
</picture>

Die Bilder entstehen aus `tools/render/scene.py` (Blender) und `tools/render/compose.py`. Die Anordnung ist ein Vorschlag, die Kabelfarben gelten für die ganze Doku:

| Farbe | Leitung |
|---|---|
| Rot | 12 V |
| Rosa | 5 V |
| Schwarz | Masse |
| Orange (dick) | Akku- und Laststrom |
| Grün (dick) | Ladestrom TP5000 → K1 |
| Blau / Gelb | I²C SDA / SCL |
| Türkis | 1-Wire zu den DS18B20 |
| Lila | Steuersignale (Relais, Lader, Last, Lüfter) |
| Grau | Balancer-Abgriffe |

## Blockschaltbild

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="img/blockschaltbild-dark.png">
  <img src="img/blockschaltbild-light.png" alt="Blockschaltbild von CellDoc">
</picture>

Zelle A und Pack B haben getrennte Sicherungen und Strommesser. K2 schaltet die Last auf genau einen Anschluss. Der Lader ist fest nur mit Anschluss A verdrahtet und kann ein Pack daher nie erreichen. Alle Minuspole treffen sich an einem Masse-Sternpunkt.

## Leistungspfad

| Von | Nach | Hinweis |
|---|---|---|
| Zelle A + | F1 (7,5 A Flachsicherung) | 18650-Halter, Schraubklemme und Lötpads parallel |
| nach F1 | D1 Kathode, Anode an GND | Verpolschutz: bei falsch gepolter Zelle leitet D1 und F1 löst aus |
| nach F1 | INA226 A IN+ → Shunt → IN− = Knoten A | Shunt 10 mΩ statt des 100 mΩ auf dem Modul |
| Zellpol + (eigene Leitung) | INA226 A VBUS | Fühlerleitung direkt am Pol, damit Kabelverluste nicht in den Innenwiderstand eingehen |
| Knoten A | K2 NC | Ruhezustand: Last an Zelle A |
| TP5000 BAT+ | K1 → Knoten A | Ladestrom läuft durch INA226 A und wird mitgezählt |
| Pack B + (XT60) | F2 (10 A) → D2 → INA226 B → Knoten B | Aufbau wie A, VBUS ebenfalls per Fühlerleitung |
| Knoten B | K2 NO | Nur bei aktivem Packtest |
| K2 COM | Last+ (beide Drains) | Silikonkabel 1,5 mm² |
| Zelle A −, Pack B −, Last-GND | Masse-Sternpunkt | auch ESP32-, INA226- und ADS1115-GND |

## Konstantstromlast

Zwei gleiche Kanäle mit je 15 W. Beide bekommen denselben Sollwert und teilen sich den Strom. Die Firmware regelt den Sollwert mit dem INA226 nach, Bauteiltoleranzen gehen deshalb nicht ins Ergebnis ein.

**Sollwert**

| Bauteil | Anschluss | Verbindung |
|---|---|---|
| MCP4725 | VOUT | 7,5 kΩ → Knoten SOLL → 1 kΩ → GND (0–3,3 V → 0–0,39 V) |
| Q3 BSS138 | D an SOLL, S an GND | Gate an FREIGABE |
| FREIGABE | 10 kΩ nach 3,3 V | ESP32 GPIO 16 zieht LOW = Last frei. Beim Booten oder Absturz ist die Last automatisch aus |

**Je Kanal (zweimal aufbauen)**

| Bauteil | Anschluss | Verbindung |
|---|---|---|
| LM358 (12 V) | IN+ | Knoten SOLL |
| LM358 | IN− | 10 kΩ zum Knoten SENSE, 10 nF von OUT nach IN− |
| LM358 | OUT | 1 kΩ → Gate IRFP250N |
| IRFP250N | Drain | Last+ (K2 COM) |
| IRFP250N | Source | Knoten SENSE → 0,1 Ω / 5 W → GND |
| Q4/Q5 BSS138 | D an Gate, S an GND | Gate an FREIGABE, zieht den MOSFET hart aus |
| Gate | 10 kΩ nach GND | Pull-down |

Beide IRFP250N isoliert (Drain liegt am Gehäuse) auf einen CPU-Kühler mit 4-Pin-Lüfter schrauben. DS18B20 T3 mit Wärmeleitkleber zwischen die MOSFETs.

## Lader TP5000

Der CS-Pin wählt die Ladeschlussspannung: **hoch = 4,2 V**, **offen = 3,6 V**, **auf Masse = aus**. Bei Modulen mit 3,6-V-Lötbrücke diese öffnen, damit der ESP32 den Pin steuern kann.

| Modulpin | Verbindung | Zweck |
|---|---|---|
| VIN / GND | 5 V vom Step-Down | Versorgung |
| CS | P-MOSFET AO3401 nach 5 V, über NPN von GPIO 32 | 4,2 V |
| CS | N-MOSFET AO3400 nach GND, Gate an GPIO 33 (10 kΩ Pull-down) | Stopp und Drosselung im Takt |
| CS | beide aus | 3,6 V (LiFePO4), Zustand beim Booten |
| CHRG / STDBY | je 1 kΩ zu GPIO 34 / 35, 10 kΩ Pull-up nach 3,3 V | lädt / voll |
| Rs | 0,1 Ω (Standard) | 1 A Ladestrom |
| TS | an GND | interne Temperaturüberwachung aus, das übernimmt T1 |
| BAT+ | K1 (Schließer) → Knoten A | Lader nur verbunden, wenn K1 anzieht |

## Balancer

Jeder Abgriff des JST-XH-Steckers bekommt einen Teiler 100 kΩ / 10 kΩ (0,1 %). Abgriff 1–4 an ADS1115 Nr. 1 (0x48, ADDR an GND), Abgriff 5–6 an ADS1115 Nr. 2 (0x49, ADDR an VDD). Einmal mit dem Multimeter kalibrieren (Einstellungen → Kalibrierung).

## Temperatur, Lüfter, Summer

| Teil | Verbindung | Hinweis |
|---|---|---|
| 3× DS18B20 | parallel an GPIO 4, 4,7 kΩ nach 3,3 V | T1 Zelle A, T2 Pack, T3 Kühlkörper |
| Lüfter PWM | AO3400 Drain an PWM-Pin, Gate an GPIO 25 | 25 kHz |
| Lüfter TACH | GPIO 39, 10 kΩ nach 3,3 V | Lüfterausfall schaltet die Last ab |
| Summer | aktiver 5-V-Summer über AO3400 an GPIO 17 | Alarm und „fertig“ |

## ESP32-Pinbelegung

| GPIO | Funktion |
|---|---|
| 21 / 22 | I²C SDA / SCL (INA226 ×2, MCP4725, ADS1115 ×2), 4,7 kΩ Pull-ups |
| 4 | 1-Wire DS18B20 ×3 |
| 16 | Last-Freigabe (LOW = frei) |
| 26 | K1 Laderelais |
| 27 | K2 Lastumschaltung A/B |
| 32 | TP5000 CS hoch (4,2 V) |
| 33 | TP5000 CS auf Masse (Stopp) |
| 34 / 35 | TP5000 CHRG / STDBY |
| 25 | Lüfter PWM |
| 39 | Lüfter Drehzahl |
| 17 | Summer |
| 0 | BOOT-Taste = Not-Aus |

Relaismodule mit Jumper auf **High Level Trigger**, dann bleiben die Relais beim Booten aus.

## Einkaufsliste

| Menge | Teil | Wofür |
|---|---|---|
| 1 | ESP32 DevKit V1 (WROOM-32, 30 Pin) | Steuerung |
| 2 | INA226-Modul + 2× Shunt 10 mΩ (2512, 1 %) | Strom und Spannung A/B |
| 1 | MCP4725-Modul | Sollwert Last |
| 2 | ADS1115-Modul | Balancer bis 6S |
| 1 | TP5000-Modul (4,2 V / 3,6 V) | Lader 1S |
| 1 | 2-Kanal-Relaismodul 5 V, 10 A, mit High/Low-Jumper | K1, K2 |
| 2 | IRFP250N + isolierende Wärmeleitpads | Last |
| 2 | Widerstand 0,1 Ω / 5 W | Messwiderstand Last |
| 1 | LM358 + Sockel | Regler Last |
| 1 | CPU-Kühler mit 4-Pin-Lüfter 12 V | Kühlung |
| 3 | DS18B20 wasserdicht | T1–T3 |
| – | 3× BSS138, 4× AO3400, 1× AO3401, 1× BC547 | Schalter |
| 1 | Aktiver Summer 5 V | Alarm |
| 1 | Step-Down LM2596 (auf 5,0 V einstellen) | 5-V-Versorgung |
| 1 | Netzteil 12 V / 3 A + Hohlbuchse | Versorgung |
| 3 | Flachsicherungshalter + Sicherungen 3 / 7,5 / 10 A | F0–F2 |
| 2 | Schottky 15SQ045 | Verpolschutz |
| 1 | 18650-Halter, Schraubklemme ≥ 10 A | Anschluss A |
| 1 | XT60-Buchse, JST-XH 2S–6S oder Balancer-Adapter | Anschluss B |
| – | Widerstände 12× 100 kΩ + 12× 10 kΩ (0,1 %), 7,5 kΩ, 1 kΩ, 10 kΩ, 4,7 kΩ; 10 nF, 100 nF | |
| – | Lochraster, Silikonkabel 1,5 mm² und 0,25 mm² | Aufbau |

## Aufbau und erste Inbetriebnahme

1. Starkstromwege mit Drahtbrücken oder 1,5-mm²-Kabel, nicht über dünne Lochraster-Leiterbahnen.
2. Masse als Stern, damit Laststrom nicht durch die Messmasse fließt.
3. VBUS-Fühlerleitungen getrennt direkt an den Akkupol.
4. Ohne Akku einschalten, Step-Down auf 5,0 V stellen, flashen, unter Einstellungen → Diagnose alle I²C-Geräte prüfen.
5. Last zuerst mit einer günstigen Zelle bei 0,2 A testen.

> **Sicherheit:** LiPo-Packs nur auf nicht brennbarer Unterlage testen und den Prüfstand bei hohen Strömen nicht unbeaufsichtigt lassen, auch wenn die Firmware abschaltet.
