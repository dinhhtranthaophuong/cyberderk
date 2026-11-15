// STUB SdStorage: the SD gia lap = thu muc that, co chen loi (doc loi, cham, thao the).
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <mutex>

#include "core/psram.hpp"
#include "freertos/semphr.h"
#include "services/sd_storage.hpp"
#include "test_hooks.hpp"

namespace hooks {
std::atomic<int> sdMounted(1);
std::atomic<long> sdFailReadAfter(-1);
std::atomic<int> sdReadDelayUs(0);
std::atomic<int> sdOpenFail(0);
static std::string g_root;
void sdSetRoot(const std::string& r) { g_root = r; }
}  // namespace hooks

static long g_readSinceOpen = 0;

static SemaphoreHandle_t sdLock() {
  static SemaphoreHandle_t m = stubMutexCreate(100, true, "SD");   // hang cao nhat: sau gCtlMtx, gRingMtx
  return m;
}
struct SdGuard {
  SdGuard() { xSemaphoreTake(sdLock(), portMAX_DELAY); }
  ~SdGuard() { xSemaphoreGive(sdLock()); }
};

namespace SdStorage {
bool begin() { return !hooks::g_root.empty(); }
bool isMounted() { return !hooks::g_root.empty() && hooks::sdMounted.load() != 0; }

std::vector<std::string> listFiles(const char* dir, const char* exts) {
  std::vector<std::string> out;
  if (!isMounted()) return out;
  SdGuard g;
  DIR* d = opendir((hooks::g_root + dir).c_str());
  if (!d) return out;
  std::string ex = exts ? exts : "";
  while (dirent* e = readdir(d)) {
    std::string n = e->d_name;
    if (n.empty() || n[0] == '.') continue;
    if (!ex.empty()) {
      size_t dot = n.rfind('.');
      if (dot == std::string::npos) continue;
      std::string ext = n.substr(dot);
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      if ((";" + ex + ";").find(";" + ext + ";") == std::string::npos) continue;
    }
    out.push_back(std::string(dir) + "/" + n);
  }
  closedir(d);
  std::sort(out.begin(), out.end());
  return out;
}
bool exists(const char* path) {
  if (!isMounted()) return false;
  struct stat st;
  return stat((hooks::g_root + path).c_str(), &st) == 0;
}
void* readFileToPsram(const char* path, size_t* outSize) {
  if (outSize) *outSize = 0;
  if (!isMounted()) return nullptr;
  SdGuard g;
  FILE* f = fopen((hooks::g_root + path).c_str(), "rb");
  if (!f) return nullptr;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (n <= 0) { fclose(f); return nullptr; }
  void* p = Psram::alloc((size_t)n);
  if (!p) { fclose(f); return nullptr; }
  size_t got = fread(p, 1, (size_t)n, f);
  fclose(f);
  if (got != (size_t)n) { Psram::release(p); return nullptr; }
  if (outSize) *outSize = got;
  return p;
}
bool writeFile(const char* path, const void* data, size_t len) {
  if (!isMounted()) return false;
  SdGuard g;
  FILE* f = fopen((hooks::g_root + path).c_str(), "wb");
  if (!f) return false;
  size_t w = fwrite(data, 1, len, f);
  fclose(f);
  return w == len;
}

bool Reader::open(const char* path) {
  close();
  if (!isMounted() || hooks::sdOpenFail.load()) return false;
  SdGuard g;
  file_.fp = fopen((hooks::g_root + path).c_str(), "rb");
  if (!file_.fp) return false;
  struct stat st;
  if (fstat(fileno(file_.fp), &st) != 0 || !S_ISREG(st.st_mode)) { fclose(file_.fp); file_.fp = nullptr; return false; }
  size_ = (size_t)st.st_size;
  open_ = true;
  g_readSinceOpen = 0;
  return true;
}
size_t Reader::read(void* dst, size_t n) {
  if (!open_) return 0;
  SdGuard g;
  if (!isMounted()) return 0;
  long fa = hooks::sdFailReadAfter.load();
  if (fa >= 0) {
    if (g_readSinceOpen >= fa) return 0;
    if ((long)n > fa - g_readSinceOpen) n = (size_t)(fa - g_readSinceOpen);
  }
  int d = hooks::sdReadDelayUs.load();
  if (d > 0) usleep((useconds_t)d);
  size_t got = fread(dst, 1, n, file_.fp);
  g_readSinceOpen += (long)got;
  return got;
}
bool Reader::seek(size_t pos) {
  if (!open_) return false;
  SdGuard g;
  return fseek(file_.fp, (long)pos, SEEK_SET) == 0;
}
void Reader::close() {
  if (file_.fp) { fclose(file_.fp); file_.fp = nullptr; }
  open_ = false;
  size_ = 0;
}
}  // namespace SdStorage
