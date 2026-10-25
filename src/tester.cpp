#include "tester.h"
#include "cells.h"
#include <time.h>
#include "config.h"
#include "ocv.h"
#include "settings.h"
#include "soh.h"
#include "storage.h"

Tester tester;

static constexpr uint32_t REST_AFTER_MS = 10UL * 60 * 1000;
static constexpr uint32_t CHARGE_TIMEOUT_MS = 10UL * 3600 * 1000;

const char* phaseId(Phase p) {
  switch (p) {
    case Phase::Idle: return "idle";
    case Phase::PreCharge: return "precharge";
    case Phase::Rest: return "rest";
    case Phase::Discharge: return "discharge";
    case Phase::RestAfter: return "restafter";
    case Phase::Recharge: return "recharge";
    case Phase::Done: return "done";
    case Phase::Error: return "error";
    case Phase::ManualCharge: return "charge";
    case Phase::ManualDischarge: return "manualdischarge";
  }
  return "idle";
}

static bool isTestPhase(Phase p) {
  return p == Phase::PreCharge || p == Phase::Rest || p == Phase::Discharge || p == Phase::RestAfter ||
         p == Phase::Recharge;
}
static bool isChargePhase(Phase p) {
  return p == Phase::PreCharge || p == Phase::Recharge || p == Phase::ManualCharge;
}
static bool isDischargePhase(Phase p) { return p == Phase::Discharge || p == Phase::ManualDischarge; }
static bool isActive(Phase p) { return p != Phase::Idle && p != Phase::Done && p != Phase::Error; }

void Tester::begin() { idleChem_ = &CHEMISTRIES[0]; }

// ---------------------------------------------------------------------------
// Hilfswerte

float Tester::cellTemp() const { return job_.port == PORT_A ? hal.m().tCell : hal.m().tPack; }

float Tester::perCellV() const {
  const PortMeas& pm = hal.m().port[job_.port];
  return pm.v / (job_.cells ? job_.cells : 1);
}

float Tester::minCellV() const {
  const Meas& m = hal.m();
  if (job_.port == PORT_B && job_.cells > 1 && m.balCells == job_.cells) {
    float mn = 99;
    for (int k = 0; k < job_.cells; k++) mn = min(mn, m.cell[k]);
    return mn;
  }
  return perCellV();
}

float Tester::testMah() const { return float(-hal.m().port[job_.port].chargeAs / 3.6); }

float Tester::socPercent() const {
  const Chemistry* c = job_.chem ? job_.chem : idleChem_;
  const PortMeas& pm = hal.m().port[isActive(phase_) || phase_ == Phase::Done ? job_.port : PORT_A];
  if (phase_ == Phase::Recharge && capMah_ > 0)
    return constrain(float(hal.m().port[job_.port].chargeAs / 3.6) / capMah_ * 100.0f, 0.0f, 100.0f);
  if (phase_ == Phase::Discharge && job_.preCharge && job_.nomCapMah > 0)
    return constrain(100.0f - testMah() / job_.nomCapMah * 100.0f, 0.0f, 100.0f);
  // Ruhespannung schätzen: Spannungsabfall am Innenwiderstand herausrechnen
  uint8_t n = job_.cells && isActive(phase_) ? job_.cells : 1;
  float v = pm.v / n;
  if (lastRi_ > 0) v += (-pm.i) * (lastRi_ / 1000.0f) / n;
  if (v < 0.5f) return -1;
  return ocv::socFromCellV(*c, v);
}

float Tester::thermalFactor(float t, float warn, float stop) const {
  if (isnan(t) || t < warn) return 1;
  float f = 1 - 0.7f * (t - warn) / (stop - warn);
  return f < 0.3f ? 0.3f : f;
}

// ---------------------------------------------------------------------------
// Start / Stopp

String Tester::precheck(const JobConfig& c, bool charge, bool discharge) {
  if (isActive(phase_)) return "Es läuft bereits ein Vorgang";
  if (!c.chem) return "Bitte einen Zelltyp wählen";
  const Meas& m = hal.m();
  const PortMeas& pm = m.port[c.port];
  const char* pn = c.port == PORT_A ? "A" : "B";
  if (!pm.ok) return String("Strommesser an Anschluss ") + pn + " antwortet nicht";
  if (pm.v < 0.5f) return String("An Anschluss ") + pn + " ist kein Akku erkannt";
  if (pm.v > cfg::MAX_PACK_V) return "Spannung zu hoch (max. 6S)";
  if (c.cells < 1 || c.cells > 6) return "Zellzahl muss zwischen 1 und 6 liegen";
  if (c.port == PORT_A && c.cells != 1) return "Anschluss A ist nur für Einzelzellen";
  float vc = pm.v / c.cells;
  if (vc > c.chem->maxV + 0.08f)
    return "Die Spannung passt nicht zu Zelltyp und Zellzahl (" + String(vc, 2) + " V pro Zelle)";
  if (vc < c.chem->cutoffV - 0.5f) return "Akku ist tiefentladen. Zum Testen ungeeignet, siehe Handbuch";
  if (c.port == PORT_B && c.cells > 1 && c.chem->lithium) {
    if (!m.balOk) return "Balancer-Messung antwortet nicht";
    if (m.balCells != c.cells)
      return "Balancer zeigt " + String(m.balCells) + " Zellen, eingestellt sind " + String(c.cells);
  }
  if (charge) {
    if (c.port != PORT_A) return "Laden ist nur an Anschluss A möglich";
    if (c.chem->charger == ChargerMode::Off) return String(c.chem->name) + " kann hier nicht geladen werden";
    if (isnan(m.tCell)) return "Temperaturfühler an der Zelle fehlt";
  }
  if (discharge) {
    if (!m.dacOk) return "Last-Sollwertgeber (MCP4725) antwortet nicht";
    if (isnan(m.tSink)) return "Temperaturfühler am Kühlkörper fehlt";
    if (c.port == PORT_A && isnan(m.tCell)) return "Temperaturfühler an der Zelle fehlt";
    float lim = c.port == PORT_A ? cfg::MAX_CURRENT_PORT_A : cfg::MAX_CURRENT_A;
    if (c.currentA < 0.05f || c.currentA > lim)
      return "Entladestrom muss zwischen 0,05 und " + String(lim, 1) + " A liegen";
  }
  return "";
}

String Tester::startTest(const JobConfig& cIn) {
  JobConfig c = cIn;
  if (c.port == PORT_B) {
    c.preCharge = false;  // Packs werden extern geladen
    c.after = After::None;
  }
  if (c.chem && c.chem->charger == ChargerMode::Off) {
    c.preCharge = false;
    c.after = After::None;
  }
  String e = precheck(c, c.preCharge || c.after != After::None, true);
  if (e.length()) return e;
  e = linkCell(c, true);
  if (e.length()) return e;
  job_ = c;
  if (job_.cutoffV <= 0) job_.cutoffV = job_.chem->cutoffV;
  sdArmAt_ = 0;

  // Zähler und Messwerte zurücksetzen
  preChargeMah_ = startOcv_ = restOcv_ = capMah_ = energyWh_ = 0;
  rechargeMah_ = rechargeWh_ = 0;
  rechargeSec_ = 0;
  riCount_ = 0;
  riStage_ = 0;
  lastRi_ = 0;
  throttled_ = saturatedSeen_ = false;
  nPts_ = 0;
  ptInterval_ = 10;
  lastPtSec_ = 0;
  tempStart_ = cellTemp();
  tempMax_ = tempStart_;
  error_ = "";
  float ref = job_.nomCapMah > 0 ? job_.nomCapMah : (job_.chem->defaultCapMah > 0 ? job_.chem->defaultCapMah : 3000);
  riStepMah_ = max(50.0f, ref * 0.05f);
  startMs_ = millis();
  enter(job_.preCharge ? Phase::PreCharge : Phase::Rest);
  return "";
}

String Tester::startManualCharge(const JobConfig& cIn) {
  JobConfig c = cIn;
  String e = precheck(c, true, false);
  if (e.length()) return e;
  e = linkCell(c, false);
  if (e.length()) return e;
  job_ = c;
  sdArmAt_ = 0;
  error_ = "";
  nPts_ = 0;
  ptInterval_ = 10;
  lastPtSec_ = 0;
  startMs_ = millis();
  enter(Phase::ManualCharge);
  return "";
}

String Tester::startManualDischarge(const JobConfig& cIn) {
  JobConfig c = cIn;
  String e = precheck(c, false, true);
  if (e.length()) return e;
  e = linkCell(c, false);
  if (e.length()) return e;
  job_ = c;
  sdArmAt_ = 0;
  if (job_.cutoffV <= 0) job_.cutoffV = job_.chem->cutoffV;
  error_ = "";
  nPts_ = 0;
  ptInterval_ = 10;
  lastPtSec_ = 0;
  riCount_ = 0;
  riStage_ = 0;
  riStepMah_ = 1e9;  // keine Ri-Pulse im Handbetrieb
  startMs_ = millis();
  enter(Phase::ManualDischarge);
  return "";
}

void Tester::stop(const char* reason) {
  if (!isActive(phase_)) return;
  hal.killAll();
  error_ = reason ? reason : "Abgebrochen";
  enter(Phase::Error);
}

void Tester::acknowledge() {
  if (phase_ == Phase::Done || phase_ == Phase::Error) enter(Phase::Idle);
}

void Tester::fail(const String& reason) {
  hal.killAll();
  error_ = reason;
  enter(Phase::Error);
  hal.alarm();
}

void Tester::enter(Phase p) {
  uint32_t now = millis();
  phase_ = p;
  phaseMs_ = now;
  cutoffSince_ = fullSince_ = ovSince_ = ocSince_ = fanBadSince_ = 0;
  loadFactor_ = chgFactor_ = 1;
  switch (p) {
    case Phase::PreCharge:
    case Phase::Recharge:
    case Phase::ManualCharge:
      hal.setLoad(job_.port, 0);
      hal.resetCounters(job_.port);
      hal.setCharger(job_.chem->charger);
      break;
    case Phase::Discharge:
    case Phase::ManualDischarge:
      hal.setCharger(ChargerMode::Off);
      hal.resetCounters(job_.port);
      nextRiMah_ = 0;
      hal.setLoad(job_.port, job_.currentA);
      break;
    case Phase::Error:
      hal.killAll();
      break;
    default:
      hal.setCharger(ChargerMode::Off);
      hal.setLoad(job_.port, 0);
      break;
  }
  hal.setFanBoost(false);
}

// ---------------------------------------------------------------------------
// Hauptschleife

bool Tester::active() const { return isActive(phase_); }

// Zelle aus dem Bestand verknüpfen; ein Test legt bei Bedarf eine neue an
String Tester::linkCell(JobConfig& c, bool create) {
  if (!c.cellNo && !create) return "";
  cells::JobCell jc;
  jc.no = c.cellNo;
  jc.name = c.name;
  jc.presetId = c.presetId;
  jc.chem = c.chem ? c.chem->id : "liion";
  jc.cellCount = c.cells;
  jc.nomCapMah = c.nomCapMah;
  jc.riNewMilli = c.riNewMilli;
  String e = cells::prepare(jc);
  if (e.length()) return e;
  c.cellNo = jc.no;
  c.cellId = jc.id;
  c.name = jc.name;
  c.presetId = jc.presetId;
  c.nomCapMah = jc.nomCapMah;
  c.riNewMilli = jc.riNewMilli;
  return "";
}

void Tester::checkSelfDischarge(uint32_t now) {
  if (!sdArmAt_ || phase_ != Phase::Done || int32_t(now - sdArmAt_) < 0) return;
  sdArmAt_ = 0;
  float v = hal.m().port[job_.port].v / max<uint8_t>(1, job_.cells);
  if (job_.cellNo && v > 3.0f) cells::sdStart(job_.cellNo, v);
}

void Tester::update(uint32_t now) {
  checkSelfDischarge(now);
  if (!isActive(phase_)) return;
  safety(now);
  if (!isActive(phase_)) return;
  logPoint(now);

  const PortMeas& pm = hal.m().port[job_.port];
  switch (phase_) {
    case Phase::PreCharge:
    case Phase::Recharge:
      handleCharge(now, true);
      break;
    case Phase::ManualCharge:
      handleCharge(now, false);
      break;
    case Phase::Rest: {
      uint32_t restMs = uint32_t(job_.restMin) * 60000UL;
      if (now - phaseMs_ >= restMs) {
        startOcv_ = pm.v;
        enter(Phase::Discharge);
      }
      break;
    }
    case Phase::Discharge:
    case Phase::ManualDischarge:
      handleDischarge(now);
      break;
    case Phase::RestAfter:
      if (now - phaseMs_ >= REST_AFTER_MS) {
        restOcv_ = pm.v;
        if (job_.after == After::None) {
          finishTest();
        } else {
          storageTargetMah_ = job_.after == After::Storage ? capMah_ * 0.5f : 0;
          enter(Phase::Recharge);
        }
      }
      break;
    default:
      break;
  }
}

void Tester::safety(uint32_t now) {
  const Meas& m = hal.m();
  const PortMeas& pm = m.port[job_.port];

  if (digitalRead(cfg::PIN_BUTTON) == LOW) {
    fail("Not-Aus-Taste gedrückt");
    return;
  }
  if (!pm.ok) {
    fail("Strommesser antwortet nicht mehr");
    return;
  }
  if (pm.v > cfg::MAX_PACK_V) {
    fail("Spannung zu hoch");
    return;
  }

  // Überstrom
  float dis = -pm.i;
  float tgt = hal.loadTarget();
  bool oc = fabsf(pm.i) > cfg::MAX_CURRENT_A * 1.1f ||
            (dis > 0 && dis > max(tgt * cfg::OVERCURRENT_FACTOR, tgt + 0.3f) && !isChargePhase(phase_));
  if (isChargePhase(phase_) && pm.i > 2.5f) oc = true;  // TP5000 liefert max. 2 A
  if (oc) {
    if (!ocSince_) ocSince_ = now;
    if (now - ocSince_ > cfg::OVERCURRENT_MS) {
      fail("Überstrom " + String(fabsf(pm.i), 2) + " A");
      return;
    }
  } else {
    ocSince_ = 0;
  }

  // Temperaturen
  float tc = cellTemp();
  if (!isnan(tc)) {
    if (isnan(tempMax_) || tc > tempMax_) tempMax_ = tc;
  }
  bool needCellSensor = job_.port == PORT_A;
  if (needCellSensor && isnan(tc)) {
    fail("Temperaturfühler an der Zelle ausgefallen");
    return;
  }
  if (isChargePhase(phase_)) {
    if (!isnan(tc) && tc >= settings.chargeStopC) {
      fail("Zelle zu heiß beim Laden (" + String(tc, 1) + " °C)");
      return;
    }
    chgFactor_ = thermalFactor(tc, settings.chargeWarnC, settings.chargeStopC);
    hal.setChargerDuty(chgFactor_);
    if (chgFactor_ < 0.99f) throttled_ = true;
  } else {
    if (!isnan(tc) && tc >= settings.dischargeStopC) {
      fail("Zelle zu heiß beim Entladen (" + String(tc, 1) + " °C)");
      return;
    }
  }

  if (isDischargePhase(phase_)) {
    float ts = m.tSink;
    if (isnan(ts)) {
      fail("Temperaturfühler am Kühlkörper ausgefallen");
      return;
    }
    if (ts >= settings.sinkStopC) {
      fail("Kühlkörper zu heiß (" + String(ts, 1) + " °C)");
      return;
    }
    float f1 = thermalFactor(tc, settings.dischargeWarnC, settings.dischargeStopC);
    float f2 = thermalFactor(ts, settings.sinkWarnC, settings.sinkStopC);
    loadFactor_ = min(f1, f2);
    if (loadFactor_ < 0.99f) throttled_ = true;
    hal.setFanBoost(f2 < 0.99f);

    // Lüfter muss drehen, sobald es warm wird
    if (ts > 45 && m.fanRpm < 200) {
      if (!fanBadSince_) fanBadSince_ = now;
      if (now - fanBadSince_ > 10000) {
        fail("Lüfter dreht nicht");
        return;
      }
    } else {
      fanBadSince_ = 0;
    }

    // Harte Unterspannung (unabhängig vom normalen Entladeschluss)
    if (minCellV() < job_.cutoffV - 0.4f && hal.loadTarget() > 0) {
      if (!cutoffSince_) cutoffSince_ = now;
    }
  }

  // Überspannung beim Laden
  if (isChargePhase(phase_)) {
    if (perCellV() > job_.chem->maxV) {
      if (!ovSince_) ovSince_ = now;
      if (now - ovSince_ > 2000) {
        fail("Überspannung beim Laden (" + String(perCellV(), 3) + " V)");
        return;
      }
    } else {
      ovSince_ = 0;
    }
  }
}

void Tester::handleCharge(uint32_t now, bool isTest) {
  const Meas& m = hal.m();
  const PortMeas& pm = m.port[job_.port];
  uint32_t t = now - phaseMs_;
  float mah = float(pm.chargeAs / 3.6);

  if (t > CHARGE_TIMEOUT_MS) {
    fail("Laden dauert zu lange (über 10 h)");
    return;
  }
  // Lader startet nicht
  if (t > 120000 && mah < 5 && perCellV() < job_.chem->chargeV - 0.1f && !m.chgFull) {
    fail("Lader liefert keinen Strom");
    return;
  }

  bool full = false;
  if (m.chgFull) {
    if (!fullSince_) fullSince_ = now;
    if (now - fullSince_ > 5000) full = true;
  } else if (t > 60000 && pm.i < 0.05f && perCellV() >= job_.chem->chargeV - 0.05f && chgFactor_ > 0.99f) {
    if (!fullSince_) fullSince_ = now;
    if (now - fullSince_ > 60000) full = true;
  } else {
    fullSince_ = 0;
  }
  if (phase_ == Phase::Recharge && storageTargetMah_ > 0 && mah >= storageTargetMah_) full = true;

  if (!full) return;
  hal.setCharger(ChargerMode::Off);
  if (phase_ == Phase::PreCharge) {
    preChargeMah_ = mah;
    enter(Phase::Rest);
  } else if (phase_ == Phase::Recharge) {
    rechargeMah_ = mah;
    rechargeWh_ = float(pm.energyWs / 3600.0);
    rechargeSec_ = t / 1000;
    finishTest();
  } else {
    enter(Phase::Done);
    hal.beep(600);
  }
  (void)isTest;
}

void Tester::handleDischarge(uint32_t now) {
  const PortMeas& pm = hal.m().port[job_.port];
  capMah_ = testMah();
  energyWh_ = float(-pm.energyWs / 3600.0);
  if (hal.loadSaturated()) saturatedSeen_ = true;

  if (riStage_) {
    handleRiPulse(now);
    return;
  }

  // Sollstrom mit Temperaturdrosselung
  float target = job_.currentA * loadFactor_;
  if (fabsf(target - hal.loadTarget()) > 0.01f) hal.setLoad(job_.port, target);

  // Entladeschluss (2 s entprellt)
  float v = minCellV();
  if (v <= job_.cutoffV) {
    if (!cutoffSince_) cutoffSince_ = now;
  } else if (v > job_.cutoffV + 0.02f) {
    cutoffSince_ = 0;
  }
  if (cutoffSince_ && now - cutoffSince_ > 2000) {
    hal.setLoad(job_.port, 0);
    logPoint(now, true);
    if (phase_ == Phase::ManualDischarge) {
      enter(Phase::Done);
      hal.beep(600);
    } else {
      enter(Phase::RestAfter);
    }
    return;
  }

  // Ri-Puls fällig?
  if (capMah_ >= nextRiMah_ && now - phaseMs_ > 5000 && v > job_.cutoffV + 0.15f) {
    nextRiMah_ = capMah_ + riStepMah_;
    startRiPulse(now);
  }
}

// Innenwiderstand: 0,5 s Mittelwert beim Grundstrom, dann Stromsprung, nach 1 s erneut messen
void Tester::startRiPulse(uint32_t now) {
  if (riCount_ >= MAX_RI) return;
  const PortMeas& pm = hal.m().port[job_.port];
  riBase_ = hal.loadTarget();
  float lim = job_.port == PORT_A ? cfg::MAX_CURRENT_PORT_A : cfg::MAX_CURRENT_A;
  float delta = job_.port == PORT_A ? 1.5f : 1.0f;
  delta = min(delta, lim - riBase_);
  if (pm.v > 0.5f) delta = min(delta, cfg::MAX_POWER_W / pm.v * 0.95f - riBase_);
  if (delta < 0.2f) return;
  riDelta_ = delta;
  riStage_ = 1;
  riT0_ = now;
  riSumV1_ = riSumI1_ = 0;
  for (double& c : riCell1_) c = 0;
  riN1_ = 0;
  riSeq_ = pm.seq;
}

void Tester::handleRiPulse(uint32_t now) {
  const Meas& m = hal.m();
  const PortMeas& pm = m.port[job_.port];
  if (riStage_ == 1) {
    if (pm.seq != riSeq_) {
      riSeq_ = pm.seq;
      riSumV1_ += pm.v;
      riSumI1_ += -pm.i;
      for (int k = 0; k < 6; k++) riCell1_[k] += m.cell[k];
      riN1_++;
    }
    if (now - riT0_ >= 500 && riN1_ >= 5) {
      hal.setLoad(job_.port, riBase_ + riDelta_);
      riStage_ = 2;
      riT0_ = now;
    }
    return;
  }
  // Stufe 2: Abbruch, wenn die Spannung zu stark einbricht
  if (minCellV() < job_.cutoffV - 0.3f) {
    hal.setLoad(job_.port, riBase_);
    riStage_ = 0;
    return;
  }
  if (now - riT0_ < 1000) return;
  float v1 = riSumV1_ / riN1_, i1 = riSumI1_ / riN1_;
  float v2 = pm.v, i2 = -pm.i;
  hal.setLoad(job_.port, riBase_);
  riStage_ = 0;
  float di = i2 - i1;
  if (di < 0.15f) return;  // Stromsprung nicht erreicht, verwerfen
  RiSample& s = ri_[riCount_++];
  s.qmAh = capMah_;
  s.riMilli = (v1 - v2) / di * 1000.0f;
  for (int k = 0; k < 6; k++) s.cellRi[k] = k < m.balCells ? float((riCell1_[k] / riN1_ - m.cell[k]) / di * 1000.0) : 0;
  lastRi_ = s.riMilli;
}

void Tester::logPoint(uint32_t now, bool force) {
  uint32_t sec = (now - startMs_) / 1000;
  if (!force && nPts_ && sec - lastPtSec_ < ptInterval_) return;
  if (nPts_ >= MAX_POINTS) {
    // Ausdünnen: jeden zweiten Punkt behalten, Intervall verdoppeln
    int j = 0;
    for (int k = 0; k < nPts_; k += 2) pts_[j++] = pts_[k];
    nPts_ = j;
    ptInterval_ *= 2;
  }
  const PortMeas& pm = hal.m().port[job_.port];
  CurvePoint& p = pts_[nPts_++];
  p.t = sec;
  p.v = pm.v;
  p.i = pm.i;
  p.qmAh = float(pm.chargeAs / 3.6);
  p.temp = cellTemp();
  lastPtSec_ = sec;
}

// ---------------------------------------------------------------------------
// Ergebnis

void Tester::finishTest() {
  hal.setCharger(ChargerMode::Off);
  hal.setLoad(job_.port, 0);

  // Ri bei 100 % = erster Puls, Ri bei 50 % = Puls nächst der halben gemessenen Kapazität
  float ri100 = riCount_ ? ri_[0].riMilli : 0;
  int i50 = -1;
  float best = 1e9;
  for (int k = 0; k < riCount_; k++) {
    float d = fabsf(ri_[k].qmAh - capMah_ / 2);
    if (d < best) {
      best = d;
      i50 = k;
    }
  }
  float ri50 = i50 >= 0 ? ri_[i50].riMilli : 0;

  SohInput in{job_.chem, job_.cells, capMah_, ri50 > 0 ? ri50 : ri100, job_.nomCapMah, job_.riNewMilli};
  SohResult r = soh::compute(in);

  uint32_t id = settings.nextResultId++;
  settings.save();

  JsonDocument d;
  d["id"] = id;
  d["name"] = job_.name;
  d["cell"] = job_.cellNo;
  d["cellId"] = job_.cellId;
  time_t now = time(nullptr);
  d["time"] = now > 1700000000 ? uint32_t(now) : 0;
  d["port"] = job_.port == PORT_A ? "A" : "B";
  d["chem"] = job_.chem->id;
  d["preset"] = job_.presetId;
  const CellPreset* pr = findPreset(job_.presetId.c_str());
  d["presetName"] = pr ? pr->name : "";
  d["cells"] = job_.cells;
  d["nomCapMah"] = job_.nomCapMah;
  d["riNew"] = job_.riNewMilli;
  d["currentA"] = job_.currentA;
  d["cutoffV"] = job_.cutoffV;
  d["capMah"] = roundf(capMah_);
  d["energyWh"] = roundf(energyWh_ * 100) / 100;
  d["avgV"] = capMah_ > 1 ? roundf(energyWh_ / (capMah_ / 1000) * 1000) / 1000 : 0;
  d["ri100"] = roundf(ri100 * 10) / 10;
  d["ri50"] = roundf(ri50 * 10) / 10;
  if (job_.cells > 1 && i50 >= 0 && ri_[i50].cellRi[0] > 0) {
    JsonArray cr = d["cellRi"].to<JsonArray>();
    for (int k = 0; k < job_.cells; k++) cr.add(roundf(ri_[i50].cellRi[k] * 10) / 10);
  }
  JsonArray rl = d["riCurve"].to<JsonArray>();
  for (int k = 0; k < riCount_; k++) {
    JsonArray p = rl.add<JsonArray>();
    p.add(roundf(ri_[k].qmAh));
    p.add(roundf(ri_[k].riMilli * 10) / 10);
  }
  d["startOcv"] = startOcv_;
  d["restOcv"] = restOcv_;
  d["preChargeMah"] = roundf(preChargeMah_);
  d["rechargeMah"] = roundf(rechargeMah_);
  d["rechargeWh"] = roundf(rechargeWh_ * 100) / 100;
  d["rechargeSec"] = rechargeSec_;
  if (job_.after == After::Full && rechargeMah_ > 0) {
    d["coulombEff"] = roundf(capMah_ / rechargeMah_ * 1000) / 10;
    d["energyEff"] = rechargeWh_ > 0 ? roundf(energyWh_ / rechargeWh_ * 1000) / 10 : 0;
  }
  d["after"] = job_.after == After::Full ? "full" : job_.after == After::Storage ? "storage" : "none";
  d["tempMax"] = isnan(tempMax_) ? 0 : roundf(tempMax_ * 10) / 10;
  d["tempRise"] = (isnan(tempMax_) || isnan(tempStart_)) ? 0 : roundf((tempMax_ - tempStart_) * 10) / 10;
  d["throttled"] = throttled_;
  d["saturated"] = saturatedSeen_;
  d["durationSec"] = (millis() - startMs_) / 1000;
  JsonObject s = d["soh"].to<JsonObject>();
  s["score"] = r.score;
  s["low"] = r.low;
  s["high"] = r.high;
  s["estimated"] = r.estimated;
  s["capScore"] = r.capScore;
  s["riScore"] = r.riScore;
  s["capRatio"] = roundf(r.capRatio * 1000) / 10;
  s["refCapMah"] = r.refCapMah;
  s["verdict"] = soh::verdict(r.score);
  float base = cells::firstCapacity(job_.cellNo);
  if (base > 0) d["baselineCapMah"] = base;

  storage::saveResult(id, d, pts_, nPts_);
  cells::addResult(job_.cellNo, d);
  lastResultId_ = id;
  lastCellId_ = job_.cellId;
  lastCap_ = capMah_;
  lastScore_ = r.score;
  // nach dem Vollladen in einer Stunde die Startspannung für die Selbstentladung merken
  if (job_.after == After::Full && job_.cells == 1 && job_.chem->lithium) sdArmAt_ = millis() + 3600000UL;
  enter(Phase::Done);
  hal.beep(800);
}

// ---------------------------------------------------------------------------
// Ausgabe

void Tester::stateJson(JsonObject o) const {
  const Meas& m = hal.m();
  uint32_t now = millis();
  o["phase"] = phaseId(phase_);
  o["error"] = error_;
  o["lastResult"] = lastResultId_;
  if (isActive(phase_) || phase_ == Phase::Done || phase_ == Phase::Error) {
    JsonObject j = o["job"].to<JsonObject>();
    j["port"] = job_.port == PORT_A ? "A" : "B";
    j["name"] = job_.name;
    j["cell"] = job_.cellNo;
    j["cellId"] = job_.cellId;
    j["chem"] = job_.chem ? job_.chem->id : "";
    j["preset"] = job_.presetId;
    j["cells"] = job_.cells;
    j["currentA"] = job_.currentA;
    j["nomCapMah"] = job_.nomCapMah;
    j["cutoffV"] = job_.cutoffV;
    j["preCharge"] = job_.preCharge;
    j["restMin"] = job_.restMin;
    j["after"] = job_.after == After::Full ? "full" : job_.after == After::Storage ? "storage" : "none";
    o["elapsed"] = (now - startMs_) / 1000;
    o["phaseElapsed"] = (now - phaseMs_) / 1000;
    if (phase_ == Phase::Rest) o["phaseTotal"] = uint32_t(job_.restMin) * 60;
    if (phase_ == Phase::RestAfter) o["phaseTotal"] = REST_AFTER_MS / 1000;
    o["mah"] = isDischargePhase(phase_) ? capMah_ : float(m.port[job_.port].chargeAs / 3.6);
    o["wh"] = isDischargePhase(phase_) ? energyWh_ : float(m.port[job_.port].energyWs / 3600.0);
    o["capMah"] = capMah_;
    o["riLast"] = lastRi_;
    o["riCount"] = riCount_;
    o["riPulse"] = riStage_ != 0;
    o["throttle"] = isChargePhase(phase_) ? chgFactor_ : loadFactor_;
    o["throttled"] = throttled_;
    o["saturated"] = hal.loadSaturated();
    // Restzeit beim Entladen schätzen
    if (phase_ == Phase::Discharge && job_.nomCapMah > 0 && job_.currentA > 0) {
      float rest = job_.nomCapMah - capMah_;
      if (rest > 0) o["eta"] = uint32_t(rest / (job_.currentA * 1000) * 3600);
    }
  }
  o["soc"] = roundf(socPercent());
  if (lastScore_ >= 0) {
    JsonObject l = o["last"].to<JsonObject>();
    l["cellId"] = lastCellId_;
    l["cap"] = roundf(lastCap_);
    l["score"] = lastScore_;
    l["grade"] = String(soh::grade(lastScore_));
  }

  for (int p = 0; p < 2; p++) {
    JsonObject po = o["ports"].add<JsonObject>();
    const PortMeas& pm = m.port[p];
    po["ok"] = pm.ok;
    po["v"] = roundf(pm.v * 1000) / 1000;
    po["i"] = roundf(pm.i * 1000) / 1000;
    po["p"] = roundf(pm.p * 100) / 100;
  }
  o["loadPort"] = hal.loadPort() == PORT_A ? "A" : "B";
  o["loadTarget"] = hal.loadTarget();
  JsonObject t = o["temps"].to<JsonObject>();
  if (!isnan(m.tCell)) t["cell"] = m.tCell;
  if (!isnan(m.tPack)) t["pack"] = m.tPack;
  if (!isnan(m.tSink)) t["sink"] = m.tSink;
  JsonObject lim = o["limits"].to<JsonObject>();
  lim["chargeWarn"] = settings.chargeWarnC;
  lim["chargeStop"] = settings.chargeStopC;
  lim["dischargeWarn"] = settings.dischargeWarnC;
  lim["dischargeStop"] = settings.dischargeStopC;
  lim["sinkWarn"] = settings.sinkWarnC;
  lim["sinkStop"] = settings.sinkStopC;
  JsonObject f = o["fan"].to<JsonObject>();
  f["rpm"] = m.fanRpm;
  f["duty"] = m.fanDuty;
  JsonObject ch = o["charger"].to<JsonObject>();
  ch["mode"] = hal.charger() == ChargerMode::Li42 ? "4.2" : hal.charger() == ChargerMode::Lfp36 ? "3.6" : "off";
  ch["charging"] = m.chgCharging;
  ch["full"] = m.chgFull;
  JsonObject b = o["bal"].to<JsonObject>();
  b["ok"] = m.balOk;
  JsonArray bc = b["cells"].to<JsonArray>();
  for (int k = 0; k < m.balCells; k++) bc.add(roundf(m.cell[k] * 1000) / 1000);
}

void Tester::curveJson(JsonArray a) const {
  for (int k = 0; k < nPts_; k++) {
    JsonArray p = a.add<JsonArray>();
    p.add(pts_[k].t);
    p.add(roundf(pts_[k].v * 1000) / 1000);
    p.add(roundf(pts_[k].i * 1000) / 1000);
    p.add(roundf(pts_[k].qmAh));
    p.add(isnan(pts_[k].temp) ? 0 : roundf(pts_[k].temp * 10) / 10);
  }
}
