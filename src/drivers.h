// Schlanke Treiber für INA226, ADS1115 und MCP4725 (nur was CellDoc braucht)
#pragma once
#include <Arduino.h>
#include <Wire.h>

namespace i2c {
inline bool present(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}
inline bool write16(uint8_t addr, uint8_t reg, uint16_t v) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(uint8_t(v >> 8));
  Wire.write(uint8_t(v & 0xFF));
  return Wire.endTransmission() == 0;
}
inline bool read16(uint8_t addr, uint8_t reg, uint16_t& out) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(int(addr), 2) != 2) return false;
  out = (uint16_t(Wire.read()) << 8) | Wire.read();
  return true;
}
}  // namespace i2c

// ---------------------------------------------------------------------------
// INA226: Shunt- und Busspannung im Dauerbetrieb, Mittelung 16, je 1,1 ms
// Strom wird selbst aus der Shuntspannung berechnet (kein Kalibrierregister nötig)
class INA226 {
 public:
  bool begin(uint8_t addr) {
    addr_ = addr;
    ok_ = i2c::present(addr) && i2c::write16(addr, 0x00, 0x4527);
    return ok_;
  }
  // Shuntspannung in Volt (Vorzeichen: IN+ -> IN- positiv)
  bool read(float& shuntV, float& busV) {
    uint16_t s, b;
    if (!i2c::read16(addr_, 0x01, s) || !i2c::read16(addr_, 0x02, b)) {
      ok_ = false;
      return false;
    }
    ok_ = true;
    shuntV = int16_t(s) * 2.5e-6f;
    busV = b * 1.25e-3f;
    return true;
  }
  bool ok() const { return ok_; }

 private:
  uint8_t addr_ = 0;
  bool ok_ = false;
};

// ---------------------------------------------------------------------------
// ADS1115: Einzelmessungen nacheinander, nicht blockierend
class ADS1115 {
 public:
  bool begin(uint8_t addr) {
    addr_ = addr;
    ok_ = i2c::present(addr);
    return ok_;
  }
  // Startet eine Messung an Kanal 0..3 (±4,096 V, 128 SPS)
  bool start(uint8_t ch) {
    uint16_t cfgv = 0x8000 | (uint16_t(4 + ch) << 12) | (1 << 9) | (1 << 8) | (4 << 5) | 3;
    ok_ = i2c::write16(addr_, 0x01, cfgv);
    return ok_;
  }
  // Ergebnis in Volt am Eingang
  bool result(float& v) {
    uint16_t r;
    if (!i2c::read16(addr_, 0x00, r)) {
      ok_ = false;
      return false;
    }
    v = int16_t(r) * 0.125e-3f;
    return true;
  }
  bool ok() const { return ok_; }

 private:
  uint8_t addr_ = 0;
  bool ok_ = false;
};

// ---------------------------------------------------------------------------
// MCP4728: 4 × 12-Bit-DAC, Referenz VDD, Verstärkung 1, alle Kanäle per Fast-Write
class MCP4728 {
 public:
  bool begin(uint8_t addr) {
    addr_ = addr;
    ok_ = i2c::present(addr);
    if (!ok_) return false;
    Wire.beginTransmission(addr_);
    Wire.write(0x80);  // VREF: alle Kanäle VDD
    Wire.endTransmission();
    Wire.beginTransmission(addr_);
    Wire.write(0xC0);  // Gain: alle ×1
    Wire.endTransmission();
    for (int k = 0; k < 4; k++) code_[k] = 0;
    return flush();
  }
  void set(uint8_t ch, uint16_t code) {
    if (code > 4095) code = 4095;
    if (ch < 4) code_[ch] = code;
  }
  bool flush() {
    Wire.beginTransmission(addr_);
    for (int k = 0; k < 4; k++) {
      Wire.write(uint8_t((code_[k] >> 8) & 0x0F));
      Wire.write(uint8_t(code_[k] & 0xFF));
    }
    ok_ = Wire.endTransmission() == 0;
    return ok_;
  }
  bool ok() const { return ok_; }

 private:
  uint8_t addr_ = 0;
  uint16_t code_[4] = {};
  bool ok_ = false;
};

// ---------------------------------------------------------------------------
// MCP4725: 12-Bit-DAC, Fast-Write
class MCP4725 {
 public:
  bool begin(uint8_t addr) {
    addr_ = addr;
    ok_ = i2c::present(addr);
    if (ok_) set(0);
    return ok_;
  }
  bool set(uint16_t code) {
    if (code > 4095) code = 4095;
    Wire.beginTransmission(addr_);
    Wire.write(uint8_t((code >> 8) & 0x0F));
    Wire.write(uint8_t(code & 0xFF));
    ok_ = Wire.endTransmission() == 0;
    code_ = code;
    return ok_;
  }
  uint16_t code() const { return code_; }
  bool ok() const { return ok_; }

 private:
  uint8_t addr_ = 0;
  uint16_t code_ = 0;
  bool ok_ = false;
};
