#pragma once
// Định dạng file lưu trạng thái Pet (/data/pet.txt) và tính giờ. Logic thuần (không SD, không
// Arduino) nên test được trên máy tính.
//
// Nội dung file (văn bản, mỗi dòng một giá trị):
//   cyberdeck-pet 1
//   hunger=80
//   fun=80
//   clean=80
//   energy=80
//   skin=0
//   asleep=0
//   time=1763700000      <- giờ địa phương lúc lưu, tính bằng giây kể từ 1970-01-01
//
// Khóa lạ bị bỏ qua; thiếu khóa, sai kiểu hoặc giá trị ngoài khoảng thì parse() trả false.

#include <cstddef>
#include <cstdint>
#include "apps/pet/pet_state.hpp"

namespace PetSave {

constexpr size_t kMaxText = 160;   // bộ đệm đủ cho format()

// Giây kể từ 1970-01-01 theo giờ địa phương (hai mốc cùng múi giờ nên trừ nhau ra thời gian trôi qua).
int64_t epochSeconds(int year, int month, int day, int hour, int minute, int second);

// Ghi ảnh chụp + mốc giờ thành văn bản. Trả số byte (không gồm '\0'), 0 nếu `cap` quá nhỏ.
size_t format(const PetSnapshot& snap, int64_t epoch, char* buf, size_t cap);

// Đọc văn bản. Trả false nếu sai (khi đó *snap và *epoch không đổi).
bool parse(const char* text, size_t len, PetSnapshot* snap, int64_t* epoch);

}  // namespace PetSave
