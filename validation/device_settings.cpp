#include "../firmware/badge/shutdown_state.h"
#include "../firmware/badge/device_tool_protocol.h"
#include <cassert>
#include <iostream>

int main() {
  using namespace DeviceTool;
  Command command;
  auto parse = [&](const char *name, const char *json) {
    auto *input = cJSON_Parse(json);
    bool ok = !parseCommand(name, input, command); cJSON_Delete(input); return ok;
  };
  assert(parse("set_volume", "{\"percent\":0}") && command.operation == Operation::Volume && command.value == 0);
  assert(parse("set_volume", "{\"percent\":100}"));
  assert(parse("set_brightness", "{\"percent\":1}") && command.operation == Operation::Brightness);
  assert(parse("set_brightness", "{\"percent\":100}"));
  for (const char *bad : {"{}", "null", "[]", "{\"percent\":true}", "{\"percent\":\"50\"}",
       "{\"percent\":0.5}", "{\"percent\":-1}", "{\"percent\":101}", "{\"percent\":1e100}",
       "{\"percent\":50,\"extra\":0}", "{\"percent\":50,\"percent\":40}"}) assert(!parse("set_volume", bad));
  assert(!parse("set_brightness", "{\"percent\":0}"));
  assert(parse("schedule_shutdown", "{\"delay_seconds\":10}") && command.operation == Operation::Shutdown);
  assert(!parse("schedule_shutdown", "{\"delay_seconds\":0}"));
  assert(!parse("schedule_shutdown", "{\"delay_seconds\":9}"));
  assert(!parse("shutdown", "{}"));
  auto *tools = cJSON_CreateArray(); addWriteDefinitions(tools); assert(cJSON_GetArraySize(tools) == 3); cJSON_Delete(tools);

  ShutdownState s;
  assert(!s.tick(500, 1, false, false, true));
  assert(s.schedule(1));
  assert(!s.tick(100000, 1, true, false, false)); // LLM and TTS time never consume countdown.
  assert(s.phase == ShutdownState::Phase::Waiting && s.remaining(100000) == 10);
  assert(!s.tick(100001, 1, false, false, true));
  assert(s.phase == ShutdownState::Phase::Countdown);
  assert(s.schedule(1)); // Duplicate call cannot postpone power off.
  assert(!s.tick(109002, 1, false, false, true) && s.remaining(109002) == 1);
  assert(!s.tick(110000, 1, false, false, true));
  assert(s.tick(110001, 1, false, false, true));
  assert(!s.tick(120001, 1, false, false, true)); // Only one PMU write.
  s.fail("power_off_write_failed"); assert(!s.active());
  for (bool duringCountdown : {false, true}) {
    s = ShutdownState{}; assert(s.schedule(2));
    if (duringCountdown) s.tick(0, 2, false, false, true);
    s.cancel("user_cancelled");
    assert(!s.tick(20000, 2, false, false, true));
    assert(!s.schedule(2)); // Same conversation cannot undo BOOT cancellation.
    assert(s.schedule(3));
  }
  for (int failure = 0; failure < 3; ++failure) {
    s = ShutdownState{}; s.schedule(3);
    assert(!s.tick(0, failure == 0 ? 4 : 3, false, failure == 1, failure != 2));
    assert(s.phase == ShutdownState::Phase::Cancelled);
  }
  s = ShutdownState{}; s.schedule(9);
  s.tick(UINT32_MAX - 4999, 9, false, false, true);
  assert(!s.tick(4999, 9, false, false, true));
  assert(s.tick(5000, 9, false, false, true)); // millis wrap.
  std::cout << "PASS settings validation, shutdown after reply, exact deadline, cancellation, failure, idempotency, rollover\n";
}
