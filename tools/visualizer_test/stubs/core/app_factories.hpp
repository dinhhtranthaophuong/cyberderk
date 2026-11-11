#pragma once
// Bản sao ARCHITECTURE.md mục 6.9 cho test máy tính.
#include "app_base.hpp"

IApp* createMainScreenApp() __attribute__((weak));
IApp* createPetApp()        __attribute__((weak));
IApp* createMusicApp()      __attribute__((weak));
IApp* createVisualizerApp() __attribute__((weak));
IApp* createCalendarApp()   __attribute__((weak));
IApp* createAlbumApp()      __attribute__((weak));
IApp* createSettingsApp()   __attribute__((weak));
