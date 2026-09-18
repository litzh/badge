#pragma once
#include <Arduino.h>
#include <cJSON.h>

void timeSyncSetup();
bool timeSyncTick(bool connected);
String timeSyncDisplay();
cJSON *timeSyncStatus();
