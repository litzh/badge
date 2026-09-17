#include "../firmware/badge/screensaver_state.h"
#include <assert.h>
#include <stdint.h>

int main() {
  ScreensaverState s;
  s.activity(1234);
  assert(!s.tick(11233));
  assert(s.tick(11234) && s.active);
  assert(s.effectiveBrightness(160) == 128);
  assert(s.effectiveBrightness(255) == 128);
  assert(s.effectiveBrightness(64) == 64);
  assert(s.effectiveBrightness(0) == 0);
  assert(s.effectiveBrightness(12) == 12);
  // Telemetry reads and repeated ticks neither wake nor restart the timer.
  assert(!s.tick(12000) && s.active && s.lastActivity == 1234);
  s.activity(13000);
  assert(!s.active && s.effectiveBrightness(160) == 160);
  assert(!s.tick(22999));
  assert(s.tick(23000));
  // millis() wraps approximately every 49 days.
  s.activity(UINT32_MAX - 4999);
  assert(!s.tick(4999));
  assert(s.tick(5000));
  s.activity(5001);
  assert(!s.active);
}
