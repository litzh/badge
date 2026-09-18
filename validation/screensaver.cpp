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
  // A maximum of five minutes of animation after the ten-second status page.
  s.activity(1000);
  assert(s.tick(11000) && s.active);
  assert(!s.tick(310999) && !s.screenOff);
  assert(s.tick(311000) && s.screenOff && !s.active);
  assert(s.effectiveBrightness(255) == 0);
  assert(!s.tick(500000) && s.screenOff); // polling doesn't wake a sleeping panel
  s.activity(500001);
  assert(!s.screenOff && !s.active && s.effectiveBrightness(255) == 255);
  // Low battery skips animation, and can stop an already-running screensaver.
  assert(!s.tick(510000, true));
  assert(s.tick(510001, true) && s.screenOff);
  s.activity(600000);
  assert(s.tick(610000) && s.active);
  assert(s.tick(610001, true) && s.screenOff);
  // Late loop: go straight to off instead of starting a fresh five-minute show.
  s.activity(0);
  assert(s.tick(400000) && s.screenOff);
  s.activity(UINT32_MAX - 100000);
  assert(s.tick(209999) && s.screenOff);
}
