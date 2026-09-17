#pragma once
#include <cJSON.h>
class Arduino_GFX;
void visualizerSetup();
bool visualizerDraw(Arduino_GFX *gfx, bool first);
cJSON *visualizerStatus();
