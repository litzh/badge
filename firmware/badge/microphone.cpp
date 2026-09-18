#include "microphone.h"
#include "audio_hw.h"
#include "pin_config.h"
#include <Arduino.h>
#include <atomic>

namespace {
std::atomic<bool> requested{false}, leased{false};
SemaphoreHandle_t audioMutex = nullptr;
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
MicrophoneSnapshot current;
void publish(const MicrophoneSnapshot &value) {
  portENTER_CRITICAL(&lock); current = value; portEXIT_CRITICAL(&lock);
}
void worker(void *) {
  AudioAnalyzer analyzer;
  int16_t block[512];
  uint32_t retryAfter = 0;
  for (;;) {
    if (xSemaphoreTake(audioMutex, pdMS_TO_TICKS(30)) != pdTRUE) continue;
    auto state = microphoneSnapshot();
    if (!requested.load() || leased.load()) {
      if (state.active) audioHardwareStop(true);
      state.active = false;
      state.features = {};
      publish(state);
      xSemaphoreGive(audioMutex);
      vTaskDelay(pdMS_TO_TICKS(30));
      continue;
    }
    if (!state.active) {
      if ((int32_t)(millis() - retryAfter) < 0) {
        xSemaphoreGive(audioMutex);
        vTaskDelay(pdMS_TO_TICKS(30));
        continue;
      }
      if (!audioHardwareStart(true)) {
        state.error = "microphone_start_failed";
        retryAfter = millis() + 3000;
        publish(state);
        xSemaphoreGive(audioMutex);
        continue;
      }
      analyzer = AudioAnalyzer();
      state.active = true;
      state.error = nullptr;
    }
    if (!audioHardwareRead(block, sizeof(block))) {
      audioHardwareStop(true);
      state.active = false;
      state.error = "microphone_read_failed";
      state.features = {};
      retryAfter = millis() + 3000;
    } else {
      auto measured = analyzer.process(block, 256);
      measured.level = fmaxf(measured.level, state.features.level * .90f);
      measured.low = fmaxf(measured.low, state.features.low * .90f);
      measured.mid = fmaxf(measured.mid, state.features.mid * .90f);
      measured.high = fmaxf(measured.high, state.features.high * .90f);
      state.features = measured;
      state.sampledAt = millis();
      state.samples += 256;
    }
    publish(state);
    xSemaphoreGive(audioMutex);
    vTaskDelay(1);
  }
}
} // namespace

void microphoneSetup() {
  pinMode(PA, OUTPUT);
  digitalWrite(PA, LOW);
  audioMutex = xSemaphoreCreateMutex();
  if (!audioMutex || !audioHardwareInit()) {
    current.error = "microphone_init_failed";
  } else {
    current.ready = true;
    if (xTaskCreate(worker, "mic_features", 6144, nullptr, 1, nullptr) != pdPASS) {
      current.ready = false;
      current.error = "microphone_task_failed";
    }
  }
  Serial.printf("MICROPHONE %s\n", current.ready ? "ready" : current.error);
}

void microphoneSetEnabled(bool enabled) { requested.store(enabled); }

bool microphoneAcquire() {
  if (!microphoneSnapshot().ready || leased.exchange(true)) return false;
  if (xSemaphoreTake(audioMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    leased.store(false);
    return false;
  }
  auto state = microphoneSnapshot();
  if (state.active) audioHardwareStop(true);
  state.active = false;
  state.features = {};
  publish(state);
  return true;
}
void microphoneRelease() {
  // Caller stops its input/output stream before returning the lease.
  leased.store(false);
  xSemaphoreGive(audioMutex);
}

MicrophoneSnapshot microphoneSnapshot() {
  portENTER_CRITICAL(&lock);
  auto value = current;
  portEXIT_CRITICAL(&lock);
  return value;
}

cJSON *microphoneStatus() {
  auto m = microphoneSnapshot();
  auto *j = cJSON_CreateObject();
  cJSON_AddBoolToObject(j, "ready", m.ready);
  cJSON_AddBoolToObject(j, "active", m.active);
  cJSON_AddBoolToObject(j, "voice_owned", leased.load());
  cJSON_AddNumberToObject(j, "sample_rate_hz", 16000);
  cJSON_AddNumberToObject(j, "sample_count", m.samples);
  if (m.samples) cJSON_AddNumberToObject(j, "sample_age_ms", uint32_t(millis() - m.sampledAt));
  else cJSON_AddNullToObject(j, "sample_age_ms");
  if (m.error) cJSON_AddStringToObject(j, "error", m.error);
  else cJSON_AddNullToObject(j, "error");
  bool fresh = m.active && m.samples && uint32_t(millis() - m.sampledAt) < 500;
  const char *keys[] = {"level", "low", "mid", "high", "dbfs"};
  float values[] = {m.features.level, m.features.low, m.features.mid, m.features.high, m.features.dbfs};
  for (int i = 0; i < 5; ++i) {
    if (fresh) cJSON_AddNumberToObject(j, keys[i], values[i]);
    else cJSON_AddNullToObject(j, keys[i]);
  }
  return j;
}
