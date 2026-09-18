#include "time_sync.h"
#include <atomic>
#include <esp_sntp.h>
#include <stdlib.h>
#include <time.h>

namespace {
constexpr const char *servers[] = {"ntp1.aliyun.com", "ntp1.tencent.com"};
constexpr uint32_t syncIntervalMs = 3600000, retryMs = 60000;
std::atomic<bool> ntpReceived{false};
bool wasConnected = false, awaitingNtp = false, hasTime = false;
unsigned firstServer = 0;
uint32_t attemptAt = 0, syncedAt = 0;
time_t lastNtp = 0;
const char *lastError = nullptr;

bool validLocal(time_t epoch, tm &local) {
  return localtime_r(&epoch, &local) && local.tm_year >= 120 && local.tm_year <= 199;
}
void startNtp(uint32_t now) {
  // Core helper locks lwIP and starts SNTP asynchronously; no getLocalTime wait loop.
  configTzTime("CST-8", servers[firstServer], servers[1 - firstServer]);
  awaitingNtp = true;
  attemptAt = now;
}
} // namespace

void timeSyncSetup() {
  setenv("TZ", "CST-8", 1);
  tzset();
  esp_sntp_set_sync_interval(syncIntervalMs);
  // The network task updates ESP system time; the main loop handles UI/JSON state.
  esp_sntp_set_time_sync_notification_cb([](struct timeval *) { ntpReceived.store(true); });
}

bool timeSyncTick(bool connected) {
  uint32_t now = millis();
  bool updated = false;
  if (ntpReceived.exchange(false)) {
    time_t epoch = time(nullptr);
    tm local;
    if (validLocal(epoch, local)) {
      lastNtp = epoch;
      syncedAt = now;
      hasTime = updated = true;
      awaitingNtp = false;
      lastError = nullptr;
      Serial.println("TIME NTP synchronized (Asia/Shanghai)");
    } else {
      hasTime = false;
      awaitingNtp = true;
      attemptAt = now;
      lastError = "ntp_time_out_of_range";
    }
  }
  if (connected) {
    if (!wasConnected)
      startNtp(now);
    else if (awaitingNtp && uint32_t(now - attemptAt) >= retryMs) {
      if (!lastError) lastError = "ntp_timeout";
      firstServer = 1 - firstServer;
      startNtp(now);
    } else if (!awaitingNtp && hasTime && uint32_t(now - syncedAt) >= syncIntervalMs)
      startNtp(now);
  }
  wasConnected = connected;
  return updated;
}

String timeSyncDisplay() {
  tm local;
  if (!hasTime || !validLocal(time(nullptr), local))
    return wasConnected ? "Time: syncing..." : "Time: waiting for Wi-Fi";
  char text[24];
  strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &local);
  return String(text);
}

cJSON *timeSyncStatus() {
  auto *json = cJSON_CreateObject();
  const char *state = !wasConnected ? "waiting_for_wifi"
                        : awaitingNtp ? (lastError ? "retrying" : "syncing")
                        : hasTime ? "synced" : "syncing";
  cJSON_AddStringToObject(json, "status", state);
  cJSON_AddStringToObject(json, "timezone", "Asia/Shanghai");
  auto *list = cJSON_AddArrayToObject(json, "servers");
  for (const auto *server : servers)
    cJSON_AddItemToArray(list, cJSON_CreateString(server));
  cJSON_AddNumberToObject(json, "sync_interval_seconds", syncIntervalMs / 1000);
  tm local;
  bool valid = hasTime && validLocal(time(nullptr), local);
  cJSON_AddBoolToObject(json, "time_valid", valid);
  if (valid) {
    char text[32];
    strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%S+08:00", &local);
    cJSON_AddStringToObject(json, "time", text);
  } else
    cJSON_AddNullToObject(json, "time");
  if (lastNtp) {
    tm utc;
    gmtime_r(&lastNtp, &utc);
    char text[24];
    strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &utc);
    cJSON_AddStringToObject(json, "last_ntp_sync", text);
  } else
    cJSON_AddNullToObject(json, "last_ntp_sync");
  if (lastError)
    cJSON_AddStringToObject(json, "last_error", lastError);
  else
    cJSON_AddNullToObject(json, "last_error");
  return json;
}
