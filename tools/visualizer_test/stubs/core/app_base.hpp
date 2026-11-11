#pragma once
// Bản sao đúng ARCHITECTURE.md mục 6.2 cho test máy tính.
#include <lvgl.h>
#include <cstdint>

enum class AppId : uint8_t {
    MAIN_SCREEN, APP_PET, APP_MUSIC_PLAYER,
    APP_VISUALIZER, APP_CALENDAR, APP_ALBUM,
    APP_SETTINGS, COUNT
};

class IApp {
public:
    virtual ~IApp() = default;
    virtual AppId id() const = 0;
    virtual void onCreate(lv_obj_t* parent) = 0;
    virtual void onResume() {}
    virtual void onPause() {}
    virtual void onDestroy() = 0;
    virtual bool onKey(uint32_t key) { return false; }
    virtual void onTick(uint32_t nowMs) {}
};
