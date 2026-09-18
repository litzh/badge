// Compile with -std=c++17 -Ivalidation/time_sync_stubs; no device or network used.
#include <atomic>
#include <cassert>
#include <iostream>
#include <time.h>
#include <sys/time.h>
static time_t fakeEpoch = 0;
static time_t fakeTime(time_t *out) {
  if (out) *out = fakeEpoch;
  return fakeEpoch;
}
#define time fakeTime
#include "../firmware/badge/time_sync.cpp"
#undef time

static std::string field(const char *key) {
  auto *j = timeSyncStatus();
  auto value = j->fields.at(key);
  delete j;
  return value;
}
int main() {
  timeSyncSetup();
  assert(configuredInterval == 3600000 && syncCallback);
  assert(!timeSyncTick(false) && ntpStarts == 0);
  assert(field("time") == "null" && field("time_valid") == "false");
  assert(timeSyncDisplay() == "Time: waiting for Wi-Fi");
  assert(!timeSyncTick(true) && ntpStarts == 1);
  assert(primaryServer == "ntp1.aliyun.com" && backupServer == "ntp1.tencent.com");
  assert(timeSyncDisplay() == "Time: syncing...");
  fakeMillis = 59999;
  timeSyncTick(true);
  assert(ntpStarts == 1);
  ++fakeMillis;
  timeSyncTick(true);
  assert(ntpStarts == 2 && primaryServer == "ntp1.tencent.com");
  assert(field("status") == "retrying" && field("last_error") == "ntp_timeout");

  tm utc = {};
  utc.tm_year = 126; utc.tm_mon = 8; utc.tm_mday = 17; utc.tm_hour = 16; utc.tm_min = 30;
  fakeEpoch = timegm(&utc);
  assert(field("time") == "null"); // A plausible clock alone does not establish NTP success.
  syncCallback(nullptr);
  assert(field("time") == "null"); // Callback only notifies; UI/JSON state belongs to loop.
  assert(timeSyncTick(true));
  assert(field("status") == "synced" && field("last_error") == "null");
  assert(field("time") == "2026-09-18T00:30:00+08:00");
  assert(field("last_ntp_sync") == "2026-09-17T16:30:00Z");
  assert(timeSyncDisplay() == "2026-09-18 00:30:00");
  assert(!timeSyncTick(false));
  fakeEpoch += 75;
  assert(field("status") == "waiting_for_wifi" && field("time_valid") == "true");
  assert(timeSyncDisplay() == "2026-09-18 00:31:15");
  timeSyncTick(true);
  assert(ntpStarts == 3 && field("status") == "syncing");
  fakeMillis += 60000;
  timeSyncTick(true);
  assert(ntpStarts == 4 && field("time_valid") == "true"); // Failed resync retains clock.
  syncCallback(nullptr);
  assert(timeSyncTick(true));
  fakeMillis += 3600000;
  timeSyncTick(true);
  assert(ntpStarts == 5);
  auto lastGood = fakeEpoch;
  fakeEpoch = 0;
  syncCallback(nullptr);
  assert(!timeSyncTick(true));
  assert(field("time") == "null" && field("last_error") == "ntp_time_out_of_range");
  fakeMillis += 60000;
  timeSyncTick(true);
  assert(ntpStarts == 6);
  fakeEpoch = lastGood;
  syncCallback(nullptr);
  assert(timeSyncTick(true));
  assert(field("time_valid") == "true" && field("last_error") == "null");
  fakeMillis = UINT32_MAX - 1000;
  syncCallback(nullptr);
  timeSyncTick(true);
  timeSyncTick(false);
  timeSyncTick(true);
  auto beforeWrap = ntpStarts;
  fakeMillis += 60000;
  timeSyncTick(true);
  assert(ntpStarts == beforeWrap + 1);
  std::cout << "PASS initial unknown time, NTP timeout/failover, Beijing date rollover, offline clock, reconnect, hourly sync, invalid NTP, millis wrap\n";
}
