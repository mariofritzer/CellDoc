# CellDoc

**Diagnose für Akkuzellen.** Ein ESP32-Prüfstand, der Akkus lädt, mit konstantem Strom entlädt, dabei Kapazität, Energie und Innenwiderstand misst und am Ende eine **Lebenszahl von 0 bis 100** ausgibt. Bedient wird alles über eine Weboberfläche im Browser, am Handy wie am PC.

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/img/live-desktop-dark.png">
  <img src="docs/img/live-desktop-light.png" alt="CellDoc Weboberfläche: laufender Kapazitätstest mit Akkuanzeige, Temperaturen, Testablauf und Entladekurve">
</picture>

> **Ausprobieren ohne Hardware:** [Demo im Browser](https://mariofritzer.github.io/CellDoc/) (Beispielwerte)

## Was CellDoc kann

- **Kapazitätstest** in einem Durchgang: voll laden → ruhen → mit Konstantstrom entladen → erholen → wieder voll oder auf Lagerspannung laden
- **Innenwiderstand (DC)** per Stromsprung, mehrmals während der Entladung, ausgewertet bei 100 % und 50 % Ladung
- **Lebenszahl** aus Kapazität und Innenwiderstand, mit Zelltyp-Datenbank und Schätzmodus für unbekannte Zellen
- **Zwei Anschlüsse:** A für Einzelzellen (laden und entladen), B für Packs bis 6S mit Balancer-Messung jeder Einzelzelle
- **Akkutypen:** Li-Ion, LiPo, Li-HV, LiFePO4, LTO, NiMH/NiCd und Blei
- **Bis 30 W / 8 A** Entladeleistung mit zwei Lastkanälen und Lüfter
- **Sicherheit:** drei Temperaturfühler mit Drosselung und Abschaltung, Über- und Unterspannung pro Zelle, Überstrom, Lüfter- und Sensorausfall, Not-Aus-Taste
- **Ergebnisse** mit Entladekurve, Ri-Verlauf und Vergleich zum ersten Test derselben Zelle
- **Handbuch** direkt im Gerät: Akkutypen, Bauformen, Stecker, Messwerte, Sicherheit, Lagerung und Entsorgung
- **Firmware-Updates** über den Browser, WLAN-Einrichtung über einen eigenen Hotspot

## Bilder

| Live | Ergebnisse | Ergebnis im Detail |
|:---:|:---:|:---:|
| <img src="docs/img/phone-live.png" width="240" alt="Live-Ansicht am Handy"> | <img src="docs/img/phone-results.png" width="240" alt="Liste der geprüften Akkus"> | <img src="docs/img/phone-result-dark.png" width="240" alt="Ergebnis mit Lebenszahl im dunklen Design"> |

| Test starten | Handbuch | Einstellungen |
|:---:|:---:|:---:|
| <img src="docs/img/phone-start.png" width="240" alt="Startdialog für einen Kapazitätstest"> | <img src="docs/img/phone-manual.png" width="240" alt="Handbuch mit Akkutypen"> | <img src="docs/img/phone-settings.png" width="240" alt="Einstellungen"> |

*Fotos vom fertigen Aufbau folgen.*

## Hardware

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/img/blockschaltbild-dark.png">
  <img src="docs/img/blockschaltbild-light.png" alt="Blockschaltbild">
</picture>

ESP32 DevKit, 2× INA226 (Strom/Spannung), MCP4725 + LM358 + 2× IRFP250N (Konstantstromlast), TP5000 (Lader 4,2/3,6 V), 2× ADS1115 (Balancer), 3× DS18B20, zwei Relais, CPU-Kühler mit 4-Pin-Lüfter, 12-V-Netzteil.

Schaltung, Pinbelegung und Einkaufsliste: **[docs/hardware.md](docs/hardware.md)**

## Firmware installieren

### Erstinstallation per USB

1. Unter [Releases](../../releases) die Datei **`celldoc-factory.bin`** laden (`latest` = neuester Stand).
2. ESP32 per USB anschließen und im Chrome oder Edge den [ESP Web Flasher von Espressif](https://espressif.github.io/esptool-js/) öffnen.
3. *Connect*, Datei an Adresse **0x0** eintragen, *Program*.

Alternativ mit PlatformIO: `pio run -t upload`

### Updates

Unter **Einstellungen → Firmware-Update** die Datei **`celldoc-firmware.bin`** hochladen. Einstellungen und Ergebnisse bleiben erhalten.

## Erste Schritte

1. Ohne gespeichertes WLAN öffnet CellDoc den Hotspot **CellDoc-XXXX** mit dem Passwort **`celldoc1`**. Verbinden, die Seite öffnet sich automatisch, sonst `http://192.168.4.1` aufrufen.
2. **Einstellungen → WLAN**: Heimnetz wählen und speichern. Danach ist CellDoc unter **http://celldoc.local** oder seiner IP erreichbar.
3. **Einstellungen → Temperaturfühler**: prüfen, dass Zelle, Pack und Kühlkörper richtig zugeordnet sind.
4. **Einstellungen → Kalibrierung**: Spannung und Strom einmal mit dem Multimeter abgleichen.
5. **Live → Starten**: Zelltyp wählen, Strom einstellen, los.

## Die Lebenszahl

| Zelltyp | Gewichtung | Kapazität | Innenwiderstand |
|---|---|---|---|
| bekannt | 70 % Kapazität, 30 % Ri | 100 % Nennwert = 100 Punkte, 60 % = 0 | Neuwert = 100 Punkte, 2,5-facher Neuwert = 0 |
| unbekannt | 50 / 50 | gegen typischen Wert der Chemie | feste Grenzen pro Zelle |

Bei unbekannten Zellen zeigt CellDoc einen Bereich (z. B. „≈ 55–70 %“). Die hinterlegten Ri-Neuwerte sind Richtwerte für den DC-Innenwiderstand und lassen sich beim Start überschreiben. Die Berechnung liegt in [`src/soh.h`](src/soh.h) und wird mit [`test/test_soh.cpp`](test/test_soh.cpp) bei jedem Build geprüft.

## Selbst bauen

```bash
pip install platformio
pio run -e esp32dev            # baut Firmware und bettet web/index.html ein
pio run -e esp32dev -t upload  # flasht per USB
pio device monitor             # serielle Ausgabe, 115200 Baud
```

Die Weboberfläche ist eine einzelne Datei: [`web/index.html`](web/index.html). Direkt im Browser geöffnet startet sie im Demo-Modus, praktisch zum Entwickeln ohne Gerät. Beim Bauen packt `tools/embed_web.py` sie komprimiert in die Firmware.

| Pfad | Inhalt |
|---|---|
| `src/config.h` | Pins und harte Grenzwerte |
| `src/hal.*` | Messung, Last-Regelung, Lader, Relais, Lüfter |
| `src/tester.*` | Testablauf, Ri-Messung, Sicherheitsüberwachung |
| `src/profiles.h` | Akkuchemien und Zelltypen |
| `src/soh.h` | Lebenszahl |
| `src/web.*` | WLAN, REST-API, Firmware-Update |
| `web/index.html` | Weboberfläche inkl. Handbuch |

Jeder Push auf `main` baut die Firmware über GitHub Actions und aktualisiert die Vorabversion `latest`. Ein Tag `v1.2.3` erzeugt ein normales Release.

## Status

Firmware und Weboberfläche sind vollständig, **auf echter Hardware aber noch nicht erprobt**. Vor dem ersten Test mit wertvollen Akkus bitte mit einer günstigen Zelle und kleinem Strom anfangen und die Werte mit einem Multimeter vergleichen.

## Sicherheit

Lithium-Akkus können bei Fehlern brennen. Tests mit hohen Strömen und LiPo-Packs nur auf nicht brennbarer Unterlage und nicht unbeaufsichtigt durchführen. Die Schutzfunktionen der Firmware ersetzen keine Aufsicht. Nachbau und Nutzung auf eigene Verantwortung.

## Lizenz

[MIT](LICENSE) © 2026 Mario Fritzer

Die Idee geht auf das *ESP32 Smart Battery Dashboard* von LifeTronix zurück. Hardwarekonzept, Firmware und Oberfläche von CellDoc sind eigenständig neu entwickelt.
