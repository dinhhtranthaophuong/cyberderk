#pragma once
// Theme giao dien: bang mau co ten (+ anh nen tuy chon). Quy tac cho moi app:
// KHONG ghi ma mau hex truc tiep, chi lay tu Themes::current(), vi du
//   lv_obj_set_style_bg_color(obj, lv_color_hex(Themes::current().panel), 0);
// Nhu vay them theme moi khong phai sua app nao.
//
// Chi goi tu ngu canh UI (dang giu LvglLock): onCreate/onTick/onKey va event callback.
#include <lvgl.h>

#include <cstddef>
#include <cstdint>

struct Theme {
  const char* key;   // khoa on dinh de luu vao flash, KHONG doi sau khi da phat hanh
  const char* name;  // ten hien thi (ASCII)

  uint32_t bg;           // nen man hinh
  uint32_t panel;        // khoi lon, nen o/nut noi bat
  uint32_t accent;       // mau nhan
  uint32_t text;         // chu tren nen man hinh
  uint32_t textOnPanel;  // chu tren khoi panel
  uint32_t focus;        // vien khi focus
  uint32_t icon;         // mau icon
  uint32_t scrim;        // khoi lam mo dat sau chu khi co anh nen
  uint8_t scrimOpa;      // do dac cua scrim, 0..255

  // Anh nen toan man hinh (RGB565, dung kich thuoc UI_W x UI_H). nullptr = nen mau `bg`.
  // Chu cua app trong suot, noi tren anh. Anh phai ton tai suot thoi gian theme duoc chon.
  const lv_img_dsc_t* bgImage;
};

namespace Themes {

size_t count();
const Theme& at(size_t index);  // index ngoai khoang -> theme dau tien
const Theme& current();

// Chon theme theo khoa. Tra false (va giu nguyen theme hien tai) neu khong co khoa do.
// Chua tu ve lai: sau khi chon, goi AppManager::reload() de dung lai man hinh hien tai.
bool select(const char* key);

// Ap nen + mau chu mac dinh len man hinh moi. AppManager goi khi dung man hinh cho app.
void applyToScreen(lv_obj_t* screen);

}  // namespace Themes
