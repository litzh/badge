#pragma once
#include <stdint.h>

namespace BadgeUI {
enum class Page { Home, Settings, Info };
enum class Control { None, Talk, Settings, VolumeDown, VolumeUp, BrightnessDown, BrightnessUp, Wifi, Info, Back };
enum class VoiceAction { Start, Send, Cancel };
inline VoiceAction voiceAction(bool busy, bool recording) {
  return !busy ? VoiceAction::Start : recording ? VoiceAction::Send : VoiceAction::Cancel;
}
struct Button {
  Page page;
  Control id;
  int16_t x, y, w, h;
  // Rounded corners are excluded from the hit target, matching the drawn shape.
  bool contains(int px, int py) const {
    if (px < x || py < y || px >= x + w || py >= y + h) return false;
    constexpr int radius = 12;
    int dx = px < x + radius ? x + radius - px : px >= x + w - radius ? px - (x + w - radius - 1) : 0;
    int dy = py < y + radius ? y + radius - py : py >= y + h - radius ? py - (y + h - radius - 1) : 0;
    return dx * dx + dy * dy <= radius * radius;
  }
};
constexpr Button buttons[] = {
  {Page::Home, Control::Talk, 93, 264, 280, 64},
  {Page::Home, Control::Settings, 153, 350, 160, 52},
  {Page::Settings, Control::VolumeDown, 86, 155, 72, 52},
  {Page::Settings, Control::VolumeUp, 308, 155, 72, 52},
  {Page::Settings, Control::BrightnessDown, 86, 251, 72, 52},
  {Page::Settings, Control::BrightnessUp, 308, 251, 72, 52},
  {Page::Settings, Control::Wifi, 86, 323, 140, 50},
  {Page::Settings, Control::Info, 240, 323, 140, 50},
  {Page::Settings, Control::Back, 173, 388, 120, 48},
  {Page::Info, Control::Back, 173, 388, 120, 48},
};
inline const Button *button(Page page, Control id) {
  for (const auto &b : buttons) if (b.page == page && b.id == id) return &b;
  return nullptr;
}
inline Control hit(Page page, int x, int y) {
  for (const auto &b : buttons) if (b.page == page && b.contains(x, y)) return b.id;
  return Control::None;
}

// One action per press, including a long hold. Ignore a key held during boot
// until it is released, and debounce both edges across millis() wrap.
struct BootButton {
  bool initialized = false, raw = false, stable = false, armed = false;
  uint32_t changedAt = 0;
  bool update(bool down, uint32_t now) {
    if (!initialized) { initialized = true; raw = stable = down; armed = !down; changedAt = now; return false; }
    if (down != raw) { raw = down; changedAt = now; }
    if (raw == stable || uint32_t(now - changedAt) < 35) return false;
    stable = raw;
    if (!stable) { armed = true; return false; }
    if (!armed) return false;
    armed = false;
    return true;
  }
};

// Capture at touch-down, activate on release. Leaving the original button,
// changing page or waking the display cancels the entire gesture.
struct Touch {
  bool down = false;
  Control captured = Control::None;
  Page page = Page::Home;
  void cancel() { captured = Control::None; }
  Control update(bool pressed, int x, int y, Page current, bool allow) {
    if (pressed && !down) {
      captured = allow ? hit(current, x, y) : Control::None;
      page = current;
    } else if (pressed && captured != Control::None) {
      const auto *b = button(page, captured);
      if (!allow || current != page || !b || !b->contains(x, y)) cancel();
    }
    Control action = Control::None;
    if (!pressed && down) {
      const auto *b = button(page, captured);
      if (allow && current == page && b && b->contains(x, y)) action = captured;
      cancel();
    }
    down = pressed;
    return action;
  }
};
} // namespace BadgeUI
