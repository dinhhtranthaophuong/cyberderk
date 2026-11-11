#pragma once
#include <cstdint>

// Ban phim ma tran 3x3, thiet ke de nhe:
//  - Luc ranh: tat ca hang keo xuong LOW, cot INPUT_PULLUP, ngat FALLING tren 3 cot chi bat mot
//    co `wake`. poll() luc nay chi doc mot bien, khong dong vao GPIO.
//  - Co phim: huy ngat, quet 3 hang moi 5 ms (hang khong duoc quet de high-Z nen khong bao gio
//    noi tat hai hang), chong doi 2 lan quet lien tiep giong nhau. Tha het phim khoang 20 ms
//    thi tro ve trang thai ranh.
//
// Su kien:
//  - Phim 2..9: gui ngay khi nhan.
//  - Phim 1: nhan nhanh -> gui ESC luc THA; giu >= KEY_HOME_HOLD_MS -> gui KEY_HOME mot lan
//    (khi tha khong gui ESC nua).
//  - Nhan >= 2 phim cung luc: bo qua cho toi khi tha het (khong can diode, phim ma ghost cung
//    chi la ">= 2 phim").
//  - Phim dang bi giu luc khoi dong: bo qua cho toi khi tha.
namespace KeypadMatrix {

// Cau hinh chan va bat ngat. Goi mot lan luc khoi dong.
void init();

// Goi dinh ky (moi 10 ms trong read_cb cua lv_indev). Tra ma phim (LV_KEY_*, KEY_OPTION,
// KEY_HOME) neu co su kien, 0 neu khong. Moi lan goi tra toi da mot su kien.
uint32_t poll(uint32_t nowMs);

// True neu dang o trang thai ranh (dung cho test / chan doan).
bool isIdle();

}  // namespace KeypadMatrix
