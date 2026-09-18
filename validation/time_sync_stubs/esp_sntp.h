#pragma once
#include <cstdint>
#include <sys/time.h>
inline void (*syncCallback)(timeval *) = nullptr;
inline uint32_t configuredInterval = 0;
inline void esp_sntp_set_sync_interval(uint32_t interval) { configuredInterval = interval; }
inline void esp_sntp_set_time_sync_notification_cb(void (*callback)(timeval *)) {
  syncCallback = callback;
}
