#pragma once
#include "audio_features.h"
#include <cJSON.h>
#include <stdint.h>

struct MicrophoneSnapshot {
  bool ready = false, active = false;
  uint32_t samples = 0, sampledAt = 0;
  const char *error = nullptr;
  AudioFeatures features;
};
void microphoneSetup();
void microphoneSetEnabled(bool enabled);
MicrophoneSnapshot microphoneSnapshot();
cJSON *microphoneStatus();
