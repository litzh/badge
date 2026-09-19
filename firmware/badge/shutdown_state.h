#pragma once
#include <stdint.h>

// Main-loop owned. Never persist a shutdown across a reboot.
struct ShutdownState {
  enum class Phase { Idle, Waiting, Countdown, Requested, Cancelled, Failed };
  Phase phase = Phase::Idle;
  uint32_t turn = 0, started = 0;
  const char *reason = "";
  static constexpr uint32_t DELAY_MS = 10000;
  bool active() const { return phase == Phase::Waiting || phase == Phase::Countdown; }
  bool schedule(uint32_t owner) {
    if (active()) return turn == owner; // Repeated calls do not reset the deadline.
    if (phase == Phase::Requested || (turn == owner && phase == Phase::Cancelled)) return false;
    turn = owner; phase = Phase::Waiting; reason = ""; return true;
  }
  void cancel(const char *why) { if (active()) { phase = Phase::Cancelled; reason = why; } }
  void fail(const char *why) { phase = Phase::Failed; reason = why; }
  bool tick(uint32_t now, uint32_t currentTurn, bool busy, bool cancelled, bool done) {
    if (!active()) return false;
    if (currentTurn != turn || cancelled) { cancel("conversation_cancelled"); return false; }
    if (phase == Phase::Waiting && !busy) {
      if (!done) { cancel("reply_failed"); return false; }
      phase = Phase::Countdown; started = now;
    }
    if (phase == Phase::Countdown && uint32_t(now - started) >= DELAY_MS) {
      phase = Phase::Requested; started = now; return true;
    }
    return false;
  }
  unsigned remaining(uint32_t now) const {
    if (phase == Phase::Waiting) return 10;
    if (phase != Phase::Countdown) return 0;
    uint32_t elapsed = uint32_t(now - started);
    return elapsed >= DELAY_MS ? 0 : (DELAY_MS - elapsed + 999) / 1000;
  }
  const char *label() const {
    switch (phase) {
      case Phase::Waiting: return "waiting_for_reply";
      case Phase::Countdown: return "countdown";
      case Phase::Requested: return "power_off_requested";
      case Phase::Cancelled: return "cancelled";
      case Phase::Failed: return "failed";
      default: return "idle";
    }
  }
};
