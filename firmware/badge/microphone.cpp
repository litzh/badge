#include "microphone.h"
#include "pin_config.h"
#include <Arduino.h>
#include <atomic>
#include <driver/i2c_master.h>
#include <driver/i2s_std.h>
#include "src/esp_codec_dev/include/esp_codec_dev.h"
#include "src/esp_codec_dev/include/esp_codec_dev_defaults.h"

namespace {
esp_codec_dev_handle_t input = nullptr;
i2s_chan_handle_t rx = nullptr;
const audio_codec_data_if_t *data = nullptr;
const audio_codec_ctrl_if_t *ctrl = nullptr;
const audio_codec_if_t *codec = nullptr;
std::atomic<bool> requested{false};
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
MicrophoneSnapshot current;

void publish(const MicrophoneSnapshot &value) {
  portENTER_CRITICAL(&lock);
  current = value;
  portEXIT_CRITICAL(&lock);
}

void cleanup() {
  if (input) esp_codec_dev_delete(input);
  if (codec) audio_codec_delete_codec_if(codec);
  if (ctrl) audio_codec_delete_ctrl_if(ctrl);
  if (data) audio_codec_delete_data_if(data);
  if (rx) { i2s_channel_disable(rx); i2s_del_channel(rx); }
  input = nullptr; codec = nullptr; ctrl = nullptr; data = nullptr; rx = nullptr;
}

bool hardwareInit() {
  i2c_master_bus_handle_t bus = nullptr;
  if (i2c_master_get_bus_handle(0, &bus) != ESP_OK) return false;
  i2s_chan_config_t cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cfg.dma_desc_num = 6;
  cfg.dma_frame_num = 256;
  if (i2s_new_channel(&cfg, nullptr, &rx) != ESP_OK) return false;
  i2s_std_config_t stdcfg = {};
  stdcfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000);
  stdcfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  stdcfg.gpio_cfg.mclk = (gpio_num_t)PIN_ES7210_MCLK;
  stdcfg.gpio_cfg.bclk = (gpio_num_t)PIN_ES7210_BCLK;
  stdcfg.gpio_cfg.ws = (gpio_num_t)PIN_ES7210_LRCK;
  stdcfg.gpio_cfg.dout = I2S_GPIO_UNUSED;
  stdcfg.gpio_cfg.din = (gpio_num_t)PIN_ES7210_DIN;
  if (i2s_channel_init_std_mode(rx, &stdcfg) != ESP_OK || i2s_channel_enable(rx) != ESP_OK)
    return false;
  audio_codec_i2s_cfg_t dataCfg = {};
  dataCfg.rx_handle = rx;
  data = audio_codec_new_i2s_data(&dataCfg);
  audio_codec_i2c_cfg_t ctrlCfg = {};
  ctrlCfg.bus_handle = bus;
  ctrlCfg.addr = ES7210_CODEC_DEFAULT_ADDR;
  ctrl = audio_codec_new_i2c_ctrl(&ctrlCfg);
  if (!data || !ctrl) return false;
  es7210_codec_cfg_t adc = {};
  adc.ctrl_if = ctrl;
  adc.mic_selected = ES7120_SEL_MIC1 | ES7120_SEL_MIC3;
  codec = es7210_codec_new(&adc);
  if (!codec) return false;
  esp_codec_dev_cfg_t dev = {};
  dev.dev_type = ESP_CODEC_DEV_TYPE_IN;
  dev.codec_if = codec;
  dev.data_if = data;
  input = esp_codec_dev_new(&dev);
  if (!input) return false;
  // Establish the sample format once, then shut down until the screensaver starts.
  esp_codec_dev_sample_info_t fs = {};
  fs.sample_rate = 16000; fs.channel = 2; fs.bits_per_sample = 16;
  if (esp_codec_dev_open(input, &fs) != ESP_CODEC_DEV_OK) return false;
  if (esp_codec_dev_set_in_gain(input, 24) != ESP_CODEC_DEV_OK) return false;
  return esp_codec_dev_close(input) == ESP_CODEC_DEV_OK;
}

void worker(void *) {
  MicrophoneSnapshot state;
  state.ready = true;
  AudioAnalyzer analyzer;
  int16_t block[512];
  uint32_t retryAfter = 0;
  for (;;) {
    if (!requested.load()) {
      if (state.active) esp_codec_dev_close(input);
      state.active = false;
      state.features = {};
      publish(state);
      vTaskDelay(pdMS_TO_TICKS(30));
      continue;
    }
    if (!state.active) {
      if ((int32_t)(millis() - retryAfter) < 0) {
        vTaskDelay(pdMS_TO_TICKS(30));
        continue;
      }
      esp_codec_dev_sample_info_t fs = {};
      fs.sample_rate = 16000; fs.channel = 2; fs.bits_per_sample = 16;
      if (esp_codec_dev_open(input, &fs) != ESP_CODEC_DEV_OK ||
          esp_codec_dev_set_in_gain(input, 24) != ESP_CODEC_DEV_OK) {
        esp_codec_dev_close(input);
        state.error = "microphone_start_failed";
        retryAfter = millis() + 3000;
        publish(state);
        continue;
      }
      analyzer = AudioAnalyzer();
      state.active = true;
      state.error = nullptr;
    }
    if (esp_codec_dev_read(input, block, sizeof(block)) != ESP_CODEC_DEV_OK) {
      esp_codec_dev_close(input);
      state.active = false;
      state.error = "microphone_read_failed";
      state.features = {};
      retryAfter = millis() + 3000;
    } else {
      auto measured = analyzer.process(block, 256);
      // Hold brief beats long enough for the slower display loop to see them.
      measured.level = fmaxf(measured.level, state.features.level * .90f);
      measured.low = fmaxf(measured.low, state.features.low * .90f);
      measured.mid = fmaxf(measured.mid, state.features.mid * .90f);
      measured.high = fmaxf(measured.high, state.features.high * .90f);
      state.features = measured;
      state.sampledAt = millis();
      state.samples += 256;
    }
    publish(state);
    vTaskDelay(1);
  }
}
} // namespace

void microphoneSetup() {
  pinMode(PA, OUTPUT);
  digitalWrite(PA, LOW); // Screensaver never enables speaker playback.
  if (!hardwareInit()) {
    cleanup();
    current.error = "microphone_init_failed";
  } else if (xTaskCreate(worker, "mic_features", 6144, nullptr, 1, nullptr) != pdPASS) {
    cleanup();
    current.error = "microphone_task_failed";
  } else {
    portENTER_CRITICAL(&lock);
    current.ready = true;
    portEXIT_CRITICAL(&lock);
  }
  Serial.printf("MICROPHONE %s\n", current.ready ? "ready" : current.error);
}

void microphoneSetEnabled(bool enabled) { requested.store(enabled); }

MicrophoneSnapshot microphoneSnapshot() {
  portENTER_CRITICAL(&lock);
  auto value = current;
  portEXIT_CRITICAL(&lock);
  return value;
}

cJSON *microphoneStatus() {
  auto m = microphoneSnapshot();
  auto *j = cJSON_CreateObject();
  cJSON_AddBoolToObject(j, "ready", m.ready);
  cJSON_AddBoolToObject(j, "active", m.active);
  cJSON_AddNumberToObject(j, "sample_rate_hz", 16000);
  cJSON_AddNumberToObject(j, "sample_count", m.samples);
  if (m.samples) cJSON_AddNumberToObject(j, "sample_age_ms", uint32_t(millis() - m.sampledAt));
  else cJSON_AddNullToObject(j, "sample_age_ms");
  if (m.error) cJSON_AddStringToObject(j, "error", m.error);
  else cJSON_AddNullToObject(j, "error");
  bool fresh = m.active && m.samples && uint32_t(millis() - m.sampledAt) < 500;
  const char *keys[] = {"level", "low", "mid", "high", "dbfs"};
  float values[] = {m.features.level, m.features.low, m.features.mid, m.features.high, m.features.dbfs};
  for (int i = 0; i < 5; ++i) {
    if (fresh) cJSON_AddNumberToObject(j, keys[i], values[i]);
    else cJSON_AddNullToObject(j, keys[i]);
  }
  return j;
}
