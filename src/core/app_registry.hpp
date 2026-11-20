#pragma once
#include <cstddef>
#include <cstdint>

#include "core/app_base.hpp"

// Bang dang ky app: khoa luu tru, ten hien thi, icon, ham factory, va viec app nao duoc hien
// tren Main Screen. Them app moi: viet app + them 1 gia tri vao AppId + them 1 dong vao bang
// trong app_registry.cpp (+ khai bao factory weak trong app_factories.hpp).
struct AppInfo {
  AppId id;
  const char* key;    // khoa on dinh de luu vao flash. KHONG doi sau khi da phat hanh
  const char* label;  // ten ngan (ASCII, font mac dinh chua co dau tieng Viet)
  const void* icon;   // nullptr = launcher hien ten; sau nay: con tro anh/symbol LVGL
  IApp* (*create)();  // factory cua app; nullptr neu app chua duoc viet (hien man thay the)
  bool selectable;    // true = nguoi dung bat/tat hien thi tren Main Screen trong Settings
};

namespace AppRegistry {
constexpr size_t kLauncherSlots = 6;  // = GRID_COLS * GRID_ROWS

// O cuoi cua luoi luon la Settings, nen toi da kLauncherSlots - 1 app duoc chon.
#ifdef APPREG_MAX_SELECTED
constexpr size_t kMaxSelected = APPREG_MAX_SELECTED;  // chi de test
#else
constexpr size_t kMaxSelected = kLauncherSlots - 1;
#endif

// Thu tu o trong luoi launcher, duyet theo hang (trai -> phai, tren -> duoi): cac app dang
// chon theo thu tu trong bang, roi cac o trong (AppId::COUNT), o cuoi la Settings.
// Con tro on dinh, noi dung doi khi thay doi lua chon.
const AppId* launcherSlots();

// Thong tin cua mot app. Voi AppId::COUNT hoac id la tra ve muc rong (label rong, create null).
const AppInfo& info(AppId id);


constexpr uint8_t kThemeCount = 4;  // = Icons::kThemeCount

// Icon (lv_img_dsc_t*) của app theo theme. Theme ngoài khoảng -> theme 0.
// nullptr nếu app không có icon.
const void* icon(AppId id, uint8_t theme);

// Duyet toan bo bang (thu tu = thu tu hien trong Settings va tren Main Screen).
size_t count();
const AppInfo& at(size_t index);

// AppId::COUNT neu khong co khoa nay.
AppId findByKey(const char* key);

// Chon app hien tren Main Screen. Chi goi tu ngu canh UI.
bool isSelected(AppId id);
size_t selectedCount();
// Tra false (va khong doi gi) neu app khong chon duoc (selectable = false, khong co trong bang)
// hoac bat them khi da du kMaxSelected app.
bool setSelected(AppId id, bool on);
// Mac dinh: cac app chon duoc dau tien trong bang, toi da kMaxSelected.
void resetSelection();
}  // namespace AppRegistry
