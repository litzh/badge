#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>

struct AudioFeatures {
  float level = 0, low = 0, mid = 0, high = 0, dbfs = -96;
};

// Broad, overlapping frequency bands for animation, not a calibrated SPL meter.
// 16 kHz PCM; a DC blocker followed by 200 Hz and 2 kHz one-pole filters.
class AudioAnalyzer {
  float previous[2] = {}, dc[2] = {}, slow[2] = {}, fast[2] = {};
  static float energy(float rms) {
    float db = 20 * log10f(fmaxf(rms, 0.00001585f));
    return fminf(1, fmaxf(0, (db + 60) / 35));
  }
public:
  AudioFeatures process(const int16_t *stereo, size_t frames) {
    AudioFeatures result;
    if (!frames) return result;
    float power[2][4] = {};
    for (size_t i = 0; i < frames; ++i) {
      for (int c = 0; c < 2; ++c) {
        float input = stereo[2 * i + c] / 32768.0f;
        dc[c] = input - previous[c] + .995f * dc[c];
        previous[c] = input;
        slow[c] += .075535f * (dc[c] - slow[c]);
        fast[c] += .544062f * (dc[c] - fast[c]);
        float bands[] = {dc[c], slow[c], fast[c] - slow[c], dc[c] - fast[c]};
        for (int b = 0; b < 4; ++b) power[c][b] += bands[b] * bands[b];
      }
    }
    float rms[4];
    // Select the stronger microphone for this block; avoid phase cancellation.
    int channel = power[1][0] > power[0][0] ? 1 : 0;
    for (int b = 0; b < 4; ++b) rms[b] = sqrtf(power[channel][b] / frames);
    result.dbfs = 20 * log10f(fmaxf(rms[0], .00001585f));
    result.level = energy(rms[0]);
    result.low = energy(rms[1]);
    result.mid = energy(rms[2]);
    result.high = energy(rms[3]);
    return result;
  }
};
