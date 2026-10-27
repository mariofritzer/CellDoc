// CellDoc Nano – Hardware: ein Schacht, ESP32-C3 SuperMini
// Messung: INA226 (Shunt 10 mΩ). Last: fester Widerstand (3 Ω) über AO3400, Strom per 10-kHz-PWM
// geregelt (der INA226 mittelt über viele Perioden). Lader: TP4056, Versorgung über AO3401 geschaltet.
#ifdef CELLDOC_NANO
#include <DallasTemperature.h>
#include <OneWire.h>
#include <Wire.h>
#include "drivers.h"
#include "hal_mini.h"
#include "settings.h"
#include "web.h"
#include "web_device.h"

using namespace mcfg;

HalMini hal;

static INA226 ina;
static OneWire oneWire(PIN_ONEWIRE);
static DallasTemperature dallas(&oneWire);
static constexpr uint8_t LOAD_CH = 0;
static constexpr uint8_t PWM_BITS = 10;
static constexpr float PWM_MAX = (1 << PWM_BITS) - 1;
static float duty = 0;  // 0..1

static void writeDuty(float d) {
  duty = d < 0 ? 0 : (d > 1 ? 1 : d);
  ledcWrite(LOAD_CH, uint32_t(duty * PWM_MAX + 0.5f));
}

void HalMini::begin() {
  // Sicherer Zustand zuerst
  pinMode(PIN_LOAD_PWM, OUTPUT);
  digitalWrite(PIN_LOAD_PWM, LOW);
  pinMode(PIN_CHG[0], OUTPUT);
  digitalWrite(PIN_CHG[0], LOW);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);
  pinMode(PIN_BTN_OK, INPUT_PULLUP);
  ledcSetup(LOAD_CH, LOAD_PWM_HZ, PWM_BITS);
  ledcAttachPin(PIN_LOAD_PWM, LOAD_CH);
  writeDuty(0);

  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  ina.begin(ADDR_INA[0]);
  dacOk_ = true;  // kein DAC

  dallas.begin();
  dallas.setResolution(11);
  dallas.setWaitForConversion(false);
  rescanTempSensors();
}

// Blaue LED: aus mit kurzem Blitz = bereit, langsam blinkend = lädt, an = entlädt,
// Doppelblitz = fertig, schnell blinkend = Fehler, zwei kurze Blitze im Sekundentakt = Hotspot
static void led(uint32_t now) {
  bool on;
  uint32_t ph;
  switch (device::ledState()) {
    case 1: on = (now / 500) % 2; break;
    case 2: on = true; break;
    case 3: ph = now % 2000; on = ph < 100 || (ph > 250 && ph < 350); break;
    case 4: on = (now / 120) % 2; break;
    default:
      if (web::apMode()) { ph = now % 1000; on = ph < 60 || (ph > 200 && ph < 260); }
      else on = now % 3000 < 40;
  }
  digitalWrite(PIN_LED, on ? LOW : HIGH);
}

void HalMini::update(uint32_t now) {
  if (now - lastIna_ >= INA_PERIOD_MS) readIna(now);
  if (now - lastTemp_ >= TEMP_PERIOD_MS) readTemps(now);
  control(now);
  buttons(now);
  led(now);

  // Lader im Takt (Drosselung bei warmer Zelle)
  bool on = chg_[0];
  if (on && chgDuty_[0] < 0.99f) on = (now % CHG_THROTTLE_PERIOD_MS) < uint32_t(chgDuty_[0] * CHG_THROTTLE_PERIOD_MS);
  digitalWrite(PIN_CHG[0], on ? HIGH : LOW);

  if (alarmBeeps_ && now >= alarmNext_) {
    beep(150);
    alarmBeeps_--;
    alarmNext_ = now + 300;
  }
  digitalWrite(PIN_BUZZER, (settings.buzzer && now < beepUntil_) ? HIGH : LOW);
}

void HalMini::readIna(uint32_t now) {
  uint32_t us = micros();
  float dt = lastInaMicros_ ? (us - lastInaMicros_) * 1e-6f : 0;
  if (dt > 1.0f) dt = 0;
  lastInaMicros_ = us;
  lastIna_ = now;
  float sh, bus;
  SlotMeas& m = slot_[0];
  if (!ina.read(sh, bus)) {
    m.ok = false;
    return;
  }
  m.ok = true;
  m.v = bus * settings.vGain[0];
  m.i = -(sh / SHUNT_OHM) * settings.iGain[0] - settings.iOffset[0];
  m.p = m.v * m.i;
  m.chargeAs += m.i * dt;
  m.energyWs += m.p * dt;
  m.seq++;
}

// Robust gegen Aussetzer billiger DS18B20 (wie bei der Mini)
static float lastT[SENSOR_ROLES];
static uint8_t badT[SENSOR_ROLES];

static float readRole(int role) {
  if (!settings.sensorSet[role]) return NAN;
  float t = dallas.getTempC(settings.sensorAddr[role]);
  float last = badT[role] < 3 ? lastT[role] : NAN;
  bool bad = t <= DEVICE_DISCONNECTED_C + 1;
  if (!bad && !isnan(last) && last != 0) {
    if (t == 85.0f && fabsf(last - 85.0f) > 10) bad = true;
    else if (fabsf(t - last) > 15 && badT[role] < 2) bad = true;
  }
  if (bad) {
    if (badT[role] < 255) badT[role]++;
    return badT[role] < 3 ? last : NAN;
  }
  badT[role] = 0;
  lastT[role] = t;
  return t;
}

void HalMini::readTemps(uint32_t now) {
  lastTemp_ = now;
  if (tempRequested_) {
    slot_[0].tCell = readRole(SENSOR_CELL);
    tSink_ = readRole(SENSOR_SINK);
  }
  dallas.requestTemperatures();
  tempRequested_ = true;
}

void HalMini::rescanTempSensors() {
  dallas.begin();
  bool changed = false;
  for (int i = 0; i < dallas.getDeviceCount(); i++) {
    uint8_t a[8];
    if (!dallas.getAddress(a, i)) continue;
    bool known = false;
    for (int r = 0; r < SENSOR_ROLES; r++)
      if (settings.sensorSet[r] && memcmp(settings.sensorAddr[r], a, 8) == 0) known = true;
    if (known) continue;
    // Nano: erster Fühler = Zelle, zweiter (optional) = Lastwiderstand
    for (int r : {int(SENSOR_CELL), int(SENSOR_SINK)}) {
      if (!settings.sensorSet[r]) {
        memcpy(settings.sensorAddr[r], a, 8);
        settings.sensorSet[r] = true;
        changed = true;
        break;
      }
    }
  }
  if (changed) settings.save();
}

int HalMini::tempSensors(SensorInfo* out, int max) {
  int n = 0;
  for (int i = 0; i < dallas.getDeviceCount() && n < max; i++) {
    SensorInfo& s = out[n];
    if (!dallas.getAddress(s.addr, i)) continue;
    s.t = dallas.getTempC(s.addr);
    s.role = -1;
    for (int r = 0; r < SENSOR_ROLES; r++)
      if (settings.sensorSet[r] && memcmp(settings.sensorAddr[r], s.addr, 8) == 0) s.role = r;
    n++;
  }
  return n;
}

void HalMini::resetCounters(uint8_t s) {
  slot_[s].chargeAs = 0;
  slot_[s].energyWs = 0;
}

// ---------------------------------------------------------------------------
// Last: Vorsteuerung aus U/R, danach Regelung auf den gemittelten Strom

void HalMini::setLoad(uint8_t s, float amps) {
  if (s != 0) return;
  if (amps < 0) amps = 0;
  if (amps > MAX_CURRENT_A) amps = MAX_CURRENT_A;
  if (amps == 0) {
    saturated_[0] = false;
    writeDuty(0);
  } else if (target_[0] == 0 || amps < target_[0] * 0.6f) {
    float v = slot_[0].v > 2.0f ? slot_[0].v : 3.7f;
    writeDuty(amps * LOAD_OHM / v * 0.95f);
  }
  target_[0] = amps;
}

void HalMini::control(uint32_t now) {
  (void)now;
  if (target_[0] <= 0) return;
  const SlotMeas& m = slot_[0];
  if (m.seq == lastSeq_[0]) return;
  lastSeq_[0] = m.seq;
  float tgt = target_[0];
  if (m.v * tgt > MAX_TOTAL_W) tgt = MAX_TOTAL_W / m.v;
  float err = tgt - (-m.i);
  float v = m.v > 2.0f ? m.v : 3.7f;
  float d = duty + err * LOAD_OHM / v * 0.35f;
  saturated_[0] = d >= 1 && err > 0.05f;
  writeDuty(d);
}

void HalMini::fan(uint32_t) {}

// ---------------------------------------------------------------------------
// Lader

void HalMini::setCharger(uint8_t s, bool on) {
  if (s != 0) return;
  chg_[0] = on;
  grant_[0] = on;
  chgDuty_[0] = 1;
  if (!on) digitalWrite(PIN_CHG[0], LOW);
}

void HalMini::grantChargers() {}

void HalMini::setChargerDuty(uint8_t s, float d) { chgDuty_[s] = d < 0.1f ? 0.1f : (d > 1 ? 1 : d); }

void HalMini::kill(uint8_t s) {
  setCharger(s, false);
  setLoad(s, 0);
}

void HalMini::killAll() {
  writeDuty(0);
  target_[0] = 0;
  chg_[0] = grant_[0] = false;
  digitalWrite(PIN_CHG[0], LOW);
}

void HalMini::beep(uint16_t ms) { beepUntil_ = millis() + ms; }

void HalMini::alarm() {
  alarmBeeps_ = 6;
  alarmNext_ = millis();
}

// ---------------------------------------------------------------------------
// BOOT-Taste: kurz = OK, lang = stoppen

void HalMini::buttons(uint32_t now) {
  uint8_t raw = digitalRead(PIN_BTN_OK) == LOW ? 2 : 0;
  if (raw != btnState_) {
    btnState_ = raw;
    btnChanged_ = now;
  }
  if (now - btnChanged_ < 30 || raw == btnStable_) {
    if ((btnStable_ & 2) && !okLongSent_ && now - okDown_ > 1200) {
      okLongSent_ = true;
      pending_ = BtnEvent::OkLong;
    }
    return;
  }
  uint8_t pressed = raw & ~btnStable_;
  uint8_t released = btnStable_ & ~raw;
  btnStable_ = raw;
  if (pressed & 2) {
    okDown_ = now;
    okLongSent_ = false;
  }
  if ((released & 2) && !okLongSent_) pending_ = BtnEvent::Ok;
}

BtnEvent HalMini::button() {
  BtnEvent e = pending_;
  pending_ = BtnEvent::None;
  return e;
}

#endif  // CELLDOC_NANO
