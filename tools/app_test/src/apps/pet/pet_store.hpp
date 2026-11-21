#pragma once
// PetStore: lưu / nạp trạng thái Pet vào thẻ SD (/data/pet.txt). Phần nối với SdStorage và
// TimeService; định dạng file và logic thuần nằm ở pet_save và PetState.
//
// Cách dùng trong PetApp:
//   onCreate:  PetStore::load();                 // trước lần PetState::update() đầu tiên
//   hành động: PetStore::markDirty(now);         // sau Eat/Play/Bath/Sleep/Skin thành công
//   onTick:    PetStore::tick(now);              // ghi khi đã yên >= 3 giây (tránh ghi liên tục)
//   onDestroy: PetStore::flush();                // ghi ngay nếu còn thay đổi chưa ghi
//
// Thư mục /data phải có sẵn trên thẻ (SdStorage::writeFile không tự tạo thư mục cha).
// Chỉ gọi từ ngữ cảnh UI (Core 1). Ghi SD chặn vài chục ms.

#include <cstdint>

namespace PetStore {

// Chỉ chạy thật ở lần gọi đầu tiên sau khi khởi động, các lần sau bỏ qua. Không có file hoặc file
// hỏng thì giữ trạng thái mặc định. Có file thì khôi phục và tính bù thời gian đã tắt máy
// (giờ thật từ TimeService, chỉ số không tụt dưới 20, mô phỏng tối đa 24 giờ).
void load();

void markDirty(uint32_t nowMs);
void tick(uint32_t nowMs);
void flush();

}  // namespace PetStore
