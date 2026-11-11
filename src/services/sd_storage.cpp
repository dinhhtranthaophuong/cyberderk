#include "services/sd_storage.hpp"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <algorithm>
#include <cctype>
#include <cstring>

#include "config/pins.hpp"
#include "core/psram.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {

constexpr size_t kChunk = 8192;

SPIClass sdSpi(HSPI);  // FSPI (SPI2) dang danh cho TFT
SemaphoreHandle_t mtx = nullptr;
bool mounted = false;

struct Lock {
  Lock() { xSemaphoreTakeRecursive(mtx, portMAX_DELAY); }
  ~Lock() { xSemaphoreGiveRecursive(mtx); }
};

std::string lower(const std::string& s) {
  std::string r = s;
  for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return r;
}

bool hasExt(const std::string& name, const std::vector<std::string>& exts) {
  if (exts.empty()) return true;
  const std::string n = lower(name);
  for (const std::string& e : exts) {
    if (n.size() >= e.size() && n.compare(n.size() - e.size(), e.size(), e) == 0) return true;
  }
  return false;
}

std::vector<std::string> splitExts(const char* exts) {
  std::vector<std::string> out;
  if (!exts) return out;
  std::string cur;
  for (const char* p = exts;; ++p) {
    if (*p == ';' || *p == '\0') {
      if (!cur.empty()) out.push_back(lower(cur));
      cur.clear();
      if (*p == '\0') break;
    } else {
      cur += *p;
    }
  }
  return out;
}

}  // namespace

namespace SdStorage {

bool begin() {
  if (mounted) return true;
  if (!mtx) mtx = xSemaphoreCreateRecursiveMutex();
  if (!mtx) return false;

  sdSpi.begin(Pins::SdSck, Pins::SdMiso, Pins::SdMosi, Pins::SdCs);
  const uint32_t freqs[] = {25000000, 10000000, 4000000};
  for (uint32_t hz : freqs) {
    Lock lock;
    if (SD.begin(Pins::SdCs, sdSpi, hz)) {
      mounted = true;
      Serial.printf("[SD] mount OK @ %u MHz, %llu MB\n", (unsigned)(hz / 1000000),
                    (unsigned long long)(SD.cardSize() / (1024ULL * 1024ULL)));
      return true;
    }
    SD.end();
  }
  Serial.println("[SD] mount that bai (kiem tra day, nguon, the FAT32)");
  return false;
}

bool isMounted() { return mounted; }

std::vector<std::string> listFiles(const char* dir, const char* exts) {
  std::vector<std::string> out;
  if (!mounted || !dir) return out;
  const std::vector<std::string> want = splitExts(exts);
  std::string base = dir;
  if (base.empty() || base.back() != '/') base += '/';

  {
    Lock lock;
    File d = SD.open(dir);
    if (!d || !d.isDirectory()) return out;
    for (File f = d.openNextFile(); f; f = d.openNextFile()) {
      if (f.isDirectory()) continue;
      std::string name = f.name();
      const size_t slash = name.find_last_of('/');
      if (slash != std::string::npos) name = name.substr(slash + 1);  // phong khi core tra ca duong dan
      if (name.empty() || name[0] == '.') continue;
      if (!hasExt(name, want)) continue;
      out.push_back(base + name);
    }
  }
  std::sort(out.begin(), out.end(),
            [](const std::string& a, const std::string& b) { return lower(a) < lower(b); });
  return out;
}

bool exists(const char* path) {
  if (!mounted || !path) return false;
  Lock lock;
  return SD.exists(path);
}

void* readFileToPsram(const char* path, size_t* outSize) {
  if (outSize) *outSize = 0;
  if (!mounted || !path) return nullptr;

  File f;
  size_t size = 0;
  {
    Lock lock;
    f = SD.open(path);
    if (!f || f.isDirectory()) return nullptr;
    size = f.size();
  }
  if (size == 0) return nullptr;
  uint8_t* buf = static_cast<uint8_t*>(Psram::alloc(size));
  if (!buf) return nullptr;

  size_t got = 0;
  while (got < size) {
    const size_t want = std::min(kChunk, size - got);
    size_t n;
    {
      Lock lock;  // nha khoa giua cac chunk de task khac (audio) khong bi doi lau
      n = f.read(buf + got, want);
    }
    if (n == 0) break;
    got += n;
  }
  {
    Lock lock;
    f.close();
  }
  if (got != size) {
    Psram::release(buf);
    return nullptr;
  }
  if (outSize) *outSize = size;
  return buf;
}

bool writeFile(const char* path, const void* data, size_t len) {
  if (!mounted || !path || (!data && len)) return false;
  File f;
  {
    Lock lock;
    f = SD.open(path, FILE_WRITE);
    if (!f) return false;
  }
  const uint8_t* p = static_cast<const uint8_t*>(data);
  size_t done = 0;
  bool ok = true;
  while (done < len) {
    const size_t n = std::min(kChunk, len - done);
    Lock lock;
    if (f.write(p + done, n) != n) {
      ok = false;
      break;
    }
    done += n;
  }
  Lock lock;
  f.close();
  return ok;
}

bool Reader::open(const char* path) {
  close();
  if (!mounted || !path) return false;
  Lock lock;
  file_ = SD.open(path);
  if (!file_ || file_.isDirectory()) {
    file_ = File();
    return false;
  }
  size_ = file_.size();
  open_ = true;
  return true;
}

size_t Reader::read(void* dst, size_t n) {
  if (!open_ || !dst || n == 0) return 0;
  Lock lock;
  return file_.read(static_cast<uint8_t*>(dst), n);
}

bool Reader::seek(size_t pos) {
  if (!open_) return false;
  Lock lock;
  return file_.seek(pos);
}

void Reader::close() {
  if (!open_) return;
  {
    Lock lock;
    file_.close();
  }
  file_ = File();
  open_ = false;
  size_ = 0;
}

}  // namespace SdStorage
