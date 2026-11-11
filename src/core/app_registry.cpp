#include "core/app_registry.hpp"

#include <cstring>

#include "config/display.hpp"
#include "assets/icons/icons.hpp"
#include "core/app_factories.hpp"

static_assert(AppRegistry::kLauncherSlots == static_cast<size_t>(GRID_COLS * GRID_ROWS),
              "kLauncherSlots phai bang GRID_COLS * GRID_ROWS");
static_assert(AppRegistry::kThemeCount == Icons::kThemeCount,
              "kThemeCount phai bang Icons::kThemeCount");
static_assert(AppRegistry::kMaxSelected < AppRegistry::kLauncherSlots,
              "Phai con it nhat 1 o cho Settings");

namespace {

// Thu tu cac app co selectable = true cung la thu tu hien trong Settings va tren Main Screen
// (mac dinh theo mockup: Music, Pet, Calendar, Visualizer, Album).
const AppInfo kInfos[] = {
    {AppId::APP_MUSIC_PLAYER, "music", "Music", &icon_t0_music, createMusicApp, true},
    {AppId::APP_PET, "pet", "Pet", &icon_t0_pet, createPetApp, true},
    {AppId::APP_CALENDAR, "calendar", "Cal", &icon_t0_calendar, createCalendarApp, true},
    {AppId::APP_VISUALIZER, "visualizer", "Viz", &icon_t0_visual, createVisualizerApp, true},
    {AppId::APP_ALBUM, "album", "Album", &icon_t0_album, createAlbumApp, true},
    {AppId::MAIN_SCREEN, "main", "Main", nullptr, createMainScreenApp, false},
    {AppId::APP_SETTINGS, "settings", "Set", &icon_t0_settings, createSettingsApp, false},
};
constexpr size_t kInfoCount = sizeof(kInfos) / sizeof(kInfos[0]);

const AppInfo kEmpty = {AppId::COUNT, "", "", nullptr, nullptr, false};

bool selected[kInfoCount] = {};
AppId slots[AppRegistry::kLauncherSlots];
bool ready = false;

void rebuild() {
  size_t n = 0;
  for (size_t i = 0; i < kInfoCount && n < AppRegistry::kMaxSelected; ++i) {
    if (kInfos[i].selectable && selected[i]) slots[n++] = kInfos[i].id;
  }
  while (n < AppRegistry::kLauncherSlots - 1) slots[n++] = AppId::COUNT;
  slots[AppRegistry::kLauncherSlots - 1] = AppId::APP_SETTINGS;
}

int indexOfId(AppId id) {
  for (size_t i = 0; i < kInfoCount; ++i) {
    if (kInfos[i].id == id) return static_cast<int>(i);
  }
  return -1;
}

// Khoi tao muon (lan goi dau), tranh phu thuoc thu tu khoi tao cua bien tinh.
void ensureReady() {
  if (ready) return;
  ready = true;
  AppRegistry::resetSelection();
}

}  // namespace

namespace AppRegistry {

const AppId* launcherSlots() {
  ensureReady();
  return slots;
}

const AppInfo& info(AppId id) {
  const int i = indexOfId(id);
  return i >= 0 ? kInfos[i] : kEmpty;
}

size_t count() { return kInfoCount; }

const AppInfo& at(size_t index) { return index < kInfoCount ? kInfos[index] : kEmpty; }

AppId findByKey(const char* key) {
  if (!key) return AppId::COUNT;
  for (size_t i = 0; i < kInfoCount; ++i) {
    if (std::strcmp(kInfos[i].key, key) == 0) return kInfos[i].id;
  }
  return AppId::COUNT;
}

bool isSelected(AppId id) {
  ensureReady();
  const int i = indexOfId(id);
  return i >= 0 && kInfos[i].selectable && selected[i];
}

size_t selectedCount() {
  ensureReady();
  size_t n = 0;
  for (size_t i = 0; i < kInfoCount; ++i) {
    if (kInfos[i].selectable && selected[i]) ++n;
  }
  return n;
}

bool setSelected(AppId id, bool on) {
  ensureReady();
  const int i = indexOfId(id);
  if (i < 0 || !kInfos[i].selectable) return false;
  if (on && !selected[i]) {
    if (selectedCount() >= kMaxSelected) return false;
  }
  selected[i] = on;
  rebuild();
  return true;
}

void resetSelection() {
  ready = true;  // tranh de quy voi ensureReady()
  size_t n = 0;
  for (size_t i = 0; i < kInfoCount; ++i) {
    selected[i] = kInfos[i].selectable && n < kMaxSelected;
    if (selected[i]) ++n;
  }
  rebuild();
}


const void* icon(AppId id, uint8_t theme) {
  Icons::Id iid;
  switch (id) {
    case AppId::APP_MUSIC_PLAYER: iid = Icons::Id::MUSIC; break;
    case AppId::APP_PET:          iid = Icons::Id::PET; break;
    case AppId::APP_VISUALIZER:   iid = Icons::Id::VISUAL; break;
    case AppId::APP_CALENDAR:     iid = Icons::Id::CALENDAR; break;
    case AppId::APP_ALBUM:        iid = Icons::Id::ALBUM; break;
    case AppId::APP_SETTINGS:     iid = Icons::Id::SETTINGS; break;
    default:                      return nullptr;  // MAIN_SCREEN, ô trống
  }
  return Icons::get(iid, theme);
}

}  // namespace AppRegistry
