// CellDoc – WLAN und Weboberfläche (gemeinsam für CellDoc und Mini)
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebServer.h>

namespace web {
void begin();
void update();
bool apMode();
void restartSoon(uint32_t ms);  // Neustart nach ms (Antworten können noch rausgehen)

// Hilfen für die gerätespezifischen Routen
WebServer& http();
void sendJson(JsonDocument& d, int code = 200);
void sendError(const String& msg, int code = 400);
void sendOk();
bool readBody(JsonDocument& d);
void addCommon(JsonObject o);  // Version, Laufzeit, WLAN, Uhrzeit
}  // namespace web

namespace webdata {
void routes();  // Bestand, Selbstentladung, Uhrzeit, Datensicherung (web_data.cpp)
}
