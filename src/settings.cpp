#include "settings.h"
#include <LittleFS.h>
#include "config.h"

Settings settings;

String addrToHex(const uint8_t* a) {
  char b[17];
  for (int i = 0; i < 8; i++) sprintf(b + i * 2, "%02X", a[i]);
  return String(b);
}

bool hexToAddr(const char* s, uint8_t* a) {
  if (!s || strlen(s) != 16) return false;
  for (int i = 0; i < 8; i++) {
    char h[3] = {s[i * 2], s[i * 2 + 1], 0};
    a[i] = uint8_t(strtoul(h, nullptr, 16));
  }
  return true;
}

#ifdef CELLDOC_MINI
static const char* ROLE_KEYS[SENSOR_ROLES] = {"cell1", "cell2", "cell3", "cell4", "sink"};
#else
static const char* ROLE_KEYS[SENSOR_ROLES] = {"cell", "pack", "sink"};
#endif

void Settings::toJson(JsonObject o, bool withSecrets) const {
  o["wifiSsid"] = wifiSsid;
  if (withSecrets) o["wifiPass"] = wifiPass;
  o["chargeWarnC"] = chargeWarnC;
  o["chargeStopC"] = chargeStopC;
  o["dischargeWarnC"] = dischargeWarnC;
  o["dischargeStopC"] = dischargeStopC;
  o["sinkWarnC"] = sinkWarnC;
  o["sinkStopC"] = sinkStopC;
  JsonArray vg = o["vGain"].to<JsonArray>();
  JsonArray ig = o["iGain"].to<JsonArray>();
  JsonArray io = o["iOffset"].to<JsonArray>();
  for (int i = 0; i < 4; i++) {
    vg.add(vGain[i]);
    ig.add(iGain[i]);
    io.add(iOffset[i]);
  }
  JsonArray bg = o["balGain"].to<JsonArray>();
  for (int i = 0; i < 6; i++) bg.add(balGain[i]);
  JsonObject sn = o["sensors"].to<JsonObject>();
  for (int r = 0; r < SENSOR_ROLES; r++)
    sn[ROLE_KEYS[r]] = sensorSet[r] ? addrToHex(sensorAddr[r]) : String("");
  o["restMin"] = restMin;
  o["riInOhm"] = riInOhm;
  o["buzzer"] = buzzer;
  o["nextCellNo"] = nextCellNo;
  o["nextResultId"] = nextResultId;
  o["cellPrefix"] = cellPrefix;
  o["mqttOn"] = mqttOn;
  o["mqttHost"] = mqttHost;
  o["mqttPort"] = mqttPort;
  o["mqttUser"] = mqttUser;
  if (withSecrets) o["mqttPass"] = mqttPass;
  else o["mqttPassSet"] = mqttPass.length() > 0;
  o["mqttBase"] = mqttBase;
  o["hostname"] = hostname;
  o["peers"] = peers;
  o["chargeSlots"] = chargeSlots;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void Settings::fromJson(JsonObjectConst o) {
  if (o["wifiSsid"].is<const char*>()) wifiSsid = o["wifiSsid"].as<const char*>();
  if (o["wifiPass"].is<const char*>()) wifiPass = o["wifiPass"].as<const char*>();
  chargeWarnC = clampf(o["chargeWarnC"] | chargeWarnC, 20, 60);
  chargeStopC = clampf(o["chargeStopC"] | chargeStopC, chargeWarnC + 1, 60);
  dischargeWarnC = clampf(o["dischargeWarnC"] | dischargeWarnC, 20, 70);
  dischargeStopC = clampf(o["dischargeStopC"] | dischargeStopC, dischargeWarnC + 1, 70);
  sinkWarnC = clampf(o["sinkWarnC"] | sinkWarnC, 30, cfg::SINK_STOP_C - 1);
  sinkStopC = clampf(o["sinkStopC"] | sinkStopC, sinkWarnC + 1, cfg::SINK_STOP_C);
  for (int i = 0; i < 4; i++) {
    vGain[i] = clampf(o["vGain"][i] | vGain[i], 0.9f, 1.1f);
    iGain[i] = clampf(o["iGain"][i] | iGain[i], 0.8f, 1.2f);
    iOffset[i] = clampf(o["iOffset"][i] | iOffset[i], -0.1f, 0.1f);
  }
  for (int i = 0; i < 6; i++) balGain[i] = clampf(o["balGain"][i] | balGain[i], 0.9f, 1.1f);
  JsonObjectConst sn = o["sensors"];
  if (!sn.isNull()) {
    for (int r = 0; r < SENSOR_ROLES; r++) {
      const char* h = sn[ROLE_KEYS[r]] | "";
      sensorSet[r] = hexToAddr(h, sensorAddr[r]);
    }
  }
  restMin = uint16_t(clampf(o["restMin"] | restMin, 0, 240));
  riInOhm = o["riInOhm"] | riInOhm;
  buzzer = o["buzzer"] | buzzer;
  // Ältere Firmware hatte nur einen Zähler für Ergebnisse
  if (o["nextResultId"].is<uint32_t>()) nextResultId = o["nextResultId"];
  else if (o["nextCellNo"].is<uint32_t>()) nextResultId = max(nextResultId, o["nextCellNo"].as<uint32_t>());
  nextCellNo = o["nextCellNo"] | nextCellNo;
  if (o["cellPrefix"].is<const char*>()) {
    String p = o["cellPrefix"].as<const char*>();
    p.trim();
    p.toUpperCase();
    String clean;
    for (char c : p)
      if (isalnum((unsigned char)c) && clean.length() < 6) clean += c;
    if (clean.length()) cellPrefix = clean;
  }
  mqttOn = o["mqttOn"] | mqttOn;
  if (o["mqttHost"].is<const char*>()) mqttHost = o["mqttHost"].as<const char*>();
  mqttPort = uint16_t(clampf(o["mqttPort"] | mqttPort, 1, 65535));
  if (o["mqttUser"].is<const char*>()) mqttUser = o["mqttUser"].as<const char*>();
  if (o["mqttPass"].is<const char*>()) mqttPass = o["mqttPass"].as<const char*>();
  if (o["mqttBase"].is<const char*>()) {
    String b = o["mqttBase"].as<const char*>();
    b.trim();
    if (b.length() && b.indexOf('#') < 0 && b.indexOf('+') < 0) mqttBase = b;
  }
  if (o["hostname"].is<const char*>()) {
    String h = o["hostname"].as<const char*>(), clean;
    h.toLowerCase();
    for (char c : h)
      if ((isalnum((unsigned char)c) || c == '-') && clean.length() < 24) clean += c;
    if (clean.length()) hostname = clean;
  }
  chargeSlots = uint8_t(clampf(o["chargeSlots"] | chargeSlots, 0, 4));
  if (o["peers"].is<const char*>()) {
    String p = o["peers"].as<const char*>(), clean;
    for (char c : p)
      if ((isalnum((unsigned char)c) || c == '-' || c == '.' || c == ',' || c == ':') && clean.length() < 160) clean += c;
    peers = clean;
  }
}

bool Settings::load() {
  File f = LittleFS.open("/settings.json", "r");
  if (!f) return false;
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, f);
  f.close();
  if (e) return false;
  fromJson(doc.as<JsonObjectConst>());
  return true;
}

bool Settings::save() const {
  JsonDocument doc;
  toJson(doc.to<JsonObject>(), true);
  File f = LittleFS.open("/settings.json", "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  return true;
}
