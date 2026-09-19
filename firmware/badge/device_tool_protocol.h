#pragma once
#include <cJSON.h>
#include <cmath>
#include <cstring>
#include <stdint.h>
#include "battery_policy.h"

namespace DeviceTool {
constexpr char NAME[] = "read_device_state";
constexpr unsigned MAX_CALLS = 4, MAX_REQUESTS = 6;
constexpr uint32_t LLM_TIMEOUT_MS = 90000;
enum Field : uint32_t { Battery = 1, Motion = 2, Temperature = 4, Display = 8, Audio = 16, Network = 32 };
constexpr const char *names[] = {"battery", "motion", "chip_temperature", "display", "audio", "network"};
enum class Operation { Read, Volume, Brightness, Shutdown };
struct Command { Operation operation = Operation::Read; uint32_t fields = 0; int value = 0; };
inline void addWriteDefinitions(cJSON *tools) {
  auto *definitions = cJSON_Parse(R"json([
    {"name":"set_volume","description":"Set this device speaker volume to an absolute percentage, 0 (mute) through 100. Persists across restarts. Only when the user requests a change. For relative changes first read audio and calculate the target.","input_schema":{"type":"object","properties":{"percent":{"type":"integer","minimum":0,"maximum":100}},"required":["percent"],"additionalProperties":false}},
    {"name":"set_brightness","description":"Set saved screen brightness to an absolute percentage, 1 through 100. Persists across restarts. Low battery may limit effective brightness. Only when the user requests a change. For relative changes first read display and convert saved_brightness_0_255 to percent.","input_schema":{"type":"object","properties":{"percent":{"type":"integer","minimum":1,"maximum":100}},"required":["percent"],"additionalProperties":false}},
    {"name":"schedule_shutdown","description":"Power off this device 10 seconds AFTER this reply finishes playing (after completion for silent text). Only on an explicit request to power off, not screen sleep. Confirm briefly and mention BOOT or the Cancel shutdown touch button cancels it. If this reply fails or is cancelled, shutdown is automatically cancelled.","input_schema":{"type":"object","properties":{"delay_seconds":{"type":"integer","enum":[10]}},"required":["delay_seconds"],"additionalProperties":false}}
  ])json");
  if (!definitions) return;
  while (cJSON_GetArraySize(definitions)) cJSON_AddItemToArray(tools, cJSON_DetachItemFromArray(definitions, 0));
  cJSON_Delete(definitions);
}
inline cJSON *definition() {
  return cJSON_Parse(R"json({
    "name":"read_device_state",
    "description":"Read current state of this badge device. Use for its battery, motion/tilt, internal chip temperature, screen, volume or Wi-Fi signal. Read-only. No ambient/body temperature, compass, GPS, humidity or calibrated sound level. Motion is instantaneous; read again for current follow-up questions. Failed/stale samples are not measurements. Request only relevant fields together.",
    "input_schema":{"type":"object","properties":{"fields":{"type":"array","items":{"type":"string","enum":["battery","motion","chip_temperature","display","audio","network"]},"minItems":1,"maxItems":6,"uniqueItems":true}},"required":["fields"],"additionalProperties":false}
  })json");
}
inline bool fields(cJSON *input, uint32_t &mask) {
  mask = 0;
  if (!cJSON_IsObject(input) || cJSON_GetArraySize(input) != 1) return false;
  auto *list = cJSON_GetObjectItemCaseSensitive(input, "fields");
  if (!cJSON_IsArray(list) || cJSON_GetArraySize(list) < 1 || cJSON_GetArraySize(list) > 6) return false;
  cJSON *item;
  cJSON_ArrayForEach(item, list) {
    if (!cJSON_IsString(item)) return false;
    uint32_t bit = 0;
    for (unsigned i = 0; i < 6; ++i) if (!strcmp(item->valuestring, names[i])) bit = 1u << i;
    if (!bit || (mask & bit)) { mask = 0; return false; }
    mask |= bit;
  }
  return true;
}
inline const char *parseCommand(const char *name, cJSON *input, Command &command) {
  command = Command{};
  if (!name) return "unknown_tool";
  if (!strcmp(name, NAME)) return fields(input, command.fields) ? nullptr : "invalid_fields";
  const char *key = "percent";
  int minimum = 0, maximum = 100;
  if (!strcmp(name, "set_volume")) command.operation = Operation::Volume;
  else if (!strcmp(name, "set_brightness")) { command.operation = Operation::Brightness; minimum = 1; }
  else if (!strcmp(name, "schedule_shutdown")) {
    command.operation = Operation::Shutdown; key = "delay_seconds"; minimum = maximum = 10;
  } else return "unknown_tool";
  if (!cJSON_IsObject(input) || cJSON_GetArraySize(input) != 1) return "invalid_arguments";
  auto *value = cJSON_GetObjectItemCaseSensitive(input, key);
  if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble) || value->valuedouble < minimum ||
      value->valuedouble > maximum || value->valuedouble != value->valueint) return "invalid_arguments";
  command.value = value->valueint;
  return nullptr;
}
inline cJSON *error(const char *reason) {
  auto *j = cJSON_CreateObject();
  cJSON_AddBoolToObject(j, "ok", false);
  cJSON_AddStringToObject(j, "error", reason);
  return j;
}
// Every local call needs one nonempty, unique ID for a matching tool_result.
inline int callCount(cJSON *blocks) {
  if (!cJSON_IsArray(blocks)) return -1;
  int count = 0;
  cJSON *block;
  cJSON_ArrayForEach(block, blocks) {
    auto *type = cJSON_GetObjectItemCaseSensitive(block, "type");
    if (!cJSON_IsString(type) || strcmp(type->valuestring, "tool_use")) continue;
    auto *id = cJSON_GetObjectItemCaseSensitive(block, "id"), *name = cJSON_GetObjectItemCaseSensitive(block, "name");
    if (!cJSON_IsString(id) || !*id->valuestring || strlen(id->valuestring) > 128 ||
        !cJSON_IsString(name) || !*name->valuestring || strlen(name->valuestring) > 128) return -1;
    for (auto *other = blocks->child; other != block; other = other->next) {
      auto *t = cJSON_GetObjectItemCaseSensitive(other, "type"), *i = cJSON_GetObjectItemCaseSensitive(other, "id");
      if (cJSON_IsString(t) && !strcmp(t->valuestring, "tool_use") && cJSON_IsString(i) && !strcmp(id->valuestring, i->valuestring)) return -1;
    }
    ++count;
  }
  return count;
}
inline bool appendAssistant(cJSON *messages, cJSON *blocks) {
  auto *copy = cJSON_Duplicate(blocks, true), *message = cJSON_CreateObject();
  if (!copy || !message) { cJSON_Delete(copy); cJSON_Delete(message); return false; }
  cJSON_AddStringToObject(message, "role", "assistant");
  cJSON_AddItemToObject(message, "content", copy);
  cJSON_AddItemToArray(messages, message);
  return true;
}
inline cJSON *toolResult(const char *id, cJSON *data) {
  char *json = cJSON_PrintUnformatted(data);
  if (!json) return nullptr;
  auto *result = cJSON_CreateObject();
  cJSON_AddStringToObject(result, "type", "tool_result");
  cJSON_AddStringToObject(result, "tool_use_id", id);
  cJSON_AddStringToObject(result, "content", json);
  cJSON_AddBoolToObject(result, "is_error", !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(data, "ok")));
  cJSON_free(json);
  return result;
}
struct Snapshot {
  uint32_t now = 0;
  BatterySample battery;
  bool motionValid = false, temperatureValid = false;
  uint32_t motionAge = UINT32_MAX, temperatureAge = UINT32_MAX;
  float accel[3] = {}, gyro[3] = {}, temperatureC = 0;
  int brightness = 0, effectiveBrightness = 0, volume = 0, rssi = 0;
  bool screenOff = false, screensaver = false, manualOff = false, connected = false;
};
inline cJSON *group(cJSON *root, const char *name, const char *status, uint32_t age) {
  auto *j = cJSON_AddObjectToObject(root, name);
  cJSON_AddStringToObject(j, "status", status);
  if (age == UINT32_MAX) cJSON_AddNullToObject(j, "sample_age_ms");
  else cJSON_AddNumberToObject(j, "sample_age_ms", age);
  return j;
}
inline cJSON *reading(const Snapshot &s, uint32_t mask) {
  auto *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "ok", true);
  cJSON_AddNumberToObject(root, "snapshot_uptime_ms", s.now);
  if (mask & Battery) {
    auto &b = s.battery;
    uint32_t age = s.now - b.sampledAt;
    bool fresh = age <= BatteryPolicy::MAX_AGE_MS;
    bool valid = fresh && b.valid && b.powerValid && b.present && b.millivolts >= 2000 && b.millivolts <= 5000;
    auto *j = group(root, "battery", !fresh ? "stale" : !b.powerValid ? "read_failed" : !b.present ? "not_connected" : valid ? "ok" : "read_failed", age);
    if (fresh && b.powerValid) {
      cJSON_AddBoolToObject(j, "present", b.present);
      cJSON_AddBoolToObject(j, "usb_power", b.vbus);
    } else { cJSON_AddNullToObject(j, "present"); cJSON_AddNullToObject(j, "usb_power"); }
    if (valid) {
      cJSON_AddNumberToObject(j, "voltage_v", b.millivolts / 1000.0);
      cJSON_AddStringToObject(j, "power_state", b.direction == 1 ? "charging" : b.direction == 2 ? "discharging" : b.direction == 0 ? "standby" : "unknown");
    } else { cJSON_AddNullToObject(j, "voltage_v"); cJSON_AddNullToObject(j, "power_state"); }
    if (valid && b.percent >= 0 && b.percent <= 100) cJSON_AddNumberToObject(j, "estimated_percent", b.percent);
    else cJSON_AddNullToObject(j, "estimated_percent");
    cJSON_AddStringToObject(j, "note", "PMU estimate; cannot predict remaining runtime or battery capacity.");
  }
  if (mask & Motion) {
    bool finite = true;
    for (unsigned i = 0; i < 3; ++i) finite &= std::isfinite(s.accel[i]) && std::isfinite(s.gyro[i]);
    bool valid = s.motionValid && finite && s.motionAge <= 1000;
    auto *j = group(root, "motion", s.motionAge > 1000 ? "stale" : valid ? "ok" : "read_failed", s.motionAge);
    if (valid) {
      auto *a = cJSON_AddArrayToObject(j, "acceleration_xyz_g"), *g = cJSON_AddArrayToObject(j, "angular_velocity_xyz_dps");
      double norm = 0, spin = 0;
      for (unsigned i = 0; i < 3; ++i) {
        cJSON_AddItemToArray(a, cJSON_CreateNumber(s.accel[i])); cJSON_AddItemToArray(g, cJSON_CreateNumber(s.gyro[i]));
        norm += s.accel[i] * s.accel[i]; spin += s.gyro[i] * s.gyro[i];
      }
      norm = std::sqrt(norm); spin = std::sqrt(spin);
      bool stable = norm >= 0.85 && norm <= 1.15 && spin < 10;
      cJSON_AddBoolToObject(j, "quasi_static", stable);
      if (stable) {
        double z = s.accel[2] / norm;
        z = z < -1 ? -1 : z > 1 ? 1 : z;
        cJSON_AddNumberToObject(j, "tilt_from_face_up_deg", std::acos(z) * 180 / 3.141592653589793);
      } else cJSON_AddNullToObject(j, "tilt_from_face_up_deg");
    } else {
      cJSON_AddNullToObject(j, "acceleration_xyz_g"); cJSON_AddNullToObject(j, "angular_velocity_xyz_dps");
      cJSON_AddNullToObject(j, "quasi_static"); cJSON_AddNullToObject(j, "tilt_from_face_up_deg");
    }
    cJSON_AddStringToObject(j, "note", "Instantaneous IMU sample, not movement history or absolute velocity. Tilt only valid when quasi_static; 0 degrees means face up, 90 upright, 180 face down. No compass heading or location.");
  }
  if (mask & Temperature) {
    bool valid = s.temperatureValid && std::isfinite(s.temperatureC) && s.temperatureAge <= 5000;
    auto *j = group(root, "chip_temperature", s.temperatureAge > 5000 ? "stale" : valid ? "ok" : "read_failed", s.temperatureAge);
    if (valid) cJSON_AddNumberToObject(j, "celsius", s.temperatureC);
    else cJSON_AddNullToObject(j, "celsius");
    cJSON_AddStringToObject(j, "source", "QMI8658 internal chip temperature");
    cJSON_AddStringToObject(j, "note", "NOT ambient temperature or body temperature; affected by device heating.");
  }
  if (mask & Display) {
    auto *j = group(root, "display", "ok", 0);
    cJSON_AddNumberToObject(j, "saved_brightness_0_255", s.brightness);
    cJSON_AddNumberToObject(j, "effective_brightness_0_255", s.effectiveBrightness);
    cJSON_AddStringToObject(j, "mode", s.screenOff ? "off" : s.screensaver ? "screensaver" : "on");
    cJSON_AddBoolToObject(j, "manual_off", s.manualOff);
  }
  if (mask & Audio) {
    auto *j = group(root, "audio", "ok", 0);
    cJSON_AddNumberToObject(j, "volume_0_100", s.volume);
    cJSON_AddBoolToObject(j, "muted", s.volume == 0);
  }
  if (mask & Network) {
    auto *j = group(root, "network", "ok", 0);
    cJSON_AddBoolToObject(j, "connected", s.connected);
    if (s.connected) cJSON_AddNumberToObject(j, "rssi_dbm", s.rssi);
    else cJSON_AddNullToObject(j, "rssi_dbm");
  }
  return root;
}
} // namespace DeviceTool
