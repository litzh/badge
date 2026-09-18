#pragma once
#include <stdint.h>

struct BatterySample {
  bool valid = false, powerValid = false, present = false, vbus = false;
  uint16_t millivolts = 0;
  int percent = -1;
  int direction = -1, chargerStatus = -1;
  uint32_t sampledAt = 0;
};

// Initial conservative policy derived from the September 17 discharge trace.
// SOC is display/advisory data; only fresh, sustained voltage can power off.
struct BatteryPolicy {
  static constexpr uint16_t LOW_MV = 3500, RECOVER_MV = 3600, CUTOFF_MV = 3350;
  static constexpr uint32_t CONFIRM_MS = 15000, RECOVER_MS = 30000, MAX_AGE_MS = 7500;
  bool low = false, cutoffPending = false, shutdownDue = false;
  bool haveSample = false, lowPending = false, recoveryPending = false;
  uint32_t lastSample = 0, lowSince = 0, cutoffSince = 0, recoverySince = 0;

  void resetPending() {
    lowPending = recoveryPending = cutoffPending = shutdownDue = false;
  }
  void update(const BatterySample &s, uint32_t now) {
    if (!s.powerValid || uint32_t(now - s.sampledAt) > MAX_AGE_MS) {
      resetPending();
      return; // Keep an existing low warning through temporary telemetry loss.
    }
    if (s.vbus || !s.present) {
      low = false;
      resetPending();
      haveSample = false;
      return;
    }
    if (!s.valid || s.millivolts < 2000 || s.millivolts > 5000) {
      resetPending();
      return;
    }
    if (haveSample && s.sampledAt == lastSample)
      return; // Re-reading one ADC sample cannot advance protection timers.
    if (haveSample && uint32_t(s.sampledAt - lastSample) > MAX_AGE_MS)
      resetPending();
    haveSample = true;
    lastSample = s.sampledAt;
    bool lowReading = s.millivolts <= LOW_MV || (s.percent >= 0 && s.percent <= 15);
    if (lowReading) {
      recoveryPending = false;
      if (!lowPending) { lowPending = true; lowSince = s.sampledAt; }
      if (uint32_t(s.sampledAt - lowSince) >= CONFIRM_MS) low = true;
    } else {
      lowPending = false;
      if (s.millivolts >= RECOVER_MV && (s.percent < 0 || s.percent >= 20)) {
        if (!recoveryPending) { recoveryPending = true; recoverySince = s.sampledAt; }
        if (uint32_t(s.sampledAt - recoverySince) >= RECOVER_MS) low = false;
      } else recoveryPending = false;
    }
    if (s.millivolts <= CUTOFF_MV) {
      if (!cutoffPending) { cutoffPending = true; cutoffSince = s.sampledAt; }
      shutdownDue = uint32_t(s.sampledAt - cutoffSince) >= CONFIRM_MS;
    } else {
      cutoffPending = shutdownDue = false;
    }
  }
  bool conserve() const { return low || cutoffPending; }
};
