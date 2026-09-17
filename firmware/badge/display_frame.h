#pragma once
#include <stdint.h>

// Keep every CO5300 window below the QSPI driver's 1024-pixel transfer chunk.
// Each strip starts a new RAM write instead of relying on a long continuation.
template <typename Display>
void presentFrame(Display *display, int16_t x, int16_t y, uint16_t *pixels,
                  int16_t width, int16_t height) {
  for (int16_t row = 0; row < height; row += 2) {
    int16_t rows = height - row < 2 ? height - row : 2;
    display->draw16bitRGBBitmap(x, y + row, pixels + row * width, width, rows);
  }
}
