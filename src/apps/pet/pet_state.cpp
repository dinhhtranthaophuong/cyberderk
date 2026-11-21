#include "apps/pet/pet_state.hpp"

// ---------------------------------------------------------------------------
// TỐC ĐỘ GIẢM CHỈ SỐ
//   1 = nhanh: để thử khi đang phát triển app
//   0 = chậm:  dùng thật
// Khi dùng thật chỉ cần đổi số 1 dưới đây thành 0 (hoặc thêm -DPET_DECAY_FAST=0
// vào build_flags).
// ---------------------------------------------------------------------------
#ifndef PET_DECAY_FAST
#define PET_DECAY_FAST 0
#endif

namespace {

#if PET_DECAY_FAST
// no, vui, sạch: về ~0 sau 10 / 13 / 20 phút.
constexpr uint32_t kMsPerPoint[3]    = {6000u, 8000u, 12000u};
constexpr uint32_t kEnergyDrainMs    = 10000u;   // thức: mất 1 điểm năng lượng (100 điểm ~ 17 phút)
constexpr uint32_t kEnergyRecoverMs  = 3000u;    // ngủ: hồi 1 điểm (100 điểm ~ 5 phút)
#else
// no, vui, sạch: về ~0 sau khoảng 1,7 / 2,2 / 3,3 giờ.
constexpr uint32_t kMsPerPoint[3]    = {60000u, 80000u, 120000u};
constexpr uint32_t kEnergyDrainMs    = 120000u;  // ~3,3 giờ thức thì tới giờ ngủ
constexpr uint32_t kEnergyRecoverMs  = 30000u;   // ~50 phút ngủ thì dậy
#endif

constexpr uint8_t  kStartValue         = 80;     // giá trị ban đầu của mỗi chỉ số
constexpr uint32_t kHappyMs            = 2000;   // thời gian vui sau một hành động
constexpr uint8_t  kSadBelow           = 40;     // mặt nạ tối quá 3/5 (giá trị dưới 40) thì mèo buồn
constexpr uint32_t kSleepStatDecayDiv  = 2;      // khi ngủ, no/vui/sạch giảm chậm đi 2 lần (đặt 1 để tắt)

// Tắt máy lâu (restore): chỉ số no/vui/sạch không tụt dưới 20 (hoặc dưới giá trị đã lưu nếu thấp hơn).
constexpr uint8_t  kOfflineFloor       = 20;
constexpr uint32_t kOfflineMaxSec      = 24u * 3600u;   // mô phỏng tối đa 24 giờ (đã chạm sàn từ lâu)
constexpr uint32_t kOfflineStepMs      = 30000u;        // mô phỏng theo từng bước 30 giây

constexpr int idx(PetStat s) { return static_cast<int>(s); }

// Cộng dồn dt vào phần dư acc, trả số điểm trọn vẹn đã qua (tối đa 255), giữ lại phần lẻ.
uint32_t takePoints(uint32_t& acc, uint32_t dt, uint32_t msPerPoint) {
    const uint64_t total = static_cast<uint64_t>(acc) + dt;
    acc = static_cast<uint32_t>(total % msPerPoint);
    const uint64_t pts = total / msPerPoint;
    return pts > 255 ? 255u : static_cast<uint32_t>(pts);
}

}  // namespace

PetState::PetState() {
    for (int i = 0; i < kStatCount; ++i) val_[i] = kStartValue;
}

PetState& PetState::instance() {
    static PetState s;
    return s;
}

void PetState::update(uint32_t nowMs) {
    if (!started_) {
        started_ = true;
        lastMs_ = nowMs;
        return;
    }
    const uint32_t dt = nowMs - lastMs_;   // đúng cả khi millis() tràn số
    lastMs_ = nowMs;

    advance(dt);

    if (happy_ && static_cast<int32_t>(happyUntil_ - nowMs) <= 0) happy_ = false;
}

void PetState::advance(uint32_t dt) {
    // no, vui, sạch: giảm theo thời gian (chậm hơn khi đang ngủ).
    const uint32_t dtStat = asleep_ ? dt / kSleepStatDecayDiv : dt;
    for (int i = 0; i < 3; ++i) {
        const uint32_t pts = takePoints(acc_[i], dtStat, kMsPerPoint[i]);
        val_[i] = (pts >= val_[i]) ? 0 : static_cast<uint8_t>(val_[i] - pts);
    }

    updateEnergy(dt);
}

PetSnapshot PetState::snapshot() const {
    PetSnapshot s;
    for (int i = 0; i < kStatCount; ++i) s.value[i] = val_[i];
    s.skin = skin_;
    s.asleep = asleep_;
    return s;
}

void PetState::restore(const PetSnapshot& s, uint32_t offlineSec) {
    for (int i = 0; i < kStatCount; ++i) {
        val_[i] = (s.value[i] > kMax) ? kMax : s.value[i];
        acc_[i] = 0;
    }
    skin_ = (s.skin < PetSkin::COUNT) ? s.skin : PetSkin::CALICO;
    asleep_ = s.asleep;
    happy_ = false;
    started_ = false;   // update() kế tiếp chỉ ghi lại mốc thời gian

    uint8_t floorVal[3];
    for (int i = 0; i < 3; ++i) floorVal[i] = (val_[i] < kOfflineFloor) ? val_[i] : kOfflineFloor;

    if (offlineSec > kOfflineMaxSec) offlineSec = kOfflineMaxSec;
    uint32_t leftMs = offlineSec * 1000u;
    while (leftMs > 0) {
        const uint32_t dt = (leftMs < kOfflineStepMs) ? leftMs : kOfflineStepMs;
        advance(dt);
        leftMs -= dt;
    }
    for (int i = 0; i < 3; ++i) {
        if (val_[i] < floorVal[i]) val_[i] = floorVal[i];
    }
}

void PetState::updateEnergy(uint32_t dtMs) {
    const int e = idx(PetStat::ENERGY);
    if (asleep_) {
        // Ngủ: hồi dần, đầy thì tự dậy.
        const uint32_t pts = takePoints(acc_[e], dtMs, kEnergyRecoverMs);
        const uint32_t v = static_cast<uint32_t>(val_[e]) + pts;
        val_[e] = (v >= kMax) ? kMax : static_cast<uint8_t>(v);
        if (val_[e] >= kMax) {
            asleep_ = false;
            acc_[e] = 0;
        }
    } else {
        // Thức: giảm dần; vừa chạm 0 (hết một vòng) thì đi ngủ.
        // Nếu người dùng đánh thức khi năng lượng đang 0 thì không ngủ lại ngay.
        const uint32_t pts = takePoints(acc_[e], dtMs, kEnergyDrainMs);
        if (pts > 0 && val_[e] > 0) {
            val_[e] = (pts >= val_[e]) ? 0 : static_cast<uint8_t>(val_[e] - pts);
            if (val_[e] == 0) {
                asleep_ = true;
                happy_ = false;
                acc_[e] = 0;
            }
        }
    }
}

uint8_t PetState::value(PetStat stat) const {
    return val_[idx(stat)];
}

void PetState::add(PetStat stat, int delta) {
    int v = static_cast<int>(val_[idx(stat)]) + delta;
    if (v < 0) v = 0;
    if (v > kMax) v = kMax;
    val_[idx(stat)] = static_cast<uint8_t>(v);
}

void PetState::makeHappy(uint32_t nowMs) {
    happy_ = true;
    happyUntil_ = nowMs + kHappyMs;
}

bool PetState::eat(uint32_t nowMs) {
    if (asleep_) return false;
    add(PetStat::HUNGER, +30);
    add(PetStat::CLEAN, -5);
    makeHappy(nowMs);
    return true;
}

bool PetState::play(uint32_t nowMs) {
    if (asleep_) return false;
    add(PetStat::FUN, +25);
    add(PetStat::HUNGER, -8);
    makeHappy(nowMs);
    return true;
}

bool PetState::bath(uint32_t nowMs) {
    if (asleep_) return false;
    add(PetStat::CLEAN, +40);
    makeHappy(nowMs);
    return true;
}

bool PetState::toggleSleep() {
    const int e = idx(PetStat::ENERGY);
    if (asleep_) {                         // đánh thức; năng lượng giữ nguyên
        asleep_ = false;
        acc_[e] = 0;
        return true;
    }
    if (val_[e] >= kMax) return false;     // chưa buồn ngủ
    asleep_ = true;
    happy_ = false;
    acc_[e] = 0;
    return true;
}

void PetState::nextSkin() {
    const uint8_t n = static_cast<uint8_t>(PetSkin::COUNT);
    skin_ = static_cast<PetSkin>((static_cast<uint8_t>(skin_) + 1) % n);
}

PetMood PetState::mood() const {
    if (asleep_) return PetMood::SLEEPING;
    if (happy_) return PetMood::HAPPY;
    if (val_[idx(PetStat::HUNGER)] < kSadBelow ||
        val_[idx(PetStat::FUN)]    < kSadBelow ||
        val_[idx(PetStat::CLEAN)]  < kSadBelow) return PetMood::SAD;
    return PetMood::NORMAL;
}
