#include "assets/icons/icons.hpp"

// Du lieu icon la RGB565 khong dao byte + alpha (3 byte/diem), khop lv_conf.h.
static_assert(LV_COLOR_DEPTH == 16 && LV_COLOR_16_SWAP == 0,
              "Du lieu icon sinh cho RGB565 khong dao byte; sinh lai neu doi lv_conf.h");

namespace {

constexpr size_t kIcons = static_cast<size_t>(Icons::Id::COUNT);

const lv_img_dsc_t* const kTable[Icons::kThemeCount][kIcons] = {
    {&icon_t0_music, &icon_t0_pet, &icon_t0_visual, &icon_t0_calendar, &icon_t0_album,
     &icon_t0_settings},
    {&icon_t1_music, &icon_t1_pet, &icon_t1_visual, &icon_t1_calendar, &icon_t1_album,
     &icon_t1_settings},
    {&icon_t2_music, &icon_t2_pet, &icon_t2_visual, &icon_t2_calendar, &icon_t2_album,
     &icon_t2_settings},
    {&icon_t3_music, &icon_t3_pet, &icon_t3_visual, &icon_t3_calendar, &icon_t3_album,
     &icon_t3_settings},
};

}  // namespace

namespace Icons {

const lv_img_dsc_t* get(Id id, uint8_t theme) {
  const size_t i = static_cast<size_t>(id);
  if (i >= kIcons) return nullptr;
  if (theme >= kThemeCount) theme = 0;  // chan chi so ngoai khoang
  return kTable[theme][i];
}

}  // namespace Icons
