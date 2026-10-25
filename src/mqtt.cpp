#include "mqtt.h"
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <vector>
#include "config.h"
#include "settings.h"
#include "web.h"
#include "web_device.h"

namespace mqtt {

static WiFiClient net;
static PubSubClient mq(net);
static String uid, base;
static uint32_t lastTry = 0, lastPub = 0;
static bool wanted = false, announced = false;
static String state = "aus";

const char* status() { return state.c_str(); }

static void makeIds() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char b[20];
  snprintf(b, sizeof(b), "celldoc_%02x%02x%02x", mac[3], mac[4], mac[5]);
  uid = b;
  base = settings.mqttBase + "/" + uid;
}

void reconfigure() {
  if (mq.connected()) mq.disconnect();
  announced = false;
  lastTry = 0;
  state = settings.mqttOn ? "verbindet" : "aus";
}

// "phase:<pfad>" wird zu einer Übersetzung des Status ins Deutsche
static String tplFor(const String& t) {
  if (!t.startsWith("phase:")) return t;
  String p = t.substring(6);
  return "{{ {'idle':'Bereit','precharge':'Test: Laden','rest':'Test: Ruhen','discharge':'Test: Entladen',"
         "'restafter':'Test: Erholen','recharge':'Test: Wieder laden','done':'Fertig','error':'Gestoppt',"
         "'charge':'Laden','manualdischarge':'Entladen'}.get(" + p + ", " + p + ") }}";
}

static void announce() {
  std::vector<device::MqttEntity> list;
  device::mqttEntities(list);
  String avail = base + "/status", st = base + "/state";
  for (auto& e : list) {
    JsonDocument d;
    d["name"] = e.name;
    d["uniq_id"] = uid + "_" + e.obj;
    d["obj_id"] = uid + "_" + e.obj;
    d["avty_t"] = avail;
    if (e.button) {
      d["cmd_t"] = base + "/cmd";
      d["pl_prs"] = e.tpl;
    } else {
      d["stat_t"] = st;
      d["val_tpl"] = tplFor(e.tpl);
      if (e.unit.length()) {
        d["unit_of_meas"] = e.unit;
        d["stat_cla"] = "measurement";
      }
      if (e.dclass.length()) d["dev_cla"] = e.dclass;
    }
    JsonObject dev = d["dev"].to<JsonObject>();
    dev["ids"].add(uid);
    dev["name"] = device::model();
    dev["mdl"] = device::model();
    dev["mf"] = "CellDoc";
    dev["sw"] = CELLDOC_VERSION;
    dev["cu"] = "http://" + WiFi.localIP().toString();
    String topic = String("homeassistant/") + (e.button ? "button/" : "sensor/") + uid + "/" + e.obj + "/config";
    String payload;
    serializeJson(d, payload);
    mq.publish(topic.c_str(), payload.c_str(), true);
    mq.loop();
  }
  announced = true;
}

static void onMessage(char* topic, byte* payload, unsigned int len) {
  String cmd;
  for (unsigned int k = 0; k < len && k < 32; k++) cmd += char(payload[k]);
  cmd.trim();
  device::mqttCommand(cmd);
}

static void publishState() {
  JsonDocument d;
  JsonObject o = d.to<JsonObject>();
  device::fillState(o);
  String payload;
  serializeJson(d, payload);
  mq.publish((base + "/state").c_str(), payload.c_str());
}

static bool connectNow() {
  makeIds();
  mq.setServer(settings.mqttHost.c_str(), settings.mqttPort);
  mq.setBufferSize(6144);
  mq.setCallback(onMessage);
  mq.setSocketTimeout(3);
  // Verbindung mit kurzer Wartezeit selbst aufbauen, damit die Messung nicht lange steht
  if (!net.connect(settings.mqttHost.c_str(), settings.mqttPort, 800)) {
    state = "Fehler: Broker nicht erreichbar";
    return false;
  }
  String avail = base + "/status";
  bool ok = settings.mqttUser.length()
                ? mq.connect(uid.c_str(), settings.mqttUser.c_str(), settings.mqttPass.c_str(), avail.c_str(), 0, true, "offline")
                : mq.connect(uid.c_str(), avail.c_str(), 0, true, "offline");
  if (!ok) {
    state = "Fehler: Anmeldung abgelehnt (" + String(mq.state()) + ")";
    net.stop();
    return false;
  }
  mq.publish(avail.c_str(), "online", true);
  mq.subscribe((base + "/cmd").c_str());
  announced = false;
  state = "verbunden";
  return true;
}

void update() {
  wanted = settings.mqttOn && settings.mqttHost.length() && !web::apMode() && WiFi.status() == WL_CONNECTED;
  if (!wanted) {
    if (mq.connected()) mq.disconnect();
    if (!settings.mqttOn) state = "aus";
    else if (!settings.mqttHost.length()) state = "Fehler: kein Broker eingetragen";
    else if (web::apMode() || WiFi.status() != WL_CONNECTED) state = "wartet auf WLAN";
    return;
  }
  uint32_t now = millis();
  if (!mq.connected()) {
    if (lastTry && now - lastTry < 30000) return;
    lastTry = now;
    if (!connectNow()) return;
  }
  mq.loop();
  if (!announced) announce();
  uint32_t every = device::busy() ? 5000 : 30000;
  if (!lastPub || now - lastPub >= every) {
    lastPub = now;
    publishState();
  }
}

}  // namespace mqtt
