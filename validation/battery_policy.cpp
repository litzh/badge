#include "../firmware/badge/battery_policy.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <initializer_list>

BatterySample reading(uint32_t at, int mv = 3400, int soc = 10, bool usb = false) {
  BatterySample s;
  s.valid = s.powerValid = s.present = true;
  s.millivolts = mv;
  s.percent = soc;
  s.vbus = usb;
  s.sampledAt = at;
  return s;
}
void feed(BatteryPolicy &p, uint32_t now, int mv, int soc = 10, bool usb = false) {
  p.update(reading(now, mv, soc, usb), now);
}

int main() {
  BatteryPolicy p;
  for (uint32_t t = 0; t < 15000; t += 5000) {
    feed(p, t, 3490, 16);
    assert(!p.low && !p.shutdownDue);
  }
  feed(p, 15000, 3490, 16);
  assert(p.low && !p.cutoffPending);
  // Hysteresis: recovered voltage alone doesn't clear a genuinely low SOC.
  for (uint32_t t = 20000; t <= 55000; t += 5000) feed(p, t, 3620, 15);
  assert(p.low);
  for (uint32_t t = 60000; t < 90000; t += 5000) { feed(p, t, 3620, 22); assert(p.low); }
  feed(p, 90000, 3620, 22);
  assert(!p.low);
  // Even 0% doesn't cause shutdown at healthy voltage.
  p = {};
  for (uint32_t t = 0; t <= 60000; t += 5000) feed(p, t, 3750, 0);
  assert(p.low && !p.shutdownDue);
  // Cutoff uses independent voltage evidence even when SOC is wrong or missing.
  for (int soc : {-1, 0, 90}) {
    p = {};
    for (uint32_t t = 0; t < 15000; t += 5000) {
      feed(p, t, 3350, soc);
      assert(p.cutoffPending && !p.shutdownDue);
    }
    feed(p, 15000, 3350, soc);
    assert(p.shutdownDue);
    feed(p, 20000, 3300, soc, true);
    assert(!p.shutdownDue && !p.low && !p.cutoffPending);
  }
  // Don't accumulate samples while on external power; unplug starts from zero.
  p = {};
  for (uint32_t t = 0; t <= 60000; t += 5000) feed(p, t, 3000, 0, true);
  feed(p, 65000, 3300, 0);
  assert(!p.shutdownDue);
  feed(p, 70000, 3351, 0);
  assert(!p.cutoffPending);
  // One old reading never becomes sustained evidence through frequent polling.
  p = {};
  auto s = reading(100, 3300, 1);
  for (uint32_t now = 100; now <= 20100; ++now) {
    p.update(s, now);
    assert(!p.shutdownDue);
  }
  // Gaps, failed reads, external supply and absent battery cancel pending cutoff.
  for (int mode = 0; mode < 5; ++mode) {
    p = {};
    feed(p, 0, 3300); feed(p, 5000, 3300); feed(p, 10000, 3300);
    s = reading(15000, 3300, 1);
    if (mode == 0) s.valid = false;
    if (mode == 1) s.powerValid = false;
    if (mode == 2) s.present = false;
    if (mode == 3) s.vbus = true;
    if (mode == 4) s.millivolts = 0;
    p.update(s, 15000);
    assert(!p.shutdownDue && !p.cutoffPending);
    feed(p, 20000, 3300);
    assert(!p.shutdownDue);
  }
  p = {};
  feed(p, 0, 3300); feed(p, 5000, 3300); feed(p, 10000, 3300);
  feed(p, 25000, 3300); // Missing ADC updates reset the confirmation window.
  assert(!p.shutdownDue);
  // Millisecond wrap must neither fire early nor prevent a valid shutdown.
  p = {};
  uint32_t start = UINT32_MAX - 9999;
  for (uint32_t i = 0; i < 15000; i += 5000) { feed(p, start + i, 3300); assert(!p.shutdownDue); }
  feed(p, start + 15000, 3300);
  assert(p.shutdownDue);
  puts("Battery policy: low/recovery, sustained cutoff, USB, stale/invalid/gapped samples, wrap OK");
}
