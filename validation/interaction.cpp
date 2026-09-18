#include "../firmware/badge/interaction.h"
#include "../firmware/badge/screensaver_state.h"
#include <cassert>
#include <iostream>
using namespace BadgeUI;
int main() {
  BootButton key;
  assert(!key.update(true, 0)); // A key held during boot is ignored.
  assert(!key.update(true, 4000));
  assert(!key.update(false, 4010)); assert(!key.update(false, 4045));
  assert(!key.update(true, 4050)); assert(!key.update(false, 4060)); // bounce
  assert(!key.update(true, 4070)); assert(!key.update(true, 4104));
  assert(key.update(true, 4105));
  assert(!key.update(true, 11000)); // long hold neither repeats nor enters provisioning
  assert(!key.update(false, 11010)); assert(!key.update(false, 11045));
  assert(!key.update(true, UINT32_MAX - 10)); assert(key.update(true, 24));
  assert(voiceAction(false, false) == VoiceAction::Start);
  assert(voiceAction(true, true) == VoiceAction::Send);
  assert(voiceAction(true, false) == VoiceAction::Cancel);

  Touch touch;
  assert(touch.update(true, 233, 290, Page::Home, false) == Control::None);
  assert(touch.captured == Control::None); // Wake-only gesture, even if held/moved.
  touch.update(true, 234, 291, Page::Home, true);
  assert(touch.update(false, 234, 291, Page::Home, true) == Control::None);
  touch.update(true, 233, 290, Page::Home, true);
  assert(touch.captured == Control::Talk);
  assert(touch.update(false, 233, 290, Page::Home, true) == Control::Talk);
  touch.update(true, 233, 290, Page::Home, true);
  touch.update(true, 233, 340, Page::Home, true); // Slide outside cancels permanently.
  touch.update(true, 233, 290, Page::Home, true);
  assert(touch.update(false, 233, 290, Page::Home, true) == Control::None);
  touch.update(true, 233, 290, Page::Home, true);
  assert(touch.update(false, 233, 290, Page::Settings, true) == Control::None);
  touch.update(true, 120, 180, Page::Settings, true);
  touch.cancel(); // Disabled button / PWR action cancels capture.
  assert(touch.update(false, 120, 180, Page::Settings, true) == Control::None);
  // All visible buttons fit the round screen and their centers map correctly.
  for (const auto &b : buttons) {
    assert(b.w >= 48 && b.h >= 48);
    assert(hit(b.page, b.x + b.w / 2, b.y + b.h / 2) == b.id);
    for (int x : {int(b.x), b.x + b.w - 1}) for (int y : {int(b.y), b.y + b.h - 1})
      assert((x - 233) * (x - 233) + (y - 233) * (y - 233) < 233 * 233);
  }
  assert(hit(Page::Home, 93, 264) == Control::None); // Rounded corner isn't clickable.
  assert(hit(Page::Home, 233, 340) == Control::None); // Gap between buttons.

  ScreensaverState display;
  display.activity(1000);
  display.keepAwake(20000); assert(!display.tick(20000));
  display.sleep(); display.keepAwake(40000);
  assert(display.manualOff && display.screenOff && !display.tick(60000));
  display.activity(60001);
  assert(!display.manualOff && !display.screenOff);
  assert(display.tick(70001));
  std::cout << "PASS BOOT debounce/hold/wrap, touch wake/release/drag/page cancellation, round targets, manual sleep during voice\n";
}
