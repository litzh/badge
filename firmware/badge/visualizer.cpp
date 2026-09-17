#include "visualizer.h"
#include "particle_field.h"
#include "board_io.h"
#include "microphone.h"
#include "display_frame.h"
#include <Arduino_GFX_Library.h>
#include <Arduino.h>
#include <esp_heap_caps.h>

namespace {
ParticleField field;
uint16_t *pixels = nullptr;
uint32_t lastFrame = 0, frames = 0, drawMs = 0, periodMs = 0;
uint32_t litPixels = 0;
uint16_t peakChannel = 0;
}

void visualizerSetup() {
  pixels = (uint16_t *)heap_caps_calloc(ParticleField::SIZE * ParticleField::SIZE,
                                      sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  Serial.printf("VISUALIZER %s\n", pixels ? "ready" : "psram_unavailable; using simple screensaver");
}

bool visualizerDraw(Arduino_GFX *gfx, bool first) {
  if (!pixels) return false;
  uint32_t started = millis();
  if (first) memset(pixels, 0, ParticleField::SIZE * ParticleField::SIZE * sizeof(uint16_t));
  periodMs = first || !lastFrame ? 67 : uint32_t(started - lastFrame);
  float dt = fminf(.12f, periodMs / 1000.0f);
  lastFrame = started;
  auto motion = boardMotion();
  auto mic = microphoneSnapshot();
  VisualInputs in;
  if (motion.valid) { in.ax=motion.ax;in.ay=motion.ay;in.az=motion.az;in.gz=motion.gz; }
  // Temperature sensor failure holds the last smoothed color, not a false zero.
  in.temperature=motion.temperatureValid?motion.temperatureC:field.temperature;
  if (mic.active && mic.samples && uint32_t(started - mic.sampledAt)<500) {
    in.audio=mic.features.level;
    in.bands[0]=mic.features.low;in.bands[1]=mic.features.mid;in.bands[2]=mic.features.high;
  }
  field.step(in,dt);
  field.render(pixels,dt);
  if ((frames % 16) == 0) {
    litPixels = 0; peakChannel = 0;
    for (int i = 0; i < ParticleField::SIZE * ParticleField::SIZE; ++i) {
      uint16_t p = pixels[i];
      if (p) ++litPixels;
      uint16_t peak = max(max((p >> 11) * 2, (p >> 5) & 63), (p & 31) * 2);
      if (peak > peakChannel) peakChannel = peak;
    }
  }
  presentFrame(gfx,0,0,pixels,ParticleField::SIZE,ParticleField::SIZE);
  drawMs=millis()-started;
  ++frames;
  return true;
}

cJSON *visualizerStatus() {
  auto *j=cJSON_CreateObject();
  cJSON_AddStringToObject(j,"mode",pixels?"lumina":"simple_fallback");
  cJSON_AddBoolToObject(j,"ready",pixels!=nullptr);
  cJSON_AddNumberToObject(j,"particle_count",pixels?ParticleField::COUNT:7);
  cJSON_AddNumberToObject(j,"frame_count",frames);
  cJSON_AddNumberToObject(j,"lit_pixels",litPixels);
  cJSON_AddNumberToObject(j,"peak_channel_6bit",peakChannel);
  cJSON_AddNumberToObject(j,"last_draw_ms",drawMs);
  cJSON_AddNumberToObject(j,"last_frame_period_ms",periodMs);
  cJSON_AddNumberToObject(j,"audio_energy",field.energy);
  cJSON_AddNumberToObject(j,"temperature_filtered_c",field.temperature);
  cJSON_AddNumberToObject(j,"gravity_x",field.gravityX);
  cJSON_AddNumberToObject(j,"gravity_y",field.gravityY);
  cJSON_AddNumberToObject(j,"spin",field.spin);
  cJSON_AddNumberToObject(j,"shake",field.impulse);
  return j;
}
