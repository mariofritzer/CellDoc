#include "hal.h"
#include <DallasTemperature.h>
#include <OneWire.h>
#include <Wire.h>
#include "config.h"
#include "drivers.h"
#include "settings.h"

Hal hal;

static INA226 inaA, inaB;
static ADS1115 ads1, ads2;
static MCP4725 dac;
static OneWire oneWire(cfg::PIN_ONEWIRE);
static DallasTemperature dallas(&oneWire);

static volatile uint32_t tachPulses = 0;
static void IRAM_ATTR onTach() { tachPulses++; }

static constexpr uint8_t FAN_LEDC_CH = 0;
// Nominaler Zusammenhang DAC-Code -> Gesamtstrom beider Kanäle
static constexpr float AMPS_FULL_SCALE =
    cfg::DAC_VREF / cfg::DAC_DIVIDER / cfg::LOAD_SENSE_OHM * cfg::LOAD_CHANNELS;
static constexpr float CODE_PER_AMP = 4095.0f / AMPS_FULL_SCALE;

void Hal::begin() {
  // Zuerst alles in den sicheren Zustand
  pinMode(cfg::PIN_LOAD_EN_N, OUTPUT);
  digitalWrite(cfg::PIN_LOAD_EN_N, HIGH);  // Last gesperrt
  pinMode(cfg::PIN_RELAY_K1, OUTPUT);
  digitalWrite(cfg::PIN_RELAY_K1, LOW);
  pinMode(cfg::PIN_RELAY_K2, OUTPUT);
  digitalWrite(cfg::PIN_RELAY_K2, LOW);
  pinMode(cfg::PIN_CHG_CS_HI, OUTPUT);
  digitalWrite(cfg::PIN_CHG_CS_HI, LOW);
  pinMode(cfg::PIN_CHG_CS_LO, OUTPUT);
  digitalWrite(cfg::PIN_CHG_CS_LO, HIGH);  // Lader gestoppt
  pinMode(cfg::PIN_BUZZER, OUTPUT);
  digitalWrite(cfg::PIN_BUZZER, LOW);
  pinMode(cfg::PIN_CHG_CHRG, INPUT);
  pinMode(cfg::PIN_CHG_STDBY, INPUT);
  pinMode(cfg::PIN_BUTTON, INPUT_PULLUP);

  ledcSetup(FAN_LEDC_CH, 25000, 8);
  ledcAttachPin(cfg::PIN_FAN_PWM, FAN_LEDC_CH);
  ledcWrite(FAN_LEDC_CH, 255 - 50);  // ca. 20 %
  pinMode(cfg::PIN_FAN_TACH, INPUT);
  attachInterrupt(digitalPinToInterrupt(cfg::PIN_FAN_TACH), onTach, FALLING);

  Wire.begin(cfg::PIN_SDA, cfg::PIN_SCL, 400000);
  inaA.begin(cfg::ADDR_INA_A);
  inaB.begin(cfg::ADDR_INA_B);
  m_.dacOk = dac.begin(cfg::ADDR_DAC);
  bool b1 = ads1.begin(cfg::ADDR_ADS_1);
  bool b2 = ads2.begin(cfg::ADDR_ADS_2);
  m_.balOk = b1 && b2;

  dallas.begin();
  dallas.setResolution(11);
  dallas.setWaitForConversion(false);
  rescanTempSensors();
}

bool Hal::inaOk(Port p) const { return p == PORT_A ? inaA.ok() : inaB.ok(); }

void Hal::resetCounters(Port p) {
  m_.port[p].chargeAs = 0;
  m_.port[p].energyWs = 0;
}

void Hal::update(uint32_t now) {
  if (now - lastIna_ >= cfg::INA_PERIOD_MS) readIna(now);
  readBalancer(now);
  if (now - lastTemp_ >= cfg::TEMP_PERIOD_MS) readTemps(now);
  controlLoad(now);
  if (now - lastFan_ >= 1000) controlFan(now);

  // Lader-Takt (Drosselung)
  if (chgMode_ != ChargerMode::Off) {
    bool active = true;
    if (chgDuty_ < 0.99f) {
      uint32_t phase = now % cfg::CHG_THROTTLE_PERIOD_MS;
      active = phase < uint32_t(chgDuty_ * cfg::CHG_THROTTLE_PERIOD_MS);
    }
    applyChargerPins(active);
  }
  m_.chgCharging = digitalRead(cfg::PIN_CHG_CHRG) == LOW;
  m_.chgFull = digitalRead(cfg::PIN_CHG_STDBY) == LOW;

  // Summer
  if (alarmBeeps_ && now >= alarmNext_) {
    beep(150);
    alarmBeeps_--;
    alarmNext_ = now + 300;
  }
  digitalWrite(cfg::PIN_BUZZER, (settings.buzzer && now < beepUntil_) ? HIGH : LOW);
}

void Hal::readIna(uint32_t now) {
  uint32_t us = micros();
  float dt = lastInaMicros_ ? (us - lastInaMicros_) * 1e-6f : 0;
  if (dt > 1.0f) dt = 0;  // nach Hängern nicht falsch aufsummieren
  lastInaMicros_ = us;
  lastIna_ = now;
  INA226* ina[2] = {&inaA, &inaB};
  for (int p = 0; p < 2; p++) {
    float sh, bus;
    PortMeas& pm = m_.port[p];
    if (!ina[p]->read(sh, bus)) {
      pm.ok = false;
      continue;
    }
    pm.ok = true;
    pm.v = bus * settings.vGain[p];
    // IN+ liegt am Akku: Strom aus dem Akku heraus ergibt positive Shuntspannung
    pm.i = -(sh / cfg::SHUNT_OHM) * settings.iGain[p] - settings.iOffset[p];
    pm.p = pm.v * pm.i;
    pm.chargeAs += pm.i * dt;
    pm.energyWs += pm.p * dt;
    pm.seq++;
  }
}

void Hal::readBalancer(uint32_t now) {
  if (!m_.balOk) return;
  // Abgriff k: 0..3 an ADS1, 4..5 an ADS2
  ADS1115& dev = balStep_ < 4 ? ads1 : ads2;
  uint8_t ch = balStep_ < 4 ? balStep_ : balStep_ - 4;
  if (!balStarted_) {
    dev.start(ch);
    balStarted_ = true;
    lastBal_ = now;
    return;
  }
  if (now - lastBal_ < 10) return;
  float v;
  if (dev.result(v)) m_.tap[balStep_] = v * cfg::BAL_DIVIDER * settings.balGain[balStep_];
  balStarted_ = false;
  balStep_ = (balStep_ + 1) % cfg::BAL_TAPS;
  if (balStep_ == 0) {
    // Zellen ermitteln: jeder Abgriff muss deutlich über dem vorigen liegen
    uint8_t n = 0;
    float prev = 0;
    for (int k = 0; k < cfg::BAL_TAPS; k++) {
      float c = m_.tap[k] - prev;
      if (m_.tap[k] < 0.5f || c < 0.5f) break;
      m_.cell[k] = c;
      prev = m_.tap[k];
      n = k + 1;
    }
    for (int k = n; k < cfg::BAL_TAPS; k++) m_.cell[k] = 0;
    m_.balCells = n;
  }
}

static float readRole(int role) {
  if (!settings.sensorSet[role]) return NAN;
  float t = dallas.getTempC(settings.sensorAddr[role]);
  return (t <= DEVICE_DISCONNECTED_C + 1) ? NAN : t;
}

void Hal::readTemps(uint32_t now) {
  lastTemp_ = now;
  if (tempRequested_) {
    m_.tCell = readRole(SENSOR_CELL);
    m_.tPack = readRole(SENSOR_PACK);
    m_.tSink = readRole(SENSOR_SINK);
  }
  dallas.requestTemperatures();
  tempRequested_ = true;
}

void Hal::rescanTempSensors() {
  dallas.begin();
  // Neue Fühler automatisch freien Rollen zuweisen (Reihenfolge Kühlkörper, Zelle, Pack)
  const int order[3] = {SENSOR_SINK, SENSOR_CELL, SENSOR_PACK};
  bool changed = false;
  for (int i = 0; i < dallas.getDeviceCount(); i++) {
    uint8_t a[8];
    if (!dallas.getAddress(a, i)) continue;
    bool known = false;
    for (int r = 0; r < SENSOR_ROLES; r++)
      if (settings.sensorSet[r] && memcmp(settings.sensorAddr[r], a, 8) == 0) known = true;
    if (known) continue;
    for (int r : order) {
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

int Hal::tempSensors(TempSensorInfo* out, int max) {
  int n = 0;
  for (int i = 0; i < dallas.getDeviceCount() && n < max; i++) {
    TempSensorInfo& s = out[n];
    if (!dallas.getAddress(s.addr, i)) continue;
    s.t = dallas.getTempC(s.addr);
    s.role = -1;
    for (int r = 0; r < SENSOR_ROLES; r++)
      if (settings.sensorSet[r] && memcmp(settings.sensorAddr[r], s.addr, 8) == 0) s.role = r;
    n++;
  }
  return n;
}

// ---------------------------------------------------------------------------
// Last

void Hal::writeDac(float code) {
  if (code < 0) code = 0;
  if (code > 4095) code = 4095;
  dacCode_ = code;
  dac.set(uint16_t(code));
}

void Hal::setLoad(Port port, float amps) {
  if (amps < 0) amps = 0;
  float limit = port == PORT_A ? cfg::MAX_CURRENT_PORT_A : cfg::MAX_CURRENT_A;
  if (amps > limit) amps = limit;
  if (port != loadPort_ && (loadTarget_ > 0 || switchStage_)) {
    // Erst abschalten, dann umschalten
    pendingPort_ = port;
    pendingTarget_ = amps;
    loadTarget_ = 0;
    writeDac(0);
    digitalWrite(cfg::PIN_LOAD_EN_N, HIGH);
    switchStage_ = 1;
    switchAt_ = millis() + 150;
    return;
  }
  if (port != loadPort_) {
    loadPort_ = port;
    digitalWrite(cfg::PIN_RELAY_K2, port == PORT_B ? HIGH : LOW);
    pendingTarget_ = amps;
    switchStage_ = 2;
    switchAt_ = millis() + 100;  // Relais prellen lassen
    return;
  }
  if (switchStage_) {
    pendingTarget_ = amps;
    return;
  }
  if (amps > 0 && loadTarget_ == 0) {
    // Vorsteuerung mit dem Nennwert
    writeDac(amps * CODE_PER_AMP * 0.9f);
    digitalWrite(cfg::PIN_LOAD_EN_N, LOW);
  }
  if (amps == 0) {
    writeDac(0);
    digitalWrite(cfg::PIN_LOAD_EN_N, HIGH);
    saturated_ = false;
  }
  loadTarget_ = amps;
}

void Hal::controlLoad(uint32_t now) {
  if (switchStage_ && int32_t(now - switchAt_) >= 0) {
    if (switchStage_ == 1) {
      loadPort_ = pendingPort_;
      digitalWrite(cfg::PIN_RELAY_K2, loadPort_ == PORT_B ? HIGH : LOW);
      switchStage_ = 2;
      switchAt_ = now + 100;
    } else {
      switchStage_ = 0;
      float t = pendingTarget_;
      loadTarget_ = 0;
      setLoad(loadPort_, t);
    }
    return;
  }
  if (loadTarget_ <= 0) return;

  static uint32_t lastSeq = 0;
  const PortMeas& pm = m_.port[loadPort_];
  if (pm.seq == lastSeq) return;
  lastSeq = pm.seq;

  // Leistungsbegrenzung
  float target = loadTarget_;
  if (pm.v > 0.5f) {
    float pmax = cfg::MAX_POWER_W / pm.v;
    if (target > pmax) target = pmax;
  }
  // Nachregeln: Messstrom des INA226 ist maßgeblich
  float measured = -pm.i;
  float err = target - measured;
  float code = dacCode_ + err * CODE_PER_AMP * 0.35f;
  saturated_ = code >= 4095 && err > 0.05f;
  writeDac(code);
}

void Hal::controlFan(uint32_t now) {
  lastFan_ = now;
  uint32_t pulses = tachPulses;
  tachPulses = 0;
  m_.fanRpm = int(pulses * 30);  // 2 Impulse pro Umdrehung, 1 s Fenster
  float t = m_.tSink;
  float duty = 20;
  if (!isnan(t)) {
    if (t > 35) duty = 20 + (t - 35) * (80.0f / 25.0f);
  }
  if (loadTarget_ > 0 && duty < 40) duty = 40;
  if (fanBoost_ || isnan(t)) duty = 100;
  if (duty > 100) duty = 100;
  m_.fanDuty = uint8_t(duty);
  // AO3400 invertiert: GPIO HIGH zieht den PWM-Pin des Lüfters auf Masse
  ledcWrite(FAN_LEDC_CH, 255 - uint8_t(duty * 2.55f));
}

// ---------------------------------------------------------------------------
// Lader

void Hal::applyChargerPins(bool active) {
  if (!active || chgMode_ == ChargerMode::Off) {
    digitalWrite(cfg::PIN_CHG_CS_HI, LOW);
    digitalWrite(cfg::PIN_CHG_CS_LO, HIGH);
    return;
  }
  digitalWrite(cfg::PIN_CHG_CS_LO, LOW);
  // CS hoch = 4,2 V, CS offen = 3,6 V
  digitalWrite(cfg::PIN_CHG_CS_HI, chgMode_ == ChargerMode::Li42 ? HIGH : LOW);
}

void Hal::setCharger(ChargerMode mode) {
  if (mode == chgMode_) return;
  // Bei jedem Wechsel zuerst stoppen
  chgMode_ = ChargerMode::Off;
  applyChargerPins(false);
  if (mode == ChargerMode::Off) {
    delay(20);
    digitalWrite(cfg::PIN_RELAY_K1, LOW);
    return;
  }
  digitalWrite(cfg::PIN_RELAY_K1, HIGH);
  delay(60);  // Relais anziehen lassen, bevor der Lader startet
  chgMode_ = mode;
  chgDuty_ = 1;
  applyChargerPins(true);
}

void Hal::setChargerDuty(float d) { chgDuty_ = d < 0.1f ? 0.1f : (d > 1 ? 1 : d); }

void Hal::beep(uint16_t ms) { beepUntil_ = millis() + ms; }

void Hal::alarm() {
  alarmBeeps_ = 6;
  alarmNext_ = millis();
}

void Hal::killAll() {
  digitalWrite(cfg::PIN_LOAD_EN_N, HIGH);
  writeDac(0);
  loadTarget_ = 0;
  pendingTarget_ = 0;
  switchStage_ = 0;
  saturated_ = false;
  chgMode_ = ChargerMode::Off;
  applyChargerPins(false);
  digitalWrite(cfg::PIN_RELAY_K1, LOW);
}
