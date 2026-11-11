// Sketch test the SD (khong dung TFT/LVGL). Chay bang env [env:sd_test].
// Kiem tra: mount, loai the, dung luong, liet ke /images va /music, do toc do doc.
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

#include "config/pins.hpp"

static SPIClass sdSpi(HSPI);  // bus rieng, khong trung voi TFT (FSPI)

static void listDir(const char* dir) {
  File d = SD.open(dir);
  if (!d || !d.isDirectory()) {
    Serial.printf("  [!] khong mo duoc thu muc %s\n", dir);
    return;
  }
  Serial.printf("  %s:\n", dir);
  int n = 0;
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    if (f.isDirectory()) continue;
    Serial.printf("    %-28s %8u bytes\n", f.name(), (unsigned)f.size());
    ++n;
  }
  Serial.printf("  -> %d file\n", n);
}

// Doc het mot file theo chunk 8 KB, in toc do KB/s.
static void speedTest(const char* path) {
  File f = SD.open(path);
  if (!f) {
    Serial.printf("  [!] khong mo duoc %s\n", path);
    return;
  }
  static uint8_t buf[8192];
  size_t total = 0;
  const uint32_t t0 = millis();
  int n;
  while ((n = f.read(buf, sizeof(buf))) > 0) total += n;
  const uint32_t ms = millis() - t0;
  f.close();
  Serial.printf("  doc %s: %u bytes trong %u ms = %.0f KB/s\n", path, (unsigned)total,
                (unsigned)ms, ms ? total / 1.024f / ms : 0.0f);
}

// Tim file lon nhat trong thu muc de do toc do.
static String biggest(const char* dir) {
  String best;
  size_t bestSize = 0;
  File d = SD.open(dir);
  if (!d) return best;
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    if (!f.isDirectory() && f.size() > bestSize) {
      bestSize = f.size();
      best = String(dir) + "/" + f.name();
    }
  }
  return best;
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n=== SD test ===");

  sdSpi.begin(Pins::SdSck, Pins::SdMiso, Pins::SdMosi, Pins::SdCs);
  uint32_t hz = 25000000;
  bool ok = SD.begin(Pins::SdCs, sdSpi, hz);
  if (!ok) {  // thu lai toc do thap hon (day dai / module kem)
    Serial.println("Mount 25 MHz that bai, thu 10 MHz...");
    hz = 10000000;
    ok = SD.begin(Pins::SdCs, sdSpi, hz);
  }
  if (!ok) {
    Serial.println("MOUNT THAT BAI. Kiem tra: day SCK/MOSI/MISO/CS, nguon VCC, the FAT32.");
    return;
  }

  const char* types[] = {"khong co", "MMC", "SD", "SDHC", "?"};
  const uint8_t t = SD.cardType();
  Serial.printf("Mount OK @ %u MHz | loai the: %s | dung luong: %llu MB\n", (unsigned)(hz / 1000000),
                types[t < 4 ? t : 4], SD.cardSize() / (1024ULL * 1024ULL));

  listDir("/images");
  listDir("/music");

  String img = biggest("/images");
  String mus = biggest("/music");
  if (img.length()) speedTest(img.c_str());
  if (mus.length()) speedTest(mus.c_str());
  Serial.println("=== xong ===");
}

void loop() { delay(1000); }
