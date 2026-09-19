#include "../firmware/badge/device_tool_protocol.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <memory>
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
Json parse(const char *text) { return Json(cJSON_Parse(text), cJSON_Delete); }
cJSON *get(cJSON *j, const char *name) { return cJSON_GetObjectItemCaseSensitive(j, name); }
bool equals(cJSON *j, const char *key, const char *value) {
  auto *item = get(j, key); return cJSON_IsString(item) && !strcmp(item->valuestring, value);
}
int main(int argc, char **) {
  Json definition(DeviceTool::definition(), cJSON_Delete);
  assert(definition && equals(definition.get(), "name", "read_device_state"));
  uint32_t mask = 0;
  for (const char *invalid : {"null", "[]", "{}", "{\"fields\":[]}", "{\"fields\":[\"password\"]}",
       "{\"fields\":[\"battery\",\"battery\"]}", "{\"fields\":[1]}", "{\"fields\":\"battery\"}",
       "{\"fields\":[\"battery\"],\"execute\":\"shutdown\"}"}) {
    auto j = parse(invalid); assert(!DeviceTool::fields(j.get(), mask));
  }
  auto fields = parse("{\"fields\":[\"battery\",\"motion\",\"chip_temperature\",\"display\",\"audio\",\"network\"]}");
  assert(DeviceTool::fields(fields.get(), mask) && mask == 63);
  DeviceTool::Snapshot s;
  s.now = 10000; s.battery.sampledAt = 9000;
  s.battery.valid = s.battery.powerValid = s.battery.present = true;
  s.battery.percent = 73; s.battery.millivolts = 3900; s.battery.direction = 2;
  s.motionValid = s.temperatureValid = s.connected = true;
  s.motionAge = 20; s.temperatureAge = 900;
  s.accel[2] = 1; s.temperatureC = 38.5;
  s.volume = 0; s.brightness = 160; s.effectiveBrightness = 0; s.screenOff = s.manualOff = true; s.rssi = -55;
  Json reading(DeviceTool::reading(s, mask), cJSON_Delete);
  auto *battery = get(reading.get(), "battery"), *motion = get(reading.get(), "motion");
  assert(equals(battery, "status", "ok") && get(battery, "estimated_percent")->valueint == 73);
  assert(cJSON_IsTrue(get(motion, "quasi_static")) && get(motion, "tilt_from_face_up_deg")->valuedouble == 0);
  assert(cJSON_IsTrue(get(get(reading.get(), "audio"), "muted")));
  assert(equals(get(reading.get(), "display"), "mode", "off"));
  assert(strstr(get(get(reading.get(), "chip_temperature"), "note")->valuestring, "NOT ambient"));
  char *serialized = cJSON_PrintUnformatted(reading.get());
  assert(strlen(serialized) < 3060 && !strstr(serialized, "ssid") && !strstr(serialized, "password"));
  cJSON_free(serialized);
  // Only requested fields leave the device; unavailable measurements stay null.
  Json subset(DeviceTool::reading(s, DeviceTool::Audio), cJSON_Delete);
  assert(!get(subset.get(), "battery") && !get(subset.get(), "motion") && !get(subset.get(), "network"));
  s.accel[2] = -1;
  Json upsideDown(DeviceTool::reading(s, DeviceTool::Motion), cJSON_Delete);
  assert(std::abs(get(get(upsideDown.get(), "motion"), "tilt_from_face_up_deg")->valuedouble - 180) < 0.01);
  s.accel[2] = 2; s.gyro[0] = 50;
  Json moving(DeviceTool::reading(s, DeviceTool::Motion), cJSON_Delete);
  assert(cJSON_IsFalse(get(get(moving.get(), "motion"), "quasi_static")));
  assert(cJSON_IsNull(get(get(moving.get(), "motion"), "tilt_from_face_up_deg")));
  s.battery.sampledAt = 0; s.motionAge = 1500; s.temperatureAge = 6000; s.connected = false;
  Json stale(DeviceTool::reading(s, 63), cJSON_Delete);
  for (const char *group : {"battery", "motion", "chip_temperature"}) assert(equals(get(stale.get(), group), "status", "stale"));
  assert(cJSON_IsNull(get(get(stale.get(), "battery"), "estimated_percent")));
  assert(cJSON_IsNull(get(get(stale.get(), "network"), "rssi_dbm")));
  s.battery.sampledAt = s.now; s.battery.present = false;
  s.temperatureAge = 0; s.temperatureC = std::numeric_limits<float>::quiet_NaN();
  Json failed(DeviceTool::reading(s, 63), cJSON_Delete);
  assert(equals(get(failed.get(), "battery"), "status", "not_connected"));
  assert(cJSON_IsNull(get(get(failed.get(), "chip_temperature"), "celsius")));
  // Uptime wrap does not turn a recent battery sample stale.
  s.now = 100; s.battery.sampledAt = UINT32_MAX - 100; s.battery.present = true;
  Json wrapped(DeviceTool::reading(s, DeviceTool::Battery), cJSON_Delete);
  assert(equals(get(wrapped.get(), "battery"), "status", "ok"));

  auto blocks = parse(R"json([
    {"type":"server_tool_use","id":"search1","name":"web_search","input":{"query":"test"}},
    {"type":"web_search_tool_result","tool_use_id":"search1","content":[{"type":"opaque","encrypted":"preserve"}]},
    {"type":"text","text":"intermediate text must not be spoken"},
    {"type":"tool_use","id":"local1","name":"read_device_state","input":{"fields":["battery"]}},
    {"type":"tool_use","id":"local2","name":"read_device_state","input":{"fields":["audio"]}}
  ])json");
  assert(DeviceTool::callCount(blocks.get()) == 2);
  Json messages(cJSON_CreateArray(), cJSON_Delete);
  assert(DeviceTool::appendAssistant(messages.get(), blocks.get()));
  auto *copy = get(cJSON_GetArrayItem(messages.get(), 0), "content");
  assert(cJSON_Compare(copy, blocks.get(), true)); // Includes opaque server results.
  Json result(DeviceTool::toolResult("local1", reading.get()), cJSON_Delete);
  assert(equals(result.get(), "tool_use_id", "local1") && cJSON_IsFalse(get(result.get(), "is_error")));
  auto data = parse(get(result.get(), "content")->valuestring);
  assert(cJSON_IsTrue(get(data.get(), "ok")));
  Json error(DeviceTool::error("snapshot_timeout"), cJSON_Delete);
  Json errorResult(DeviceTool::toolResult("local2", error.get()), cJSON_Delete);
  auto errorData = parse(get(errorResult.get(), "content")->valuestring);
  assert(cJSON_IsFalse(get(errorData.get(), "ok"))); // DeepSeek ignores is_error; JSON carries it too.
  auto duplicate = parse("[{\"type\":\"tool_use\",\"id\":\"x\",\"name\":\"a\"},{\"type\":\"tool_use\",\"id\":\"x\",\"name\":\"b\"}]");
  assert(DeviceTool::callCount(duplicate.get()) == -1);
  auto missing = parse("[{\"type\":\"tool_use\",\"name\":\"read_device_state\"}]");
  assert(DeviceTool::callCount(missing.get()) == -1);
  if (argc > 1) {
    char *schema = cJSON_PrintUnformatted(definition.get()); std::cout << schema << '\n'; cJSON_free(schema);
  } else std::cout << "PASS schema, selective fields, freshness/nulls, tilt validity, privacy, tool ID pairing and opaque search blocks\n";
}
