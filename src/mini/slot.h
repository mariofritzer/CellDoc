// CellDoc Mini – Testablauf und Sicherheitsüberwachung je Schacht
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "curve.h"
#include "hal_mini.h"
#include "profiles.h"

enum class Phase : uint8_t { Idle, PreCharge, Rest, Discharge, RestAfter, Recharge, Done, Error, ManualCharge, ManualDischarge };
enum class After : uint8_t { None, Full, Storage };

struct SlotJob {
  const Chemistry* chem = nullptr;
  String name, presetId;
  float nomCapMah = 0, riNewMilli = 0;
  float currentA = 0.5f, cutoffV = 0;
  bool preCharge = true;
  uint16_t restMin = 30;
  After after = After::Storage;
};

class Slot {
 public:
  void begin(uint8_t idx);
  void update(uint32_t now);
  String startTest(const SlotJob& j);
  String startCharge(const SlotJob& j);
  String startDischarge(const SlotJob& j);
  void stop(const char* reason);
  void acknowledge();

  Phase phase() const { return phase_; }
  bool active() const;
  float socPercent() const;
  float mah() const;
  uint32_t lastResult() const { return lastResultId_; }
  int lastScore() const { return lastScore_; }
  const String& error() const { return error_; }
  const SlotJob& job() const { return job_; }
  void stateJson(JsonObject o) const;
  void curveJson(JsonArray a) const;

 private:
  String precheck(const SlotJob& j, bool charge, bool discharge) const;
  void enter(Phase p);
  void fail(const String& why);
  void safety(uint32_t now);
  void handleCharge(uint32_t now);
  void handleDischarge(uint32_t now);
  void startRi(uint32_t now);
  void handleRi(uint32_t now);
  void logPoint(uint32_t now, bool force = false);
  void finish();
  float thermal(float t, float warn, float stop) const;

  uint8_t s_ = 0;
  Phase phase_ = Phase::Idle;
  SlotJob job_;
  String error_;
  uint32_t startMs_ = 0, phaseMs_ = 0;
  float loadF_ = 1, chgF_ = 1;
  bool throttled_ = false, saturatedSeen_ = false;
  uint32_t cutoffSince_ = 0, fullSince_ = 0, ovSince_ = 0, ocSince_ = 0;
  float preMah_ = 0, startOcv_ = 0, restOcv_ = 0, capMah_ = 0, energyWh_ = 0;
  float reMah_ = 0, reWh_ = 0, storageMah_ = 0;
  uint32_t reSec_ = 0;
  float tStart_ = NAN, tMax_ = NAN;
  // Innenwiderstand
  uint8_t riStage_ = 0;
  uint32_t riT0_ = 0, riSeq_ = 0;
  float riBase_ = 0, riDelta_ = 0, nextRiMah_ = 0, riStep_ = 150, lastRi_ = 0;
  double riV1_ = 0, riI1_ = 0;
  int riN1_ = 0;
  static constexpr int MAX_RI = 40;
  float riQ_[MAX_RI], riR_[MAX_RI];
  int riCount_ = 0;
  // Kurve
  static constexpr int MAX_POINTS = 400;
  CurvePoint pts_[MAX_POINTS];
  int nPts_ = 0;
  uint32_t ptInterval_ = 10, lastPtSec_ = 0;
  uint32_t lastResultId_ = 0;
  int lastScore_ = -1;
};

extern Slot slots[4];
const char* phaseId(Phase p);
