#pragma once
#include <stdint.h>

// Pure timer state; polling telemetry must never call activity().
struct ScreensaverState {
  static constexpr uint32_t TIMEOUT_MS = 10000;
  // 24 made the sparse particle scene appear off on the actual AMOLED.
  static constexpr uint8_t BRIGHTNESS_LIMIT = 128;
  uint32_t lastActivity = 0;
  bool active = false;

  void activity(uint32_t now) {
    lastActivity = now;
    active = false;
  }
  bool tick(uint32_t now) {
    if (active || uint32_t(now - lastActivity) < TIMEOUT_MS)
      return false;
    active = true;
    return true;
  }
  uint8_t effectiveBrightness(uint8_t normal) const {
    return active && normal > BRIGHTNESS_LIMIT ? BRIGHTNESS_LIMIT : normal;
  }
};
