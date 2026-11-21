#include "apps/pet/pet_store.hpp"

#include <Arduino.h>
#include "apps/pet/pet_save.hpp"
#include "apps/pet/pet_state.hpp"
#include "core/psram.hpp"
#include "services/sd_storage.hpp"
#include "services/time_service.hpp"

namespace {

constexpr const char* kPath = "/data/pet.txt";
constexpr uint32_t kSaveDelayMs = 3000;   // ghi sau khi 3 giây không có thay đổi mới

bool     s_loaded  = false;
bool     s_dirty   = false;
uint32_t s_dirtyAt = 0;

int64_t nowEpoch() {
    const DateTime t = TimeService::now();
    return PetSave::epochSeconds(t.year, t.month, t.day, t.hour, t.minute, t.second);
}

void save() {
    s_dirty = false;   // thất bại cũng không thử lại liên tục (mỗi lần gọi SD hỏng có thể chặn ~300 ms)
    char buf[PetSave::kMaxText];
    const size_t n = PetSave::format(PetState::instance().snapshot(), nowEpoch(), buf, sizeof(buf));
    if (n == 0 || !SdStorage::writeFile(kPath, buf, n)) {
        Serial.println("[PetStore] khong ghi duoc /data/pet.txt");
    }
}

}  // namespace

namespace PetStore {

void load() {
    if (s_loaded) return;
    s_loaded = true;

    size_t size = 0;
    void* data = SdStorage::readFileToPsram(kPath, &size);
    if (data == nullptr) return;   // chưa có file (lần đầu) hoặc không đọc được: giữ mặc định

    PetSnapshot snap;
    int64_t savedAt = 0;
    const bool ok = PetSave::parse(static_cast<const char*>(data), size, &snap, &savedAt);
    Psram::release(data);
    if (!ok) {
        Serial.println("[PetStore] /data/pet.txt hong, dung trang thai mac dinh");
        return;
    }

    // Thời gian trôi qua: giờ hiện tại trừ mốc lúc lưu. Đồng hồ chỉnh lùi thì coi như 0.
    int64_t elapsed = nowEpoch() - savedAt;
    if (elapsed < 0) elapsed = 0;
    if (elapsed > 0x7FFFFFFF) elapsed = 0x7FFFFFFF;
    PetState::instance().restore(snap, static_cast<uint32_t>(elapsed));
    Serial.printf("[PetStore] nap trang thai, tat may %lld giay\n", static_cast<long long>(elapsed));
}

void markDirty(uint32_t nowMs) {
    s_dirty = true;
    s_dirtyAt = nowMs;
}

void tick(uint32_t nowMs) {
    if (s_dirty && static_cast<uint32_t>(nowMs - s_dirtyAt) >= kSaveDelayMs) save();
}

void flush() {
    if (s_dirty) save();
}

}  // namespace PetStore
