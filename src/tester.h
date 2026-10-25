// CellDoc – Testablauf, manuelle Betriebsarten und Sicherheitsüberwachung
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "hal.h"
#include "profiles.h"

enum class Phase : uint8_t {
  Idle,
  PreCharge,     // Test: vorher voll laden
  Rest,          // Test: ruhen lassen
  Discharge,     // Test: Konstantstrom-Entladung mit Ri-Pulsen
  RestAfter,     // Test: Erholung nach Entladung
  Recharge,      // Test: wieder laden (voll oder Lagerspannung)
  Done,
  Error,
  ManualCharge,
  ManualDischarge,
};

enum class After : uint8_t { None, Full, Storage };

struct JobConfig {
  Port port = PORT_A;
  const Chemistry* chem = nullptr;
  uint8_t cells = 1;
  String name;
  String presetId;
  uint32_t cellNo = 0;   // Zelle im Bestand, 0 = neu anlegen (nur Test)
  String cellId;         // z. B. CD-0042
  float nomCapMah = 0;   // 0 = unbekannt
  float riNewMilli = 0;  // 0 = unbekannt
  float currentA = 0.5f;
  float cutoffV = 0;     // pro Zelle, 0 = aus Chemie
  bool preCharge = true;
  uint16_t restMin = 30;
  After after = After::Full;
  uint8_t cycles = 1;    // Zyklentest: so oft laden und entladen (1–10)
};

struct CycleRec {
  float cap, wh, ri, chg;  // Entnommen mAh, Wh, Ri bei 50 % (mΩ), vorher geladen mAh
};

#include "curve.h"

struct RiSample {
  float qmAh;
  float riMilli;
  float cellRi[6];
};

class Tester {
 public:
  void begin();
  void update(uint32_t now);

  // Rückgabe: leer = ok, sonst Fehlermeldung für die Weboberfläche
  String startTest(const JobConfig& cfg);
  String startManualCharge(const JobConfig& cfg);
  String startManualDischarge(const JobConfig& cfg);
  void stop(const char* reason = nullptr);
  void acknowledge();  // Fehler/Fertig quittieren

  Phase phase() const { return phase_; }
  bool active() const;
  uint32_t cellNo() const { return job_.cellNo; }
  void stateJson(JsonObject o) const;
  void curveJson(JsonArray a) const;

 private:
  String precheck(const JobConfig& c, bool charge, bool discharge);
  void enter(Phase p);
  void fail(const String& reason);
  void finishTest();
  void safety(uint32_t now);
  float thermalFactor(float t, float warn, float stop) const;
  void logPoint(uint32_t now, bool force = false);
  void handleDischarge(uint32_t now);
  void handleCharge(uint32_t now, bool isTest);
  void startRiPulse(uint32_t now);
  void handleRiPulse(uint32_t now);
  float cellTemp() const;
  float perCellV() const;
  float minCellV() const;
  float socPercent() const;
  float testMah() const;  // Entnommene Ladung im aktuellen Abschnitt (positiv)

  Phase phase_ = Phase::Idle;
  JobConfig job_;
  String error_;
  uint32_t startMs_ = 0, phaseMs_ = 0;
  float loadFactor_ = 1, chgFactor_ = 1;
  bool throttled_ = false, saturatedSeen_ = false;
  uint32_t cutoffSince_ = 0, fullSince_ = 0, ovSince_ = 0, ocSince_ = 0, fanBadSince_ = 0;

  // Messwerte des Tests
  float preChargeMah_ = 0;
  float startOcv_ = 0, restOcv_ = 0;
  float capMah_ = 0, energyWh_ = 0;
  float rechargeMah_ = 0, rechargeWh_ = 0;
  uint32_t rechargeSec_ = 0;
  float tempStart_ = NAN, tempMax_ = NAN;
  float storageTargetMah_ = 0;

  // Ri-Pulse
  uint8_t riStage_ = 0;
  uint32_t riT0_ = 0;
  float riBase_ = 0, riDelta_ = 0;
  double riSumV1_ = 0, riSumI1_ = 0, riCell1_[6] = {};
  int riN1_ = 0;
  uint32_t riSeq_ = 0;
  float nextRiMah_ = 0, riStepMah_ = 150;
  static constexpr int MAX_RI = 48;
  RiSample ri_[MAX_RI];
  int riCount_ = 0;
  float lastRi_ = 0;

  // Kurve
  static constexpr int MAX_POINTS = 600;
  CurvePoint pts_[MAX_POINTS];
  int nPts_ = 0;
  uint32_t ptInterval_ = 10, lastPtSec_ = 0;
  bool ptOpen_ = false;

  uint32_t lastResultId_ = 0;
  const Chemistry* idleChem_ = nullptr;
  // letztes Ergebnis für Anzeige und MQTT
  String lastCellId_;
  float lastCap_ = 0;
  int lastScore_ = -1;
  // Zyklen
  uint8_t cycleIdx_ = 0, nCyc_ = 0;
  bool midRecharge_ = false;
  CycleRec cyc_[10];
  float riAt50() const;
  void recordCycle(float chgMah);
  float lastChgMah_ = 0;
  // 1 h nach dem Test Ruhespannung merken (Spannungscheck), nach Vollladen auch Selbstentladung starten
  uint32_t sdArmAt_ = 0;
  bool sdStartToo_ = false;
  void checkSelfDischarge(uint32_t now);
  String linkCell(JobConfig& c, bool create);
};

extern Tester tester;

const char* phaseId(Phase p);
