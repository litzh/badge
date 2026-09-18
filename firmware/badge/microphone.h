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
// Exclusive ownership for voice capture/playback. Only the acquiring task releases.
bool microphoneAcquire();
void microphoneRelease();
MicrophoneSnapshot microphoneSnapshot();
cJSON *microphoneStatus();
