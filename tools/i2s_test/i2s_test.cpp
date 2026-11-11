// Test I2S: phat tieng bip 440 Hz qua MAX98357A + loa (khong thuoc firmware chinh).
// Build/nap: pio run -e i2s_test -t upload   (monitor: pio device monitor)
// Chan theo config/pins.hpp: BCLK=4, LRC=5, DIN=6.
// Duong am luong nho (bien do 3000/32767) de khong hu loa. Tang AMPLITUDE neu qua nho.
#include <Arduino.h>
#include <driver/i2s.h>
#include <cmath>

static constexpr int PIN_BCLK = 4;
static constexpr int PIN_LRC = 5;
static constexpr int PIN_DIN = 6;

static constexpr i2s_port_t kPort = I2S_NUM_0;
static constexpr uint32_t kSampleRate = 44100;
static constexpr float kFreq = 440.0f;
static constexpr int16_t kAmplitude = 3000;   // ~9% full scale
static constexpr uint32_t kBeepOnMs = 1000;
static constexpr uint32_t kBeepOffMs = 1000;
static constexpr size_t kFrames = 256;        // 1 frame = L + R

static int16_t gBuf[kFrames * 2];
static float gPhase = 0.0f;
static uint32_t gPhaseStartMs = 0;
static bool gBeepOn = true;
static uint32_t gCount = 0;

static void fillBuffer(bool beep) {
  const float step = 2.0f * (float)M_PI * kFreq / (float)kSampleRate;
  for (size_t i = 0; i < kFrames; i++) {
    int16_t s = 0;
    if (beep) {
      s = (int16_t)(sinf(gPhase) * kAmplitude);
      gPhase += step;
      if (gPhase > 2.0f * (float)M_PI) gPhase -= 2.0f * (float)M_PI;
    }
    gBuf[i * 2] = s;       // trai
    gBuf[i * 2 + 1] = s;   // phai (cung gia tri: dung ca khi chan SD cua module la L/R/mono)
  }
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) { yield(); }
  Serial.println("[I2S test] bat dau");

  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = kSampleRate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;

  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = PIN_BCLK;
  pins.ws_io_num = PIN_LRC;
  pins.data_out_num = PIN_DIN;
  pins.data_in_num = I2S_PIN_NO_CHANGE;

  esp_err_t e = i2s_driver_install(kPort, &cfg, 0, nullptr);
  Serial.printf("[I2S test] driver_install: %s\n", esp_err_to_name(e));
  e = i2s_set_pin(kPort, &pins);
  Serial.printf("[I2S test] set_pin: %s\n", esp_err_to_name(e));
  i2s_zero_dma_buffer(kPort);
  gPhaseStartMs = millis();
}

void loop() {
  uint32_t now = millis();
  uint32_t span = gBeepOn ? kBeepOnMs : kBeepOffMs;
  if (now - gPhaseStartMs >= span) {
    gBeepOn = !gBeepOn;
    gPhaseStartMs = now;
    if (gBeepOn) {
      gCount++;
      Serial.printf("[I2S test] bip #%lu\n", (unsigned long)gCount);
    }
  }
  fillBuffer(gBeepOn);
  size_t written = 0;
  // Blocking theo DMA, tu nhien nhip theo toc do I2S (khong dung delay()).
  i2s_write(kPort, gBuf, sizeof(gBuf), &written, portMAX_DELAY);
}