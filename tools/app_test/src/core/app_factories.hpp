#pragma once
#include "core/app_base.hpp"

// Moi app cung cap MOT ham factory (dat trong apps/<ten>/<ten>_app.cpp, pham vi toan cuc,
// KHONG nam trong namespace), tra ve doi tuong cap phat bang `new`.
// AppManager so huu doi tuong va `delete` sau khi goi onDestroy().
//
// Khai bao la `weak`: app nao chua duoc viet thi AppManager tu hien man hinh
// "<TEN> (chua cai dat)", du an van build va chay duoc.
IApp* createMainScreenApp() __attribute__((weak));
IApp* createPetApp() __attribute__((weak));
IApp* createMusicApp() __attribute__((weak));
IApp* createVisualizerApp() __attribute__((weak));
IApp* createCalendarApp() __attribute__((weak));
IApp* createAlbumApp() __attribute__((weak));
IApp* createSettingsApp() __attribute__((weak));
IApp* createSettingsApp() __attribute__((weak));
