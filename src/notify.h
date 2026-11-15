// Benachrichtigungen aufs Handy über ntfy und/oder Telegram (läuft im Hintergrund)
#pragma once
#include <Arduino.h>

namespace notify {
// kind: 'd' Test fertig, 'e' Abbruch/Fehler, 'c' Laden fertig, 't' Test-Nachricht (immer)
void send(char kind, const String& title, const String& text);
bool configured();
}  // namespace notify
