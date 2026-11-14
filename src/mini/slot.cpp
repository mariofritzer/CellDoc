#include "slot.h"
#include "cells.h"
#include <time.h>
#include "ocv.h"
#include "settings.h"
#include "soh.h"
#include "storage.h"
#include "evlog.h"

using namespace mcfg;

Slot slots[4];
static constexpr uint32_t REST_AFTER_MS = 10UL * 60 * 1000;
static constexpr uint32_t CHARGE_TIMEOUT_MS = 8UL * 3600 * 1000;

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

static bool isCharge(Phase p) { return p == Phase::PreCharge || p == Phase::Recharge || p == Phase::ManualCharge; }
static bool isDischarge(Phase p) { return p == Phase::Discharge || p == Phase::ManualDischarge; }

bool Slot::active() const { return phase_ != Phase::Idle && phase_ != Phase::Done && phase_ != Phase::Error; }

void Slot::begin(uint8_t idx) {
  s_ = idx;
  job_.chem = findChemistry("liion");
}

float Slot::mah() const {
  const SlotMeas& m = hal.m(s_);
  return isDischarge(phase_) ? capMah_ : float(m.chargeAs / 3.6);
}

float Slot::socPercent() const {
  const SlotMeas& m = hal.m(s_);
  if (m.v < 2.0f) return -1;
  if (phase_ == Phase::Recharge && capMah_ > 0) return constrain(float(m.chargeAs / 3.6) / capMah_ * 100, 0.0f, 100.0f);
  if (phase_ == Phase::Discharge && job_.preCharge && job_.nomCapMah > 0)
    return constrain(100 - capMah_ / job_.nomCapMah * 100, 0.0f, 100.0f);
  float v = m.v;
  if (lastRi_ > 0) v += (-m.i) * lastRi_ / 1000;
  return ocv::socFromCellV(*findChemistry("liion"), v);
}

float Slot::thermal(float t, float warn, float stop) const {
  if (isnan(t) || t < warn) return 1;
  float f = 1 - 0.7f * (t - warn) / (stop - warn);
  return f < 0.3f ? 0.3f : f;
}

// ---------------------------------------------------------------------------
// Restzeit (Schätzung)

float Slot::vSoc(float v, float iDis) const {
  if (lastRi_ > 0) v += iDis * lastRi_ / 1000;
  return ocv::socFromCellV(*findChemistry("liion"), v);
}

// Kapazität der Zelle: gemessen, hochgerechnet, Nennwert oder 2500 mAh
float Slot::capEstimate() const {
  bool measured = phase_ == Phase::RestAfter || phase_ == Phase::Recharge;
  if (measured && capMah_ > 50) return capMah_;
  if ((phase_ == Phase::Discharge || phase_ == Phase::ManualDischarge) && capMah_ > 150) {
    const SlotMeas& m = hal.m(s_);
    float d = disSoc0_ - vSoc(m.v, -m.i);
    if (d >= 15) {
      float est = capMah_ / (d / 100);
      if (est > capMah_ && est < 10000) return est;
    }
  }
  return job_.nomCapMah > 0 ? job_.nomCapMah : 2500;
}

static uint32_t chgSec(float mah, bool tail) {
  // Konstantstrom mit ca. 90 % des Ladestroms, danach Konstantspannung (TP4056) ca. 25 min
  return uint32_t(max(0.0f, mah) / (CHG_MAX_A * 900) * 3600) + (tail ? 1500 : 0);
}

uint32_t Slot::phaseEta(uint32_t now) const {
  const SlotMeas& m = hal.m(s_);
  uint32_t el = (now - phaseMs_) / 1000;
  float cap = capEstimate();
  switch (phase_) {
    case Phase::PreCharge:
    case Phase::ManualCharge:
    case Phase::Recharge: {
      if (phase_ == Phase::Recharge && storageMah_ > 0) return chgSec(storageMah_ - mah(), false);
      if (m.v >= 4.15f) return uint32_t(1500 * constrain((m.i - 0.1f) / (CHG_MAX_A - 0.1f), 0.04f, 1.0f));
      return chgSec(cap * max(0.0f, 85 - vSoc(m.v, -m.i)) / 100, true);
    }
    case Phase::Rest: {
      uint32_t T = uint32_t(job_.restMin) * 60;
      return el < T ? T - el : 1;
    }
    case Phase::Discharge:
    case Phase::ManualDischarge: {
      float a = job_.currentA > 0.05f ? job_.currentA : 0.5f;
      return uint32_t(max(0.0f, cap - capMah_) / (a * 1000) * 3600);
    }
    case Phase::RestAfter: {
      uint32_t T = REST_AFTER_MS / 1000;
      return el < T ? T - el : 1;
    }
    default:
      return 0;
  }
}

// Restzeit bis zum Ende des ganzen Tests (alle Phasen und Zyklen)
uint32_t Slot::totalEta(uint32_t now) const {
  uint32_t t = phaseEta(now);
  float cap = capEstimate();
  float a = job_.currentA > 0.05f ? job_.currentA : 0.5f;
  uint32_t rest = uint32_t(job_.restMin) * 60, ra = REST_AFTER_MS / 1000;
  uint32_t dis = uint32_t(cap / (a * 1000) * 3600);
  uint32_t full = chgSec(cap, true);
  uint32_t after = job_.after == After::Full ? full : job_.after == After::Storage ? chgSec(cap * 0.5f, false) : 0;
  int cur = cycleIdx_ + ((phase_ == Phase::Recharge && midRe_) ? 1 : 0);
  int left = int(job_.cycles) - cur - 1;
  switch (phase_) {
    case Phase::PreCharge: t += rest + dis + ra; break;
    case Phase::Rest: t += dis + ra; break;
    case Phase::Discharge: t += ra; break;
    case Phase::RestAfter: break;
    case Phase::Recharge:
      if (!midRe_) return t;
      t += rest + dis + ra;
      break;
    default:
      return 0;
  }
  if (left > 0) t += left * (full + rest + dis + ra);
  return t + after;
}

// ---------------------------------------------------------------------------

String Slot::precheck(const SlotJob& j, bool charge, bool discharge) const {
  if (active()) return "In diesem Schacht läuft bereits ein Vorgang";
  const SlotMeas& m = hal.m(s_);
  if (!m.ok) return "Strommesser von Schacht " + String(s_ + 1) + " antwortet nicht";
  if (m.v < 0.5f) return "In Schacht " + String(s_ + 1) + " steckt keine Zelle";
  if (m.v > MAX_CELL_V) return "Spannung zu hoch (" + String(m.v, 2) + " V). Ist das eine 18650 Li-Ion?";
  if (m.v < 2.5f) return "Zelle ist tiefentladen (" + String(m.v, 2) + " V). Zum Testen ungeeignet, siehe Handbuch";
  if (isnan(m.tCell) && !j.noTemp) return "Temperaturfühler an Schacht " + String(s_ + 1) + " fehlt";
  if (discharge) {
    if (!hal.dacOk()) return "Last-Sollwertgeber (MCP4728) antwortet nicht";
#ifdef CELLDOC_NANO
    if (settings.sensorSet[SENSOR_SINK] && isnan(hal.tSink())) return "Temperaturfühler am Lastwiderstand fehlt";
#else
    if (isnan(hal.tSink())) return "Temperaturfühler am Kühlkörper fehlt";
#endif
    if (j.currentA < 0.05f || j.currentA > MAX_CURRENT_A)
      return "Entladestrom muss zwischen 0,05 und " + String(MAX_CURRENT_A, 1) + " A liegen";
  }
  (void)charge;
  return "";
}

String Slot::startTest(const SlotJob& jIn) {
  SlotJob j = jIn;
  j.chem = findChemistry("liion");
  if (j.noTemp && j.currentA > NO_TEMP_MAX_A) j.currentA = NO_TEMP_MAX_A;
  String e = precheck(j, true, true);
  if (e.length()) return e;
  e = linkCell(j, true);
  if (e.length()) return e;
  job_ = j;
  sdArmAt_ = 0;
  if (job_.cutoffV <= 0) job_.cutoffV = job_.chem->cutoffV;
  if (job_.cutoffV < 2.7f) job_.cutoffV = 2.7f;
  job_.cycles = constrain(job_.cycles, 1, 10);
  cycleIdx_ = nCyc_ = 0;
  midRe_ = false;
  lastChg_ = 0;
  preMah_ = startOcv_ = restOcv_ = capMah_ = energyWh_ = reMah_ = reWh_ = 0;
  reSec_ = 0;
  riCount_ = riStage_ = 0;
  lastRi_ = 0;
  throttled_ = saturatedSeen_ = false;
  nPts_ = 0;
  ptInterval_ = 10;
  lastPtSec_ = 0;
  tStart_ = tMax_ = hal.m(s_).tCell;
  error_ = "";
  float ref = job_.nomCapMah > 0 ? job_.nomCapMah : 2600;
  riStep_ = max(50.0f, ref * 0.05f);
  startMs_ = millis();
  {
    const char* aft = job_.after == After::Full ? "voll laden" : job_.after == After::Storage ? "auf Lagerspannung laden" : "nichts";
    log('i', "Test gestartet: " + job_.cellId + (job_.name.length() && job_.name != job_.cellId ? " " + job_.name : "") +
                 ", entladen mit " + evlog::num(job_.currentA, 2) + " A bis " + evlog::num(job_.cutoffV, 2) + " V, Ruhe " +
                 String(job_.restMin) + " min, " + String(job_.cycles) + (job_.cycles > 1 ? " Zyklen" : " Zyklus") +
                 ", danach " + aft + (job_.preCharge ? "" : ", ohne Vorladen") +
                 (job_.noTemp ? ", ohne Zellfühler (bestätigt, Strom auf " + evlog::num(NO_TEMP_MAX_A, 1) + " A begrenzt)" : ""));
  }
  enter(job_.preCharge ? Phase::PreCharge : Phase::Rest);
  return "";
}

String Slot::startCharge(const SlotJob& jIn) {
  SlotJob j = jIn;
  String e = precheck(j, true, false);
  if (e.length()) return e;
  e = linkCell(j, false);
  if (e.length()) return e;
  job_ = j;
  sdArmAt_ = 0;
  job_.chem = findChemistry("liion");
  error_ = "";
  nPts_ = 0;
  ptInterval_ = 10;
  lastPtSec_ = 0;
  startMs_ = millis();
  log('i', "Laden gestartet" + (job_.cellId.length() ? ": " + job_.cellId : String("")));
  enter(Phase::ManualCharge);
  return "";
}

String Slot::startDischarge(const SlotJob& jIn) {
  SlotJob j = jIn;
  if (j.noTemp && j.currentA > NO_TEMP_MAX_A) j.currentA = NO_TEMP_MAX_A;
  String e = precheck(j, false, true);
  if (e.length()) return e;
  e = linkCell(j, false);
  if (e.length()) return e;
  job_ = j;
  sdArmAt_ = 0;
  job_.chem = findChemistry("liion");
  if (job_.cutoffV < 2.7f) job_.cutoffV = job_.chem->cutoffV;
  error_ = "";
  nPts_ = 0;
  ptInterval_ = 10;
  lastPtSec_ = 0;
  riCount_ = riStage_ = 0;
  riStep_ = 1e9;
  startMs_ = millis();
  log('i', "Entladen gestartet" + (job_.cellId.length() ? ": " + job_.cellId : String("")) + ", mit " +
               evlog::num(job_.currentA, 2) + " A bis " + evlog::num(job_.cutoffV, 2) + " V");
  disSoc0_ = vSoc(hal.m(s_).v, 0);
  enter(Phase::ManualDischarge);
  return "";
}

// ---------------------------------------------------------------------------
// Ereignisprotokoll

static const char* phaseTxt(Phase p) {
  switch (p) {
    case Phase::PreCharge: return "Vorladen";
    case Phase::Rest: return "Ruhen vor dem Entladen";
    case Phase::Discharge: return "Entladen";
    case Phase::RestAfter: return "Erholen nach dem Entladen";
    case Phase::Recharge: return "Nachladen";
    case Phase::ManualCharge: return "Laden";
    case Phase::ManualDischarge: return "Entladen";
    case Phase::Done: return "Fertig";
    case Phase::Error: return "Abbruch";
    default: return "Bereit";
  }
}

static String dur(uint32_t ms) {
  uint32_t s = ms / 1000;
  if (s < 120) return String(s) + " s";
  uint32_t m = (s + 30) / 60;
  if (m < 60) return String(m) + " min";
  return String(m / 60) + " h " + String(m % 60) + " min";
}

void Slot::log(char level, const String& text) const { evlog::add(level, s_ + 1, text); }

String Slot::snap() const {
  const SlotMeas& m = hal.m(s_);
  String s = "U " + evlog::num(m.v, 3) + " V, I " + evlog::num(m.i, 3) + " A";
  if (!isnan(m.tCell)) s += ", Zelle " + evlog::num(m.tCell, 1) + " °C";
  return s;
}

void Slot::stop(const char* reason) {
  if (!active()) return;
  log('w', String("Gestoppt: ") + (reason ? reason : "Abgebrochen") + " (während " + phaseTxt(phase_) + ", " + snap() + ")");
  hal.kill(s_);
  error_ = reason ? reason : "Abgebrochen";
  enter(Phase::Error);
}

void Slot::acknowledge() {
  if (phase_ == Phase::Done || phase_ == Phase::Error) enter(Phase::Idle);
}

void Slot::fail(const String& why) {
  log('e', "Abbruch: " + why + " (während " + phaseTxt(phase_) + ", " + snap() + ")");
  hal.kill(s_);
  error_ = why;
  enter(Phase::Error);
  hal.alarm();
}

void Slot::enter(Phase p) {
  phase_ = p;
  phaseMs_ = millis();
  cutoffSince_ = fullSince_ = ovSince_ = ocSince_ = 0;
  loadF_ = chgF_ = 1;
  thrLog_ = false;
  if (p == Phase::Done) log('i', "Fertig");
  else if (p != Phase::Idle && p != Phase::Error) log('i', String("Phase: ") + phaseTxt(p) + " (" + snap() + ")");
  if (isCharge(p)) {
    hal.setLoad(s_, 0);
    hal.resetCounters(s_);
    hal.setCharger(s_, true);
  } else if (isDischarge(p)) {
    hal.setCharger(s_, false);
    hal.resetCounters(s_);
    nextRiMah_ = 0;
    hal.setLoad(s_, job_.currentA);
  } else {
    hal.kill(s_);
  }
}

// ---------------------------------------------------------------------------

String Slot::linkCell(SlotJob& j, bool create) {
  if (!j.cellNo && !create) return "";
  cells::JobCell c;
  c.no = j.cellNo;
  c.name = j.name;
  c.presetId = j.presetId;
  c.chem = "liion";
  c.nomCapMah = j.nomCapMah;
  c.riNewMilli = j.riNewMilli;
  for (int k = 0; k < NSLOTS; k++)
    if (k != s_ && c.no && slots[k].active() && slots[k].cellNo() == c.no)
      return "Diese Zelle steckt schon in Schacht " + String(k + 1);
  String e = cells::prepare(c);
  if (e.length()) return e;
  j.cellNo = c.no;
  j.cellId = c.id;
  j.name = c.name;
  j.presetId = c.presetId;
  j.nomCapMah = c.nomCapMah;
  j.riNewMilli = c.riNewMilli;
  return "";
}

void Slot::checkSelfDischarge(uint32_t now) {
  if (!sdArmAt_ || phase_ != Phase::Done || int32_t(now - sdArmAt_) < 0) return;
  sdArmAt_ = 0;
  float v = hal.m(s_).v;
  if (!job_.cellNo || v < 2.5f) return;
  cells::setRest(job_.cellNo, v, "test");
  if (sdStartToo_ && v > 3.0f) cells::sdStart(job_.cellNo, v);
}

void Slot::update(uint32_t now) {
  checkSelfDischarge(now);
  if (!active()) return;
  safety(now);
  if (!active()) return;
  logPoint(now);
  const SlotMeas& m = hal.m(s_);
  switch (phase_) {
    case Phase::PreCharge:
    case Phase::Recharge:
    case Phase::ManualCharge:
      handleCharge(now);
      break;
    case Phase::Rest:
      if (now - phaseMs_ >= uint32_t(job_.restMin) * 60000UL) {
        log('i', "Ruhezeit vorbei (" + String(job_.restMin) + " min), Ruhespannung " + evlog::num(m.v, 3) + " V");
        startOcv_ = m.v;
        disSoc0_ = vSoc(m.v, 0);
        enter(Phase::Discharge);
      }
      break;
    case Phase::Discharge:
    case Phase::ManualDischarge:
      handleDischarge(now);
      break;
    case Phase::RestAfter:
      if (now - phaseMs_ >= REST_AFTER_MS) {
        log('i', "Erholt: Ruhespannung " + evlog::num(m.v, 3) + " V nach " + dur(now - phaseMs_) +
                     (cycleIdx_ + 1 < job_.cycles ? ", weiter mit Zyklus " + String(cycleIdx_ + 2) + " von " + String(job_.cycles)
                      : job_.after == After::None ? String(", Test wird abgeschlossen")
                      : job_.after == After::Storage ? ", lade auf Lagerspannung (" + evlog::num(capMah_ * 0.5f, 0) + " mAh)"
                                                     : String(", lade wieder voll")));
        restOcv_ = m.v;
        if (nCyc_ < 10) {
          cycCap_[nCyc_] = capMah_;
          cycWh_[nCyc_] = energyWh_;
          cycRi_[nCyc_] = ri50();
          cycChg_[nCyc_] = cycleIdx_ == 0 ? preMah_ : lastChg_;
          nCyc_++;
        }
        if (cycleIdx_ + 1 < job_.cycles) {
          midRe_ = true;
          storageMah_ = 0;
          enter(Phase::Recharge);
        } else if (job_.after == After::None) {
          finish();
        } else {
          storageMah_ = job_.after == After::Storage ? capMah_ * 0.5f : 0;
          enter(Phase::Recharge);
        }
      }
      break;
    default:
      break;
  }
}

void Slot::safety(uint32_t now) {
  const SlotMeas& m = hal.m(s_);
#ifndef CELLDOC_NANO  // Nano: BOOT-Taste ist die Bedientaste (lang = stoppen)
  if (digitalRead(PIN_BOOT) == LOW) { fail("Not-Aus-Taste gedrückt"); return; }
#endif
  if (!m.ok) { fail("Strommesser antwortet nicht mehr"); return; }
  float t = m.tCell;
  // Ohne Zellfühler (vom Benutzer bestätigt) entfallen nur die Zelltemperatur-Grenzen
  if (isnan(t) && !job_.noTemp) { fail("Temperaturfühler an der Zelle ausgefallen"); return; }
  if (isnan(tMax_) || t > tMax_) tMax_ = t;

  float dis = -m.i, tgt = hal.loadTarget(s_);
  bool oc = fabsf(m.i) > MAX_CURRENT_A * 1.3f + 0.2f;
  if (isDischarge(phase_) && dis > max(tgt * OVERCURRENT_FACTOR, tgt + 0.3f)) oc = true;
  if (isCharge(phase_) && m.i > CHG_MAX_A * 1.3f) oc = true;
  if (oc) {
    if (!ocSince_) ocSince_ = now;
    if (now - ocSince_ > OVERCURRENT_MS) { fail("Überstrom " + String(fabsf(m.i), 2) + " A"); return; }
  } else {
    ocSince_ = 0;
  }

  if (isCharge(phase_)) {
    if (t >= settings.chargeStopC) { fail("Zelle zu heiß beim Laden (" + String(t, 1) + " °C)"); return; }
    chgF_ = thermal(t, settings.chargeWarnC, settings.chargeStopC);
    hal.setChargerDuty(s_, chgF_);
    if (chgF_ < 0.99f) throttled_ = true;
    if (m.v > 4.25f) {
      if (!ovSince_) ovSince_ = now;
      if (now - ovSince_ > 2000) { fail("Überspannung beim Laden (" + String(m.v, 3) + " V)"); return; }
    } else {
      ovSince_ = 0;
    }
  } else if (isDischarge(phase_)) {
    if (t >= settings.dischargeStopC) { fail("Zelle zu heiß beim Entladen (" + String(t, 1) + " °C)"); return; }
    float ts = hal.tSink();
    float sinkStop = min(settings.sinkStopC, SINK_STOP_C);
#ifdef CELLDOC_NANO
    // Fühler am Lastwiderstand ist optional
    if (isnan(ts) && settings.sensorSet[SENSOR_SINK]) { fail("Temperaturfühler am Lastwiderstand ausgefallen"); return; }
    if (!isnan(ts) && ts >= sinkStop) { fail("Lastwiderstand zu heiß (" + String(ts, 1) + " °C)"); return; }
#else
    if (isnan(ts)) { fail("Temperaturfühler am Kühlkörper ausgefallen"); return; }
    if (ts >= sinkStop) { fail("Kühlkörper zu heiß (" + String(ts, 1) + " °C)"); return; }
#endif
    loadF_ = min(thermal(t, settings.dischargeWarnC, settings.dischargeStopC), thermal(ts, settings.sinkWarnC, sinkStop));
    if (loadF_ < 0.99f) throttled_ = true;
  }
  // Beginn und Ende einer Drosselung protokollieren
  float f = isCharge(phase_) ? chgF_ : isDischarge(phase_) ? loadF_ : 1;
  bool thr = f < 0.99f;
  if (thr != thrLog_) {
    thrLog_ = thr;
    float ts = hal.tSink();
    String temps = "Zelle " + (isnan(t) ? String("?") : evlog::num(t, 1)) + " °C" +
                   (isDischarge(phase_) && !isnan(ts) ? ", Last " + evlog::num(ts, 1) + " °C" : "");
    if (thr) log('w', String(isCharge(phase_) ? "Laden" : "Entladen") + " gedrosselt auf " + evlog::num(f * 100, 0) + " % wegen Wärme (" + temps + ")");
    else log('i', "Drosselung aufgehoben (" + temps + ")");
  }
}

void Slot::handleCharge(uint32_t now) {
  const SlotMeas& m = hal.m(s_);
  if (hal.chargerWaiting(s_)) {
    // wartet auf einen freien Ladeplatz: Zeitüberwachung beginnt erst beim Laden
    phaseMs_ = now;
    fullSince_ = 0;
    hal.resetCounters(s_);
    return;
  }
  uint32_t t = now - phaseMs_;
  float mah = float(m.chargeAs / 3.6);
  if (t > CHARGE_TIMEOUT_MS) { fail("Laden dauert zu lange (über 8 h)"); return; }
  if (t > 120000 && mah < 5 && m.v < 4.1f) { fail("Lader liefert keinen Strom"); return; }
  bool full = false;
  // TP4056 beendet bei ca. 1/10 des Ladestroms. Danach sinkt die Spannung oft unter 4,15 V,
  // deshalb gilt auch "Lader liefert praktisch nichts mehr" bei nahezu voller Zelle als fertig.
  bool chgDone = m.i < 0.12f && (m.v >= 4.15f || (m.i < 0.02f && m.v >= 4.0f));
  if (t > 60000 && chgDone && chgF_ > 0.99f) {
    if (!fullSince_) fullSince_ = now;
    if (now - fullSince_ > 30000) full = true;
  } else {
    fullSince_ = 0;
  }
  bool storageHit = phase_ == Phase::Recharge && storageMah_ > 0 && mah >= storageMah_;
  if (storageHit) full = true;
  if (!full) return;
  {
    String why = storageHit ? "Lagerladung erreicht (" + evlog::num(mah, 0) + " von " + evlog::num(storageMah_, 0) + " mAh)"
                 : m.v >= 4.15f ? "Zelle voll, Ladestrom seit 30 s unter 0,12 A bei " + evlog::num(m.v, 3) + " V"
                                : "Lader hat abgeschaltet (" + evlog::num(m.i * 1000, 0) + " mA) bei " + evlog::num(m.v, 3) +
                                      " V, Zelle gilt als voll";
    log('i', String(phaseTxt(phase_)) + " beendet: " + why + ". Geladen " + evlog::num(mah, 0) + " mAh in " + dur(t));
  }
  hal.setCharger(s_, false);
  if (phase_ == Phase::PreCharge) {
    preMah_ = mah;
    enter(Phase::Rest);
  } else if (phase_ == Phase::Recharge && midRe_) {
    midRe_ = false;
    lastChg_ = mah;
    cycleIdx_++;
    riCount_ = 0;
    enter(Phase::Rest);
  } else if (phase_ == Phase::Recharge) {
    reMah_ = mah;
    reWh_ = float(m.energyWs / 3600.0);
    reSec_ = t / 1000;
    finish();
  } else {
    enter(Phase::Done);
    hal.beep(500);
  }
}

void Slot::handleDischarge(uint32_t now) {
  const SlotMeas& m = hal.m(s_);
  capMah_ = float(-m.chargeAs / 3.6);
  energyWh_ = float(-m.energyWs / 3600.0);
  if (hal.loadSaturated(s_)) saturatedSeen_ = true;
  if (riStage_) { handleRi(now); return; }
  float target = job_.currentA * loadF_;
  if (fabsf(target - hal.loadTarget(s_)) > 0.01f) hal.setLoad(s_, target);
  if (m.v <= job_.cutoffV) {
    if (!cutoffSince_) cutoffSince_ = now;
  } else if (m.v > job_.cutoffV + 0.02f) {
    cutoffSince_ = 0;
  }
  if (cutoffSince_ && now - cutoffSince_ > 2000) {
    log('i', String(phaseTxt(phase_)) + " beendet: Abschaltspannung " + evlog::num(job_.cutoffV, 2) + " V erreicht (" +
                 evlog::num(m.v, 3) + " V seit 2 s). Entnommen " + evlog::num(capMah_, 0) + " mAh, " +
                 evlog::num(energyWh_, 2) + " Wh in " + dur(now - phaseMs_));
    hal.setLoad(s_, 0);
    logPoint(now, true);
    if (phase_ == Phase::ManualDischarge) {
      enter(Phase::Done);
      hal.beep(500);
    } else {
      enter(Phase::RestAfter);
    }
    return;
  }
  if (capMah_ >= nextRiMah_ && now - phaseMs_ > 5000 && m.v > job_.cutoffV + 0.15f) {
    nextRiMah_ = capMah_ + riStep_;
    startRi(now);
  }
}

void Slot::startRi(uint32_t now) {
  if (riCount_ >= MAX_RI) return;
  riBase_ = hal.loadTarget(s_);
#ifdef CELLDOC_NANO
  // Nano: Last kurz ganz abschalten (Sprung nach unten)
  riDelta_ = -riBase_;
  if (riBase_ < 0.2f) return;
#else
  riDelta_ = min(1.0f, MAX_CURRENT_A - riBase_);
  if (riDelta_ < 0.2f) return;
#endif
  riStage_ = 1;
  riT0_ = now;
  riV1_ = riI1_ = 0;
  riN1_ = 0;
  riSeq_ = hal.m(s_).seq;
}

void Slot::handleRi(uint32_t now) {
  const SlotMeas& m = hal.m(s_);
  if (riStage_ == 1) {
    if (m.seq != riSeq_) {
      riSeq_ = m.seq;
      riV1_ += m.v;
      riI1_ += -m.i;
      riN1_++;
    }
    if (now - riT0_ >= 500 && riN1_ >= 5) {
      hal.setLoad(s_, riBase_ + riDelta_);
      riStage_ = 2;
      riT0_ = now;
    }
    return;
  }
  if (m.v < job_.cutoffV - 0.3f) {
    hal.setLoad(s_, riBase_);
    riStage_ = 0;
    return;
  }
  if (now - riT0_ < 1000) return;
  float v1 = riV1_ / riN1_, i1 = riI1_ / riN1_, v2 = m.v, i2 = -m.i;
  hal.setLoad(s_, riBase_);
  riStage_ = 0;
  if (fabsf(i2 - i1) < 0.15f) return;
  riQ_[riCount_] = capMah_;
  riR_[riCount_] = (v1 - v2) / (i2 - i1) * 1000;
  lastRi_ = riR_[riCount_];
  riCount_++;
}

void Slot::logPoint(uint32_t now, bool force) {
  uint32_t sec = (now - startMs_) / 1000;
  if (!force && nPts_ && sec - lastPtSec_ < ptInterval_) return;
  if (nPts_ >= MAX_POINTS) {
    int j = 0;
    for (int k = 0; k < nPts_; k += 2) pts_[j++] = pts_[k];
    nPts_ = j;
    ptInterval_ *= 2;
  }
  const SlotMeas& m = hal.m(s_);
  pts_[nPts_++] = {sec, m.v, m.i, float(m.chargeAs / 3.6), m.tCell};
  lastPtSec_ = sec;
}

float Slot::ri50() const {
  float r = riCount_ ? riR_[0] : 0, best = 1e9;
  for (int k = 0; k < riCount_; k++) {
    float d = fabsf(riQ_[k] - capMah_ / 2);
    if (d < best) { best = d; r = riR_[k]; }
  }
  return r;
}

void Slot::finish() {
  hal.kill(s_);
  float ri100 = riCount_ ? riR_[0] : 0, ri50 = 0, best = 1e9;
  for (int k = 0; k < riCount_; k++) {
    float d = fabsf(riQ_[k] - capMah_ / 2);
    if (d < best) { best = d; ri50 = riR_[k]; }
  }
  SohResult r = soh::compute({job_.chem, 1, capMah_, ri50 > 0 ? ri50 : ri100, job_.nomCapMah, job_.riNewMilli});
  uint32_t id = settings.nextResultId++;
  settings.save();
  JsonDocument d;
  d["id"] = id;
  d["name"] = job_.name;
  d["cell"] = job_.cellNo;
  d["cellId"] = job_.cellId;
  time_t now = time(nullptr);
  d["time"] = now > 1700000000 ? uint32_t(now) : 0;
  d["port"] = "S" + String(s_ + 1);
  d["slot"] = s_ + 1;
  d["chem"] = job_.chem->id;
  d["preset"] = job_.presetId;
  const CellPreset* pr = findPreset(job_.presetId.c_str());
  d["presetName"] = pr ? pr->name : "";
  d["cells"] = 1;
  d["nomCapMah"] = job_.nomCapMah;
  d["riNew"] = job_.riNewMilli;
  d["currentA"] = job_.currentA;
  d["cutoffV"] = job_.cutoffV;
  d["capMah"] = roundf(capMah_);
  d["energyWh"] = roundf(energyWh_ * 100) / 100;
  d["avgV"] = capMah_ > 1 ? roundf(energyWh_ / (capMah_ / 1000) * 1000) / 1000 : 0;
  d["ri100"] = roundf(ri100 * 10) / 10;
  d["ri50"] = roundf(ri50 * 10) / 10;
  JsonArray rl = d["riCurve"].to<JsonArray>();
  for (int k = 0; k < riCount_; k++) {
    JsonArray p = rl.add<JsonArray>();
    p.add(roundf(riQ_[k]));
    p.add(roundf(riR_[k] * 10) / 10);
  }
  d["startOcv"] = startOcv_;
  d["restOcv"] = restOcv_;
  d["preChargeMah"] = roundf(preMah_);
  d["rechargeMah"] = roundf(reMah_);
  d["rechargeWh"] = roundf(reWh_ * 100) / 100;
  d["rechargeSec"] = reSec_;
  if (job_.after == After::Full && reMah_ > 0) {
    d["coulombEff"] = roundf(capMah_ / reMah_ * 1000) / 10;
    d["energyEff"] = reWh_ > 0 ? roundf(energyWh_ / reWh_ * 1000) / 10 : 0;
  }
  d["after"] = job_.after == After::Full ? "full" : job_.after == After::Storage ? "storage" : "none";
  d["tempMax"] = isnan(tMax_) ? 0 : roundf(tMax_ * 10) / 10;
  d["tempRise"] = (isnan(tMax_) || isnan(tStart_)) ? 0 : roundf((tMax_ - tStart_) * 10) / 10;
  d["throttled"] = throttled_;
  d["saturated"] = saturatedSeen_;
  if (job_.noTemp) d["noTemp"] = true;
  d["durationSec"] = (millis() - startMs_) / 1000;
  if (nCyc_ > 1) {
    JsonArray ca = d["cycles"].to<JsonArray>();
    for (int k = 0; k < nCyc_; k++) {
      JsonObject c = ca.add<JsonObject>();
      c["cap"] = roundf(cycCap_[k]);
      c["wh"] = roundf(cycWh_[k] * 100) / 100;
      c["ri"] = roundf(cycRi_[k] * 10) / 10;
      c["chg"] = roundf(cycChg_[k]);
    }
  }
  JsonObject so = d["soh"].to<JsonObject>();
  so["score"] = r.score;
  log('i', "Test fertig: " + evlog::num(capMah_, 0) + " mAh" +
               (job_.nomCapMah > 0 ? " (" + evlog::num(capMah_ / job_.nomCapMah * 100, 0) + " % der Nennkapazität)" : "") +
               ", Ri " + evlog::num(ri50 > 0 ? ri50 : ri100, 0) + " mΩ, Lebenszahl " + String(r.score) + ", Dauer " +
               dur(millis() - startMs_));
  so["low"] = r.low;
  so["high"] = r.high;
  so["estimated"] = r.estimated;
  so["capScore"] = r.capScore;
  so["riScore"] = r.riScore;
  so["capRatio"] = roundf(r.capRatio * 1000) / 10;
  so["refCapMah"] = r.refCapMah;
  so["verdict"] = soh::verdict(r.score);
  float base = cells::firstCapacity(job_.cellNo);
  if (base > 0) d["baselineCapMah"] = base;
  storage::saveResult(id, d, pts_, nPts_);
  cells::addResult(job_.cellNo, d);
  lastResultId_ = id;
  lastScore_ = r.score;
  lastCap_ = capMah_;
  lastCellId_ = job_.cellId;
  sdArmAt_ = millis() + 3600000UL;
  sdStartToo_ = job_.after == After::Full;
  enter(Phase::Done);
  hal.beep(800);
}

// ---------------------------------------------------------------------------

void Slot::stateJson(JsonObject o) const {
  const SlotMeas& m = hal.m(s_);
  uint32_t now = millis();
  o["slot"] = s_ + 1;
  o["phase"] = phaseId(phase_);
  o["error"] = error_;
  o["lastResult"] = lastResultId_;
  if (phase_ != Phase::Idle) {
    JsonObject j = o["job"].to<JsonObject>();
    j["port"] = "S" + String(s_ + 1);
    j["name"] = job_.name;
    j["cell"] = job_.cellNo;
    j["cellId"] = job_.cellId;
    j["chem"] = "liion";
    j["preset"] = job_.presetId;
    j["cells"] = 1;
    j["currentA"] = job_.currentA;
    j["nomCapMah"] = job_.nomCapMah;
    j["cutoffV"] = job_.cutoffV;
    j["preCharge"] = job_.preCharge;
    j["restMin"] = job_.restMin;
    j["after"] = job_.after == After::Full ? "full" : job_.after == After::Storage ? "storage" : "none";
    j["cycles"] = job_.cycles;
    o["cycle"] = cycleIdx_ + 1;
    o["elapsed"] = (now - startMs_) / 1000;
    o["phaseElapsed"] = (now - phaseMs_) / 1000;
    if (phase_ == Phase::Rest) o["phaseTotal"] = uint32_t(job_.restMin) * 60;
    if (phase_ == Phase::RestAfter) o["phaseTotal"] = REST_AFTER_MS / 1000;
    o["mah"] = mah();
    o["wh"] = isDischarge(phase_) ? energyWh_ : float(m.energyWs / 3600.0);
    o["capMah"] = capMah_;
    o["riLast"] = lastRi_;
    o["riPulse"] = riStage_ != 0;
    o["throttle"] = isCharge(phase_) ? chgF_ : loadF_;
    o["throttled"] = throttled_;
    o["saturated"] = hal.loadSaturated(s_);
    uint32_t pe = phaseEta(now);
    if (pe) o["eta"] = pe;
    uint32_t te = totalEta(now);
    if (te) o["etaTotal"] = te;
  }
  o["soc"] = roundf(socPercent());
  if (active() && job_.noTemp) o["noTemp"] = true;
  if (lastScore_ >= 0) {
    JsonObject l = o["last"].to<JsonObject>();
    l["cellId"] = lastCellId_;
    l["cap"] = roundf(lastCap_);
    l["score"] = lastScore_;
    l["grade"] = String(soh::grade(lastScore_));
  }
  JsonArray ports = o["ports"].to<JsonArray>();
  JsonObject p = ports.add<JsonObject>();
  p["ok"] = m.ok;
  p["v"] = roundf(m.v * 1000) / 1000;
  p["i"] = roundf(m.i * 1000) / 1000;
  p["p"] = roundf(m.p * 100) / 100;
  JsonObject p2 = ports.add<JsonObject>();
  p2["ok"] = false;
  p2["v"] = 0;
  p2["i"] = 0;
  p2["p"] = 0;
  o["loadPort"] = "A";
  o["loadTarget"] = hal.loadTarget(s_);
  JsonObject t = o["temps"].to<JsonObject>();
  if (!isnan(m.tCell)) t["cell"] = m.tCell;
  if (!isnan(hal.tSink())) t["sink"] = hal.tSink();
  JsonObject ch = o["charger"].to<JsonObject>();
  ch["mode"] = hal.charger(s_) ? "4.2" : "off";
  if (hal.chargerWaiting(s_)) ch["waiting"] = true;
  ch["charging"] = hal.charger(s_) && m.i > 0.05f;
  ch["full"] = false;
}

void Slot::curveJson(JsonArray a) const {
  for (int k = 0; k < nPts_; k++) {
    JsonArray p = a.add<JsonArray>();
    p.add(pts_[k].t);
    p.add(roundf(pts_[k].v * 1000) / 1000);
    p.add(roundf(pts_[k].i * 1000) / 1000);
    p.add(roundf(pts_[k].qmAh));
    p.add(isnan(pts_[k].temp) ? 0 : roundf(pts_[k].temp * 10) / 10);
  }
}
