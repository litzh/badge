#include "../firmware/badge/local_tools.cpp"
#include <cassert>
#include <future>
#include <iostream>
#include <thread>
std::thread::id mainThread;
int snapshots = 0;
int writes = 0, volume = 100;
cJSON *provider(const DeviceTool::Command &command) {
  assert(std::this_thread::get_id() == mainThread); // Never read sensors on voice worker.
  if (command.operation == DeviceTool::Operation::Volume) {
    ++writes; volume = command.value;
    auto *result = cJSON_CreateObject(); cJSON_AddBoolToObject(result, "ok", true); return result;
  }
  ++snapshots;
  DeviceTool::Snapshot data; data.volume = snapshots; data.now = millis();
  return DeviceTool::reading(data, command.fields);
}
void waitQueued() {
  for (int i = 0; i < 200; ++i) {
    { std::lock_guard<std::mutex> lock(requests->mutex); if (!requests->items.empty()) return; }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  assert(false && "No request queued");
}
int main() {
  mainThread = std::this_thread::get_id(); assert(localToolsSetup());
  std::atomic<bool> cancelled{false};
  auto *input = cJSON_Parse("{\"fields\":[\"audio\"]}");
  auto call = [&] { return localToolsCall("read_device_state", input, cancelled); };
  auto pending = std::async(std::launch::async, call);
  waitQueued(); localToolsPoll(provider);
  auto *result = pending.get();
  assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result, "ok"))); cJSON_Delete(result);
  assert(snapshots == 1);
  auto aborted = std::async(std::launch::async, call);
  waitQueued(); cancelled.store(true); result = aborted.get();
  assert(!strcmp(cJSON_GetObjectItemCaseSensitive(result, "error")->valuestring, "cancelled")); cJSON_Delete(result);
  localToolsPoll(provider); assert(snapshots == 1); // Stale cancelled request is discarded.
  cancelled.store(false);
  auto next = std::async(std::launch::async, call);
  waitQueued(); localToolsPoll(provider); result = next.get();
  auto *audio = cJSON_GetObjectItemCaseSensitive(result, "audio");
  assert(cJSON_GetObjectItemCaseSensitive(audio, "volume_0_100")->valueint == 2); cJSON_Delete(result);
  uint32_t started = millis(); result = call(); // Main loop deliberately unavailable.
  assert(!strcmp(cJSON_GetObjectItemCaseSensitive(result, "error")->valuestring, "snapshot_timeout"));
  assert(uint32_t(millis() - started) < 2500); cJSON_Delete(result);
  localToolsPoll(provider); assert(snapshots == 2);
  result = localToolsCall("set_network", input, cancelled);
  assert(!strcmp(cJSON_GetObjectItemCaseSensitive(result, "error")->valuestring, "unknown_tool")); cJSON_Delete(result);
  auto *setting = cJSON_Parse("{\"percent\":30}");
  auto set = [&] { return localToolsCall("set_volume", setting, cancelled); };
  auto settingCall = std::async(std::launch::async, set);
  waitQueued(); localToolsPoll(provider); result = settingCall.get();
  assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result, "ok")) && volume == 30 && writes == 1); cJSON_Delete(result);
  auto cancelledWrite = std::async(std::launch::async, set);
  waitQueued(); cancelled.store(true);
  localToolsPoll(provider); // Main loop also checks cancellation before the worker has returned.
  result = cancelledWrite.get(); cJSON_Delete(result); assert(writes == 1);
  cancelled.store(false);
  result = set(); cJSON_Delete(result); localToolsPoll(provider); assert(writes == 1); // Timed-out write cannot execute later.
  cJSON_Delete(setting);
  cJSON_Delete(input); delete requests; delete responses;
  std::cout << "PASS main-thread read/write, cancelled/expired writes discarded, response isolation and bounded timeout\n";
}
