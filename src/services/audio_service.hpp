#pragma once
#include <Arduino.h>

enum class AudioState : uint8_t { STOPPED, PLAYING, PAUSED, ERROR };

// AudioService: phat MP3 tu SD qua I2S (MAX98357A), chay nen doc lap voi app.
// Moi ham goi duoc tu bat ky task nao, khong cham LVGL. Chi MP3 (CBR/VBR, 8-48 kHz).
namespace AudioService {
bool begin();                              // I2S + task decode o Core 0; goi lai khi da chay thi tra true
bool play(const char* path);               // MP3 tu SD (bat dau ngay, bo bai dang phat)
void pause();
void resume();
void stop();
void seek(int deltaSeconds);               // tua tuong doi (+/-)
void setVolume(uint8_t v0to100);

AudioState state();
uint32_t positionMs();
uint32_t durationMs();                     // 0 neu chua biet
bool finished();                           // bai vua ket thuc (doc xong tu xoa co)

// Cho Visualizer: sao chep toi da n mau PCM 16-bit mono gan nhat.
// Tra so mau thuc te (0 khi khong phat). Khong chan, khong khoa lau.
size_t readPcmTap(int16_t* dst, size_t n);
}