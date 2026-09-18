#include "audio_hw.h"
#include <driver/i2c_master.h>
#include <driver/i2s_std.h>
#include "src/esp_codec_dev/include/esp_codec_dev.h"
#include "src/esp_codec_dev/include/esp_codec_dev_defaults.h"
static esp_codec_dev_handle_t input, output;

bool audioHardwareInit() {
  i2c_master_bus_handle_t bus = nullptr;
  if (i2c_master_get_bus_handle(0, &bus) != ESP_OK)
    return false;
  i2s_chan_handle_t tx = nullptr, rx = nullptr;
  i2s_chan_config_t cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cfg.auto_clear = true;
  if (i2s_new_channel(&cfg, &tx, &rx) != ESP_OK)
    return false;
  i2s_std_config_t stdcfg = {};
  stdcfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000);
  stdcfg.slot_cfg =
      I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  stdcfg.gpio_cfg.mclk = GPIO_NUM_16;
  stdcfg.gpio_cfg.bclk = GPIO_NUM_9;
  stdcfg.gpio_cfg.ws = GPIO_NUM_45;
  stdcfg.gpio_cfg.dout = GPIO_NUM_8;
  stdcfg.gpio_cfg.din = GPIO_NUM_10;
  if (i2s_channel_init_std_mode(tx, &stdcfg) != ESP_OK ||
      i2s_channel_init_std_mode(rx, &stdcfg) != ESP_OK)
    return false;
  i2s_channel_enable(tx);
  i2s_channel_enable(rx);
  audio_codec_i2s_cfg_t dataCfg = {};
  dataCfg.rx_handle = rx;
  dataCfg.tx_handle = tx;
  auto *data = audio_codec_new_i2s_data(&dataCfg);
  audio_codec_i2c_cfg_t ctrlCfg = {};
  ctrlCfg.bus_handle = bus;
  ctrlCfg.addr = ES8311_CODEC_DEFAULT_ADDR;
  auto *outCtrl = audio_codec_new_i2c_ctrl(&ctrlCfg);
  ctrlCfg.addr = ES7210_CODEC_DEFAULT_ADDR;
  auto *inCtrl = audio_codec_new_i2c_ctrl(&ctrlCfg);
  auto *gpio = audio_codec_new_gpio();
  if (!data || !outCtrl || !inCtrl || !gpio)
    return false;
  es8311_codec_cfg_t dac = {};
  dac.ctrl_if = outCtrl;
  dac.gpio_if = gpio;
  dac.codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC;
  dac.pa_pin = 46;
  dac.use_mclk = true;
  dac.hw_gain.pa_gain = 6;
  es7210_codec_cfg_t adc = {};
  adc.ctrl_if = inCtrl;
  adc.mic_selected = ES7120_SEL_MIC1 | ES7120_SEL_MIC3;
  auto *dacIf = es8311_codec_new(&dac);
  auto *adcIf = es7210_codec_new(&adc);
  if (!dacIf || !adcIf)
    return false;
  esp_codec_dev_cfg_t dev = {};
  dev.data_if = data;
  dev.codec_if = dacIf;
  dev.dev_type = ESP_CODEC_DEV_TYPE_OUT;
  output = esp_codec_dev_new(&dev);
  dev.codec_if = adcIf;
  dev.dev_type = ESP_CODEC_DEV_TYPE_IN;
  input = esp_codec_dev_new(&dev);
  return input && output;
}
bool audioHardwareStart(bool recording, int volume) {
  esp_codec_dev_sample_info_t fs = {};
  fs.sample_rate = 16000;
  fs.channel = 2;
  fs.bits_per_sample = 16;
  auto handle = recording ? input : output;
  if (esp_codec_dev_open(handle, &fs) != ESP_CODEC_DEV_OK) {
    esp_codec_dev_close(handle);
    return false;
  }
  bool configured = recording ? esp_codec_dev_set_in_gain(input, 24) == ESP_CODEC_DEV_OK : audioHardwareVolume(volume);
  if (!configured) {
    esp_codec_dev_close(handle);
    return false;
  }
  return true;
}
bool audioHardwareRead(int16_t *data, size_t bytes) {
  return esp_codec_dev_read(input, data, bytes) == ESP_CODEC_DEV_OK;
}
bool audioHardwareWrite(int16_t *data, size_t bytes) {
  return esp_codec_dev_write(output, data, bytes) == ESP_CODEC_DEV_OK;
}
void audioHardwareStop(bool recording) { esp_codec_dev_close(recording ? input : output); }
bool audioHardwareVolume(int volume) {
  return esp_codec_dev_set_out_vol(output, volume) == ESP_CODEC_DEV_OK &&
         esp_codec_dev_set_out_mute(output, volume == 0) == ESP_CODEC_DEV_OK;
}
