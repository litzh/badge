#include "local_tools.h"
#include "device_tool_protocol.h"
#include <Arduino.h>

namespace {
struct Request { uint32_t id; DeviceTool::Command command; const std::atomic<bool> *cancelled; };
struct Response { uint32_t id; char json[3072]; };
QueueHandle_t requests = nullptr, responses = nullptr;
std::atomic<uint32_t> active{0};
std::atomic<uint32_t> executing{0};
uint32_t sequence = 0; // Voice worker only.
}
bool localToolsSetup() {
  requests = xQueueCreate(1, sizeof(Request));
  responses = xQueueCreate(1, sizeof(Response));
  return requests && responses;
}
void localToolsPoll(cJSON *(*provider)(const DeviceTool::Command &)) {
  Request request;
  if (!requests || !responses || xQueueReceive(requests, &request, 0) != pdTRUE) return;
  if (active.load() != request.id || request.cancelled->load()) return;
  static Response response; // Main loop only; don't consume the 8KB loop stack.
  response.id = request.id;
  executing.store(request.id);
  // Do not execute a request which expired while the main loop was busy.
  if (active.load() != request.id || request.cancelled->load()) return;
  auto *reading = provider(request.command);
  bool printed = reading && cJSON_PrintPreallocated(reading, response.json, sizeof(response.json), false);
  cJSON_Delete(reading);
  if (!printed) strcpy(response.json, "{\"ok\":false,\"error\":\"snapshot_buffer_full\"}");
  if (active.load() == request.id) xQueueOverwrite(responses, &response);
}
cJSON *localToolsCall(const char *name, cJSON *input, const std::atomic<bool> &cancelled) {
  DeviceTool::Command command;
  if (const char *error = DeviceTool::parseCommand(name, input, command)) return DeviceTool::error(error);
  if (!requests || !responses) return DeviceTool::error("tool_not_ready");
  if (cancelled.load()) return DeviceTool::error("cancelled");
  if (!++sequence) ++sequence;
  Request request{sequence, command, &cancelled};
  xQueueReset(responses);
  active.store(request.id);
  if (xQueueSend(requests, &request, 0) != pdTRUE) { active.store(0); return DeviceTool::error("tool_busy"); }
  Response response;
  uint32_t started = millis();
  while (!cancelled.load() && uint32_t(millis() - started) < 2000) {
    if (xQueueReceive(responses, &response, pdMS_TO_TICKS(20)) == pdTRUE && response.id == request.id) {
      active.store(0);
      auto *result = cJSON_Parse(response.json);
      return result ? result : DeviceTool::error("invalid_snapshot");
    }
  }
  active.store(0);
  // A setting already being persisted cannot be rolled back by cancellation.
  if (command.operation != DeviceTool::Operation::Read && executing.load() == request.id)
    return DeviceTool::error("operation_outcome_unknown");
  return DeviceTool::error(cancelled.load() ? "cancelled" : "snapshot_timeout");
}
