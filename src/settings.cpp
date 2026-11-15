#include "settings.h"
#include <LittleFS.h>
#include <time.h>
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
  JsonArray rl = o["rLead"].to<JsonArray>();
  for (int i = 0; i < 4; i++) {
    vg.add(vGain[i]);
    ig.add(iGain[i]);
    io.add(iOffset[i]);
    rl.add(rLead[i]);
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
  o["tI"] = tI; o["tCut"] = tCut; o["tCyc"] = tCyc; o["tPre"] = tPre; o["tAfter"] = tAfter;
  o["tStorPct"] = tStorPct; o["tRestAfter"] = tRestAfter;
  o["chgA"] = chgA; o["fullA"] = fullA; o["fullV"] = fullV; o["chgMaxH"] = chgMaxH;
  o["maxV"] = maxV; o["ocF"] = ocF; o["noTempA"] = noTempA;
  o["tz"] = tz; o["ntp"] = ntp;
  o["bzKey"] = bzKey; o["bzDone"] = bzDone; o["bzErr"] = bzErr; o["bzBoot"] = bzBoot; o["bzScan"] = bzScan;
  o["led"] = led; o["oledBr"] = oledBr; o["oledOff"] = oledOff;
  o["nightOn"] = nightOn; o["nightFrom"] = nightFrom; o["nightTo"] = nightTo;
  o["nightBz"] = nightBz; o["nightLed"] = nightLed; o["nightOled"] = nightOled;
  o["ntfy"] = ntfy; o["tgChat"] = tgChat; o["nDone"] = nDone; o["nErr"] = nErr; o["nChg"] = nChg;
  if (withSecrets) o["tgTok"] = tgTok;
  else o["tgTokSet"] = tgTok.length() > 0;
  o["ip"] = ip; o["gw"] = gw; o["mask"] = mask; o["dns"] = dns;
  o["ssid2"] = ssid2; o["apName"] = apName;
  if (withSecrets) { o["pass2"] = pass2; o["apPass"] = apPass; }
  else { o["pass2Set"] = pass2.length() > 0; o["apPassSet"] = apPass.length() > 0; }
  o["scanMode"] = scanMode; o["verbose"] = verbose;
  JsonDocument u;
  if (!deserializeJson(u, ui) && u.is<JsonObject>()) o["ui"] = u;
  else o["ui"].to<JsonObject>();
}

bool Settings::night() const {
  if (!nightOn) return false;
  time_t t = time(nullptr);
  if (t < 1700000000) return false;
  struct tm lt;
  localtime_r(&t, &lt);
  int m = lt.tm_hour * 60 + lt.tm_min;
  return nightFrom <= nightTo ? (m >= nightFrom && m < nightTo) : (m >= nightFrom || m < nightTo);
}

bool Settings::sound(char kind) const {
  if (!buzzer) return false;
  if (night() && nightBz && kind != 'e') return false;  // Fehler piepen auch nachts
  switch (kind) {
    case 'k': return bzKey;
    case 'd': return bzDone;
    case 'e': return bzErr;
    case 'b': return bzBoot;
    case 's': return bzScan;
  }
  return true;
}

static void str(JsonObjectConst o, const char* k, String& v, size_t max) {
  if (!o[k].is<const char*>()) return;
  String s = o[k].as<const char*>();
  s.trim();
  if (s.length() <= max) v = s;
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
    iGain[i] = clampf(o["iGain"][i] | iGain[i], 0.2f, 3.0f);
    iOffset[i] = clampf(o["iOffset"][i] | iOffset[i], -0.1f, 0.1f);
    rLead[i] = clampf(o["rLead"][i] | rLead[i], 0.0f, 2.0f);
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
  tI = clampf(o["tI"] | tI, 0, 5);
  tCut = clampf(o["tCut"] | tCut, 2.5f, 3.6f);
  tCyc = uint8_t(clampf(o["tCyc"] | tCyc, 1, 5));
  tPre = o["tPre"] | tPre;
  tAfter = uint8_t(clampf(o["tAfter"] | tAfter, 0, 2));
  tStorPct = uint8_t(clampf(o["tStorPct"] | tStorPct, 20, 80));
  tRestAfter = uint8_t(clampf(o["tRestAfter"] | tRestAfter, 1, 60));
  chgA = clampf(o["chgA"] | chgA, 0.1f, 3.0f);
  fullA = clampf(o["fullA"] | fullA, 0.02f, 0.5f);
  fullV = clampf(o["fullV"] | fullV, 4.0f, 4.2f);
  chgMaxH = uint8_t(clampf(o["chgMaxH"] | chgMaxH, 0, 24));
  maxV = clampf(o["maxV"] | maxV, 4.2f, 4.3f);
  ocF = clampf(o["ocF"] | ocF, 1.1f, 2.0f);
  noTempA = clampf(o["noTempA"] | noTempA, 0.2f, 2.0f);
  str(o, "tz", tz, 60);
  str(o, "ntp", ntp, 60);
  bzKey = o["bzKey"] | bzKey; bzDone = o["bzDone"] | bzDone; bzErr = o["bzErr"] | bzErr;
  bzBoot = o["bzBoot"] | bzBoot; bzScan = o["bzScan"] | bzScan;
  led = o["led"] | led;
  oledBr = uint8_t(clampf(o["oledBr"] | oledBr, 0, 255));
  oledOff = uint16_t(clampf(o["oledOff"] | oledOff, 0, 1440));
  nightOn = o["nightOn"] | nightOn;
  nightFrom = uint16_t(clampf(o["nightFrom"] | nightFrom, 0, 1439));
  nightTo = uint16_t(clampf(o["nightTo"] | nightTo, 0, 1439));
  nightBz = o["nightBz"] | nightBz; nightLed = o["nightLed"] | nightLed; nightOled = o["nightOled"] | nightOled;
  str(o, "ntfy", ntfy, 120);
  str(o, "tgTok", tgTok, 80);
  str(o, "tgChat", tgChat, 30);
  nDone = o["nDone"] | nDone; nErr = o["nErr"] | nErr; nChg = o["nChg"] | nChg;
  str(o, "ip", ip, 15); str(o, "gw", gw, 15); str(o, "mask", mask, 15); str(o, "dns", dns, 15);
  str(o, "ssid2", ssid2, 32); str(o, "pass2", pass2, 63);
  str(o, "apName", apName, 32);
  if (o["apPass"].is<const char*>()) {
    String p = o["apPass"].as<const char*>();
    if (p.length() == 0 || (p.length() >= 8 && p.length() <= 63)) apPass = p;
  }
  scanMode = uint8_t(clampf(o["scanMode"] | scanMode, 0, 2));
  verbose = o["verbose"] | verbose;
  if (o["ui"].is<JsonObjectConst>()) {
    String u;
    serializeJson(o["ui"], u);
    if (u.length() <= 8000) ui = u;
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
