// CellDoc Mini – Hardware: Messung, Last, Lader je Schacht, Temperaturen, Taster
#pragma once
#include <Arduino.h>
#include "config_mini.h"

struct SlotMeas {
  bool ok = false;
  float v = 0;          // V am Zellpol (SENSE)
  float i = 0;          // A, positiv = Laden
  float p = 0;
  double chargeAs = 0;  // seit resetCounters
  double energyWs = 0;
  uint32_t seq = 0;
  float tCell = NAN;
};

enum class BtnEvent : uint8_t { None, Prev, Next, Ok, OkLong };

class HalMini {
 public:
  void begin();
  void update(uint32_t now);

  const SlotMeas& m(uint8_t s) const { return slot_[s]; }
  float tSink() const { return tSink_; }
  bool dacOk() const { return dacOk_; }
  bool inaOk(uint8_t s) const { return slot_[s].ok; }
  uint8_t fanDuty() const { return fanDuty_; }

  void setLoad(uint8_t s, float amps);
  float loadTarget(uint8_t s) const { return target_[s]; }
  bool loadSaturated(uint8_t s) const { return saturated_[s]; }

  void setCharger(uint8_t s, bool on);
  bool charger(uint8_t s) const { return chg_[s]; }
  // Begrenzung gleichzeitig ladender Schächte (Netzteil): Lader wartet, bis ein Platz frei ist
  bool chargerWaiting(uint8_t s) const { return chg_[s] && !grant_[s]; }
  void setChargerDuty(uint8_t s, float d);

  void resetCounters(uint8_t s);
  void kill(uint8_t s);
  void killAll();

  void beep(uint16_t ms);
  void alarm();
  BtnEvent button();  // ein Ereignis pro Aufruf

  struct SensorInfo { uint8_t addr[8]; float t; int role; };
  int tempSensors(SensorInfo* out, int max);
  void rescanTempSensors();

 private:
  void readIna(uint32_t now);
  void readTemps(uint32_t now);
  void control(uint32_t now);
  void fan(uint32_t now);
  void buttons(uint32_t now);

  SlotMeas slot_[4];
  float tSink_ = NAN;
  bool dacOk_ = false;
  float target_[4] = {};
  float code_[4] = {};
  bool saturated_[4] = {};
  bool chg_[4] = {};
  bool grant_[4] = {};
  uint32_t chgSince_[4] = {};
  void grantChargers();
  float chgDuty_[4] = {1, 1, 1, 1};
  uint32_t lastSeq_[4] = {};
  uint32_t lastIna_ = 0, lastInaMicros_ = 0, lastTemp_ = 0, lastFan_ = 0;
  bool tempRequested_ = false;
  uint8_t fanDuty_ = 0;
  uint32_t beepUntil_ = 0, alarmNext_ = 0;
  uint8_t alarmBeeps_ = 0;
  // Taster
  uint8_t btnState_ = 0, btnStable_ = 0;
  uint32_t btnChanged_ = 0, okDown_ = 0;
  bool okLongSent_ = false;
  BtnEvent pending_ = BtnEvent::None;
};

extern HalMini hal;
