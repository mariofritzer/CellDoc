#include "hal_mini.h"
#include <DallasTemperature.h>
#include <OneWire.h>
#include <Wire.h>
#include "drivers.h"
#include "settings.h"

using namespace mcfg;

HalMini hal;

static INA226 ina[4];
static MCP4728 dac;
static OneWire oneWire(PIN_ONEWIRE);
static DallasTemperature dallas(&oneWire);
static constexpr uint8_t FAN_CH = 0;
static constexpr float AMPS_FULL_SCALE = DAC_VREF / DAC_DIVIDER / LOAD_SENSE_OHM;
static constexpr float CODE_PER_AMP = 4095.0f / AMPS_FULL_SCALE;

void HalMini::begin() {
  // Sicherer Zustand zuerst
  pinMode(PIN_LOAD_EN_N, OUTPUT);
  digitalWrite(PIN_LOAD_EN_N, HIGH);
  for (int s = 0; s < 4; s++) {
    pinMode(PIN_CHG[s], OUTPUT);
    digitalWrite(PIN_CHG[s], LOW);
  }
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  pinMode(PIN_BTN_PREV, INPUT);
  pinMode(PIN_BTN_OK, INPUT);
  pinMode(PIN_BTN_NEXT, INPUT);
  pinMode(PIN_BOOT, INPUT_PULLUP);
  ledcSetup(FAN_CH, 25000, 8);
  ledcAttachPin(PIN_FAN, FAN_CH);
  ledcWrite(FAN_CH, 0);

  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  for (int s = 0; s < NSLOTS; s++) ina[s].begin(ADDR_INA[s]);
  dacOk_ = dac.begin(ADDR_DAC);

  dallas.begin();
  dallas.setResolution(11);
  dallas.setWaitForConversion(false);
  rescanTempSensors();
}

void HalMini::update(uint32_t now) {
  if (now - lastIna_ >= INA_PERIOD_MS) readIna(now);
  if (now - lastTemp_ >= TEMP_PERIOD_MS) readTemps(now);
  control(now);
  if (now - lastFan_ >= 1000) fan(now);
  buttons(now);

  // Lader im Takt (Drosselung), höchstens settings.chargeSlots gleichzeitig
  grantChargers();
  for (int s = 0; s < NSLOTS; s++) {
    bool on = chg_[s] && grant_[s];
    if (on && chgDuty_[s] < 0.99f) on = (now % CHG_THROTTLE_PERIOD_MS) < uint32_t(chgDuty_[s] * CHG_THROTTLE_PERIOD_MS);
    digitalWrite(PIN_CHG[s], on ? HIGH : LOW);
  }

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
  for (int s = 0; s < NSLOTS; s++) {
    float sh, bus;
    SlotMeas& m = slot_[s];
    if (!ina[s].read(sh, bus)) {
      m.ok = false;
      continue;
    }
    m.ok = true;
    m.v = bus * settings.vGain[s];
    m.i = -(sh / SHUNT_OHM) * settings.iGain[s] - settings.iOffset[s];
    m.p = m.v * m.i;
    m.chargeAs += m.i * dt;
    m.energyWs += m.p * dt;
    m.seq++;
  }
}

// Robust gegen Aussetzer billiger DS18B20: einzelne Fehllesungen, den Einschaltwert 85 °C
// und unplausible Sprünge überbrücken, erst nach 3 Fehlern in Folge gilt der Fühler als fehlend
static float lastT[SENSOR_ROLES];
static uint8_t badT[SENSOR_ROLES];

static float readRole(int role) {
  if (!settings.sensorSet[role]) return NAN;
  float t = dallas.getTempC(settings.sensorAddr[role]);
  float last = badT[role] < 3 ? lastT[role] : NAN;
  bool bad = t <= DEVICE_DISCONNECTED_C + 1;
  if (!bad && !isnan(last) && last != 0) {
    if (t == 85.0f && fabsf(last - 85.0f) > 10) bad = true;  // Einschaltwert nach Spannungseinbruch
    else if (fabsf(t - last) > 15 && badT[role] < 2) bad = true;  // Sprung um mehr als 15 K: erst nach 3 Lesungen glauben
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
    for (int s = 0; s < NSLOTS; s++) slot_[s].tCell = readRole(s);
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
    // Neue Fühler: zuerst Kühlkörper, dann Schacht 1, 2, ...
    int order[5] = {SENSOR_SINK, 0, 1, 2, 3};
    for (int r : order) {
      if (r != SENSOR_SINK && r >= NSLOTS) continue;
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
// Last

void HalMini::setLoad(uint8_t s, float amps) {
  if (s >= NSLOTS) return;
  if (amps < 0) amps = 0;
  if (amps > MAX_CURRENT_A) amps = MAX_CURRENT_A;
  if (amps > 0 && target_[s] == 0) code_[s] = amps * CODE_PER_AMP * 0.9f;  // Vorsteuerung
  if (amps == 0) {
    code_[s] = 0;
    saturated_[s] = false;
  }
  target_[s] = amps;
  dac.set(s, uint16_t(code_[s]));
  dac.flush();
  bool any = false;
  for (int k = 0; k < NSLOTS; k++) any |= target_[k] > 0;
  digitalWrite(PIN_LOAD_EN_N, any ? LOW : HIGH);
}

void HalMini::control(uint32_t now) {
  (void)now;
  bool changed = false;
  // Gesamtleistung begrenzen: gleichmäßig auf aktive Schächte verteilen
  float total = 0;
  for (int s = 0; s < NSLOTS; s++) total += target_[s] * slot_[s].v;
  float scale = total > MAX_TOTAL_W ? MAX_TOTAL_W / total : 1;
  for (int s = 0; s < NSLOTS; s++) {
    if (target_[s] <= 0) continue;
    const SlotMeas& m = slot_[s];
    if (m.seq == lastSeq_[s]) continue;
    lastSeq_[s] = m.seq;
    float tgt = target_[s] * scale;
    float err = tgt - (-m.i);
    float c = code_[s] + err * CODE_PER_AMP * 0.35f;
    if (c < 0) c = 0;
    if (c > 4095) c = 4095;
    saturated_[s] = c >= 4095 && err > 0.05f;
    if (uint16_t(c) != uint16_t(code_[s])) changed = true;
    code_[s] = c;
    dac.set(s, uint16_t(c));
  }
  if (changed) dac.flush();
}

void HalMini::fan(uint32_t now) {
  lastFan_ = now;
  float t = tSink_;
  float duty = 0;
  bool load = false;
  for (int s = 0; s < NSLOTS; s++) load |= target_[s] > 0;
  if (!isnan(t) && t > 35) duty = 30 + (t - 35) * 70.0f / 20.0f;
  if (load && duty < 30) duty = 30;
  if (load && isnan(t)) duty = 100;
  if (duty > 100) duty = 100;
  fanDuty_ = uint8_t(duty);
  ledcWrite(FAN_CH, uint8_t(duty * 2.55f));
}

// ---------------------------------------------------------------------------
// Lader

void HalMini::setCharger(uint8_t s, bool on) {
  if (s >= NSLOTS) return;
  if (on && !chg_[s]) chgSince_[s] = millis();
  chg_[s] = on;
  chgDuty_[s] = 1;
  if (!on) {
    grant_[s] = false;
    digitalWrite(PIN_CHG[s], LOW);
  }
  grantChargers();
}

// Wer zuerst laden wollte, darf zuerst; wer schon lädt, behält seinen Platz
void HalMini::grantChargers() {
  int limit = settings.chargeSlots ? settings.chargeSlots : NSLOTS;
  int used = 0;
  for (int s = 0; s < NSLOTS; s++)
    if (chg_[s] && grant_[s]) used++;
  while (used < limit) {
    int best = -1;
    for (int s = 0; s < NSLOTS; s++)
      if (chg_[s] && !grant_[s] && (best < 0 || chgSince_[s] < chgSince_[best])) best = s;
    if (best < 0) break;
    grant_[best] = true;
    used++;
  }
}

void HalMini::setChargerDuty(uint8_t s, float d) { chgDuty_[s] = d < 0.1f ? 0.1f : (d > 1 ? 1 : d); }

void HalMini::kill(uint8_t s) {
  setCharger(s, false);
  setLoad(s, 0);
}

void HalMini::killAll() {
  digitalWrite(PIN_LOAD_EN_N, HIGH);
  for (int s = 0; s < NSLOTS; s++) {
    chg_[s] = false;
    grant_[s] = false;
    digitalWrite(PIN_CHG[s], LOW);
    target_[s] = 0;
    code_[s] = 0;
    dac.set(s, 0);
  }
  dac.flush();
}

void HalMini::beep(uint16_t ms) { beepUntil_ = millis() + ms; }

void HalMini::alarm() {
  alarmBeeps_ = 6;
  alarmNext_ = millis();
}

// ---------------------------------------------------------------------------
// Taster (aktiv LOW, externe Pull-ups)

void HalMini::buttons(uint32_t now) {
  uint8_t raw = (digitalRead(PIN_BTN_PREV) == LOW ? 1 : 0) | (digitalRead(PIN_BTN_OK) == LOW ? 2 : 0) |
                (digitalRead(PIN_BTN_NEXT) == LOW ? 4 : 0);
  if (raw != btnState_) {
    btnState_ = raw;
    btnChanged_ = now;
  }
  if (now - btnChanged_ < 30 || raw == btnStable_) {
    // OK lange gedrückt?
    if ((btnStable_ & 2) && !okLongSent_ && now - okDown_ > 1200) {
      okLongSent_ = true;
      pending_ = BtnEvent::OkLong;
    }
    return;
  }
  uint8_t pressed = raw & ~btnStable_;
  uint8_t released = btnStable_ & ~raw;
  btnStable_ = raw;
  if (pressed & 1) pending_ = BtnEvent::Prev;
  if (pressed & 4) pending_ = BtnEvent::Next;
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
