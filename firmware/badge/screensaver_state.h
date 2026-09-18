#pragma once
#include <stdint.h>

// Pure timer state; polling telemetry must never call activity().
struct ScreensaverState {
  static constexpr uint32_t TIMEOUT_MS = 10000;
  static constexpr uint32_t MAX_DISPLAY_MS = 300000;
  // 24 made the sparse particle scene appear off on the actual AMOLED.
  static constexpr uint8_t BRIGHTNESS_LIMIT = 128;
  uint32_t lastActivity = 0;
  bool active = false;
  bool screenOff = false;

  void activity(uint32_t now) {
    lastActivity = now;
    active = false;
    screenOff = false;
  }
  bool tick(uint32_t now, bool lowBattery = false) {
    if (screenOff) return false;
    uint32_t idle = uint32_t(now - lastActivity);
    if (idle >= TIMEOUT_MS + MAX_DISPLAY_MS || (lowBattery && (active || idle >= TIMEOUT_MS))) {
      active = false;
      screenOff = true;
      return true;
    }
    if (active || uint32_t(now - lastActivity) < TIMEOUT_MS)
      return false;
    active = true;
    return true;
  }
  uint8_t effectiveBrightness(uint8_t normal) const {
    if (screenOff) return 0;
    return active && normal > BRIGHTNESS_LIMIT ? BRIGHTNESS_LIMIT : normal;
  }
};
