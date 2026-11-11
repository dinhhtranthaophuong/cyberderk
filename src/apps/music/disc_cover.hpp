#pragma once
#include <lvgl.h>

#include <cstdint>

// Anh mat dia cho Music Player: dung chung anh da chon cho widget Main Screen
// (duong dan trong /data/slideshow.txt, dat tu Album bang phim 3).
//
// Task Core 0 giai ma anh, cat vuong chinh giua (kieu "cover"), roi DUNG SAN kFrames khung
// da xoay san (moi khung lech kStepDeg do), da cat tron, phan goc to mau nen theme.
// Luc phat, UI chi doi src sang khung ke tiep (khong con phep xoay/mat na cua LVGL).
// Cache trong PSRAM (kFrames * d * d * 2 byte); vao/ra Music khong dung lai neu
// anh, kich thuoc va mau goc khong doi.
namespace DiscCover {
constexpr int kFrames = 45;
constexpr int kStepDeg = 360 / kFrames;   // 8 do

// Goi tu UI (dang giu LvglLock) trong onCreate. `diameter` la canh khung vuong (= duong kinh dia),
// `cornerColor` la mau nen theme (0xRRGGBB) de to phan goc ngoai hinh tron.
// Khong chan lau (chi doc file duong dan nho). Theme co anh nen (bgImage) se lo goc.
void request(int diameter, uint32_t cornerColor);

// true khi khong con cho (da co anh, khong co anh, hoac loi).
bool settled();

// So khung san sang (0 neu khong co anh / loi / dang dung).
int frameCount();

// Khung thu `index` (lay du theo frameCount). nullptr neu khong co.
// Con tro song den lan request() ke tiep co doi anh/kich thuoc/mau.
const lv_img_dsc_t* frame(int index);
}