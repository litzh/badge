#include "../firmware/badge/display_frame.h"
#include <assert.h>
#include <vector>
#include <stdio.h>

struct Panel {
  uint16_t *source;
  int nextY = 0, calls = 0;
  void draw16bitRGBBitmap(int x, int y, uint16_t *data, int w, int h) {
    assert(x == 0 && y == nextY && w == 466 && h > 0 && h <= 2);
    assert(w * h <= 1024);
    assert(data == source + y * w);
    nextY += h;
    ++calls;
  }
};

int main() {
  std::vector<uint16_t> pixels(466 * 466);
  Panel panel; panel.source = pixels.data();
  presentFrame(&panel, 0, 0, pixels.data(), 466, 466);
  assert(panel.nextY == 466 && panel.calls == 233);
  panel.nextY = panel.calls = 0;
  presentFrame(&panel, 0, 0, pixels.data(), 466, 23);
  assert(panel.nextY == 23 && panel.calls == 12);
  puts("PASS: full and odd-height frame strips cover every pixel without QSPI continuation");
}
