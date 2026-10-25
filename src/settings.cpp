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

static const char* ROLE_KEYS[SENSOR_ROLES] = {"cell", "pack", "sink"};

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
  for (int i = 0; i < 2; i++) {
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
  for (int i = 0; i < 2; i++) {
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
  nextCellNo = o["nextCellNo"] | nextCellNo;
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
