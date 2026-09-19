#pragma once
#include <atomic>
#include <cJSON.h>
#include <stdint.h>
#include "device_tool_protocol.h"

bool localToolsSetup();
// Main loop only; provider reads cached hardware state on its owning task.
void localToolsPoll(cJSON *(*provider)(const DeviceTool::Command &command));
// Voice worker only; bounded/cancellable wait. Caller owns the returned JSON.
cJSON *localToolsCall(const char *name, cJSON *input, const std::atomic<bool> &cancelled);
