// CellDoc – Hardware: Messung, Last, Lader, Relais, Lüfter
#pragma once
#include <Arduino.h>
#include "profiles.h"

enum Port : uint8_t { PORT_A = 0, PORT_B = 1 };

struct PortMeas {
  bool ok = false;     // INA226 antwortet
  float v = 0;         // Spannung am Akku (V)
  float i = 0;         // Strom (A), positiv = Laden, negativ = Entladen
  float p = 0;         // Leistung (W), Vorzeichen wie i
  double chargeAs = 0; // aufsummierte Ladung (As), Vorzeichen wie i
  double energyWs = 0; // aufsummierte Energie (Ws)
  uint32_t seq = 0;    // Zähler neuer Messwerte
};

struct Meas {
  PortMeas port[2];
  float tCell = NAN, tPack = NAN, tSink = NAN;
  float tap[6] = {};       // Balancer-Abgriffe (V, gegen Minus)
  float cell[6] = {};      // Einzelzellen (V)
  uint8_t balCells = 0;    // erkannte Zellen am Balancer
  bool balOk = false;
  bool dacOk = false;
  int fanRpm = 0;
  uint8_t fanDuty = 0;     // 0..100 %
  bool chgCharging = false, chgFull = false;
};

struct TempSensorInfo {
  uint8_t addr[8];
  float t;
  int role;  // -1 = nicht zugeordnet
};

class Hal {
 public:
  void begin();
  void update(uint32_t now);

  const Meas& m() const { return m_; }

  // Last: Strom in A (Entladen, positiv). 0 = aus. Wechselt der Anschluss,
  // wird erst abgeschaltet, dann umgeschaltet.
  void setLoad(Port port, float amps);
  float loadTarget() const { return loadTarget_; }
  Port loadPort() const { return loadPort_; }
  bool loadSaturated() const { return saturated_; }

  // Lader an Anschluss A
  void setCharger(ChargerMode mode);
  ChargerMode charger() const { return chgMode_; }
  void setChargerDuty(float duty01);  // Drosselung im Takt, 1 = Dauerbetrieb

  void setFanBoost(bool on) { fanBoost_ = on; }
  void beep(uint16_t ms);
  void alarm();

  // Alles sofort aus (Not-Aus, Fehler)
  void killAll();

  // Diagnose
  int tempSensors(TempSensorInfo* out, int max);
  void rescanTempSensors();
  bool inaOk(Port p) const;

  // Zähler für Ladung/Energie eines Anschlusses zurücksetzen
  void resetCounters(Port p);

 private:
  void readIna(uint32_t now);
  void readBalancer(uint32_t now);
  void readTemps(uint32_t now);
  void controlLoad(uint32_t now);
  void controlFan(uint32_t now);
  void applyChargerPins(bool active);
  void writeDac(float code);

  Meas m_;
  float loadTarget_ = 0;
  Port loadPort_ = PORT_A;
  Port pendingPort_ = PORT_A;
  float pendingTarget_ = 0;
  uint32_t switchAt_ = 0;
  uint8_t switchStage_ = 0;
  float dacCode_ = 0;
  bool saturated_ = false;
  ChargerMode chgMode_ = ChargerMode::Off;
  float chgDuty_ = 1;
  bool fanBoost_ = false;
  uint32_t lastIna_ = 0, lastInaMicros_ = 0, lastTemp_ = 0, lastBal_ = 0, lastFan_ = 0;
  uint8_t balStep_ = 0;
  bool balStarted_ = false;
  bool tempRequested_ = false;
  uint32_t beepUntil_ = 0;
  uint8_t alarmBeeps_ = 0;
  uint32_t alarmNext_ = 0;
};

extern Hal hal;
