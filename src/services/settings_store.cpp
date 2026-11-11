#include "services/settings_store.hpp"

#include <Arduino.h>
#include <Preferences.h>

#include <cstring>

#include "core/app_registry.hpp"
#include "core/theme.hpp"

namespace {

constexpr const char* kNamespace = "cyberdeck";
constexpr const char* kKeyTheme = "theme";
constexpr const char* kKeyApps = "apps";
constexpr size_t kBufLen = 128;

// Ap chuoi khoa app "music,pet,album" vao AppRegistry. Khoa la / trung / vuot gioi han bi bo qua.
void applyApps(char* list) {
  for (size_t i = 0; i < AppRegistry::count(); ++i) {
    const AppInfo& a = AppRegistry::at(i);
    if (a.selectable) AppRegistry::setSelected(a.id, false);
  }
  char* save = nullptr;
  for (char* tok = strtok_r(list, ",", &save); tok; tok = strtok_r(nullptr, ",", &save)) {
    const AppId id = AppRegistry::findByKey(tok);
    if (id != AppId::COUNT) AppRegistry::setSelected(id, true);
  }
}

}  // namespace

namespace SettingsStore {

void begin() {
  Preferences prefs;
  // Mo che do doc-ghi: chi che do nay tu tao namespace khi chua co du lieu.
  if (!prefs.begin(kNamespace, false)) {
    Serial.println("[Settings] khong mo duoc NVS, dung mac dinh");
    return;
  }
  char theme[24] = "";
  char apps[kBufLen] = "";
  const bool hasTheme = prefs.isKey(kKeyTheme);
  const bool hasApps = prefs.isKey(kKeyApps);
  if (hasTheme) prefs.getString(kKeyTheme, theme, sizeof(theme));
  if (hasApps) prefs.getString(kKeyApps, apps, sizeof(apps));
  prefs.end();

  if (hasTheme) Themes::select(theme);  // khoa la: giu theme mac dinh
  if (hasApps) applyApps(apps);
  Serial.printf("[Settings] theme=%s, %u app tren Main Screen\n", Themes::current().key,
                (unsigned)AppRegistry::selectedCount());
}

void save() {
  char apps[kBufLen] = "";
  size_t len = 0;
  for (size_t i = 0; i < AppRegistry::count(); ++i) {
    const AppInfo& a = AppRegistry::at(i);
    if (!a.selectable || !AppRegistry::isSelected(a.id)) continue;
    const size_t k = std::strlen(a.key);
    if (len + k + 2 > sizeof(apps)) break;
    if (len > 0) apps[len++] = ',';
    std::memcpy(apps + len, a.key, k);
    len += k;
    apps[len] = '\0';
  }

  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return;
  prefs.putString(kKeyTheme, Themes::current().key);
  prefs.putString(kKeyApps, apps);
  prefs.end();
}

}  // namespace SettingsStore
