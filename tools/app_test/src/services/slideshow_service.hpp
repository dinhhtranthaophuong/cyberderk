#pragma once
// Anh dai dien cho nua trai Main Screen: nguoi dung chon 1 anh (tu Album), service giai ma 1 lan
// o Core 0 va giu ket qua trong PSRAM. Khong chuyen anh tu dong, khong task chay nen lien tuc:
// task giai ma chi ton tai trong luc giai ma roi tu thoat.
//
// Duong dan anh duoc luu o /data/slideshow.txt tren the SD (thu muc /data phai co san).
// Khong goi LVGL o day; chi dien san lv_img_dsc_t de UI dung.
#include <lvgl.h>

namespace SlideshowService {

// ALBUM goi: chon anh hien tren Main Screen. Xoa cache cu va luu duong dan.
// Tra false neu duong dan khong hop le; neu ghi the SD loi thi van ap dung trong RAM
// (mat sau khi khoi dong lai) va cung tra false.
// Chi goi khi Main Screen KHONG hien thi (vd dang o Album), vi cache cu bi giai phong.
bool setPhoto(const char* path);

// Bo anh da chon (Main Screen hien lai khung trong). Cung dieu kien goi nhu setPhoto.
void clearPhoto();

// MAIN SCREEN goi trong onCreate: doc duong dan (lan dau), neu co anh ma chua co cache thi
// bat task Core 0 giai ma vua khung targetW x targetH. Khong chan.
void request(int targetW, int targetH);

// true khi khong con cho gi nua (da co anh, khong co anh, hoac giai ma that bai).
bool settled();

// Anh san sang hien thi, nullptr neu khong co. Con tro song den lan setPhoto/clearPhoto ke tiep
// (khong bi giai phong khi thoat Main Screen), nen widget khong can giu ban sao.
const lv_img_dsc_t* image();

}  // namespace SlideshowService
