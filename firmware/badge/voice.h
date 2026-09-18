#pragma once
#include <Arduino.h>
#include <cJSON.h>
enum class VoiceMode { Chat, Echo, Loopback, Ask, Say };
void voiceSetup();
// Called only from the main loop / its HTTP handlers. Starts one background job.
bool voiceStart(VoiceMode mode = VoiceMode::Chat, const char *text = "");
bool voiceStopRecording();
void voiceCancel();
bool voiceReset();
bool voiceBusy();
bool voiceRecording();
uint32_t voiceRecordedMs();
bool voiceConfigured();
int voiceVolume();
// Main loop only: persist first, then notify the audio worker.
bool voiceSetVolume(int volume);
String voiceVolumeError();
String voiceLabel();
String voiceError();
cJSON *voiceStatus();
// Valid until the next voiceStart; main-loop HTTP download only, while idle.
const uint8_t *voiceLastRecording(size_t &size);
