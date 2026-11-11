#include "core/theme.hpp"

#include <cstring>

namespace {

// Them theme moi: them mot khoi vao cuoi mang (khong doi khoa cua theme da co).
// Theme dau tien la mac dinh.
const Theme kThemes[] = {
    {
        "pink", "Sakura",
        /*bg*/ 0x471327, /*panel*/ 0xFFB7CE, /*accent*/ 0xF4A9BC,
        /*text*/ 0xFFFFFF, /*textOnPanel*/ 0x7A3B52, /*focus*/ 0xFFFFFF, /*icon*/ 0xF4A9BC,
        /*scrim*/ 0x000000, /*scrimOpa*/ 120, /*bgImage*/ nullptr,
    },
    {
        "night", "Dull Night",
        /*bg*/ 0x0A0F1E, /*panel*/ 0x2A346E, /*accent*/ 0x8FB3FF,
        /*text*/ 0xFFFFFF, /*textOnPanel*/ 0x0A1230, /*focus*/ 0xFFFFFF, /*icon*/ 0x8FB3FF,
        /*scrim*/ 0x000000, /*scrimOpa*/ 120, /*bgImage*/ nullptr,
    },
    {
        "mint", "Mint Leaves",
        /*bg*/ 0x112B1F, /*panel*/ 0x96D4A7, /*accent*/ 0x9FE8C6,
        /*text*/ 0xFFFFFF, /*textOnPanel*/ 0x0B3A28, /*focus*/ 0xFFFFFF, /*icon*/ 0x9FE8C6,
        /*scrim*/ 0x000000, /*scrimOpa*/ 120, /*bgImage*/ nullptr,
    },
   {
        "sundaysky", "Sunday Sky",
        /*bg*/ 0xCDEBFA, /*panel*/ 0x7EC8E3, /*accent*/ 0x5BB5D9,
        /*text*/ 0x1F4E6B, /*textOnPanel*/ 0x103A52, /*focus*/ 0x1F4E6B, /*icon*/ 0xFFFFFF,
        /*scrim*/ 0xFFFFFF, /*scrimOpa*/ 120, /*bgImage*/ nullptr,
    },
};

constexpr size_t kCount = sizeof(kThemes) / sizeof(kThemes[0]);
size_t currentIdx = 0;

}  // namespace

namespace Themes {

size_t count() { return kCount; }

const Theme& at(size_t index) { return kThemes[index < kCount ? index : 0]; }

const Theme& current() { return kThemes[currentIdx]; }

bool select(const char* key) {
  if (!key) return false;
  for (size_t i = 0; i < kCount; ++i) {
    if (std::strcmp(kThemes[i].key, key) == 0) {
      currentIdx = i;
      return true;
    }
  }
  return false;
}

void applyToScreen(lv_obj_t* screen) {
  const Theme& t = current();
  lv_obj_set_style_bg_color(screen, lv_color_hex(t.bg), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  // Theme mac dinh cua LVGL la theme SANG (chu toi). Dat chu theo theme de label khong tu
  // dat mau luon doc duoc tren nen cua theme (widget con ke thua mau nay).
  lv_obj_set_style_text_color(screen, lv_color_hex(t.text), 0);
  if (t.bgImage) {
    lv_obj_set_style_bg_img_src(screen, t.bgImage, 0);
  }
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

}  // namespace Themes
