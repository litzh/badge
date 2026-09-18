#pragma once
#include <stddef.h>
#include <stdint.h>
// All calls after init must hold the microphone audio lease.
bool audioHardwareInit();
bool audioHardwareStart(bool recording, int volume = 60);
bool audioHardwareRead(int16_t *stereo, size_t bytes);
bool audioHardwareWrite(int16_t *stereo, size_t bytes);
bool audioHardwareVolume(int volume);
void audioHardwareStop(bool recording);
