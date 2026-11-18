#include "apps/pet/pet_background.hpp"

#include <Arduino.h>
#include "core/psram.hpp"

namespace {

struct ThemePalette {
    uint32_t sky, cloud, grass, dashA, dashB;
};

// Màu lấy xấp xỉ từ hai ảnh mẫu (ngày/đêm); quyết định giao diện chính thức chốt sau.
// Chỉ số 0 = ban ngày, 1 = ban đêm.
const ThemePalette kPalettes[2] = {
    {0x8EF8FF, 0xDDFDFF, 0x00B432, 0xEEFF3F, 0x4DD60F},   // ngày
    {0x4B50AA, 0x7B9BA0, 0x134521, 0x8E9B2F, 0x2E8A12},   // đêm
};

constexpr uint32_t kMoonRgb = 0xFFFFFF;
constexpr uint32_t kStarRgb = 0xFFFFFF;

constexpr uint32_t kStepMs = 50;     // nhịp cập nhật tối thiểu (~20 Hz)
constexpr uint32_t kFadeMs = 1000;   // thời gian chuyển ngày/đêm

// Phần trăm của một cạnh màn hình.
constexpr int pct(int total, int percent) { return total * percent / 100; }

// Đường chân trời nằm ở 64% chiều cao (từ trên xuống). Vệt cỏ chỉ rải bên dưới đường này.
constexpr int kHorizonPercent = 64;

// Bộ sinh số giả ngẫu nhiên cố định hạt giống: nền không đổi giữa các lần vào app.
struct Rng {
    uint32_t s;
    uint32_t next(uint32_t n) {
        s = s * 1664525u + 1013904223u;
        return (s >> 16) % n;
    }
};

// Mây: vị trí và bề rộng theo % màn hình (đám trên cùng nhô khỏi mép trên), tốc độ trôi theo
// 1/10 px mỗi giây trên màn 160 px (màn rộng hơn tự nhân theo).
struct CloudCfg { int xPct, yPct, wPct, speedTenths; };
constexpr CloudCfg kCloudCfg[PetBackground::kClouds] = {
    {8, 8, 24, 20}, {65, 21, 26, 30}, {10, 38, 28, 15}, {45, -3, 30, 25},
};

struct StarPos {
    int  xPct;
    int  yPct;
    bool bright;   // sao sáng: thêm hai nét chéo (hình dấu hoa thị)
};

// Vị trí theo tỉ lệ ảnh mẫu.
constexpr StarPos kStarPos[PetBackground::kStars] = {
    {3, 7, false},  {36, 6, true},  {78, 18, false}, {22, 35, false}, {58, 40, false},
    {41, 44, false}, {96, 37, true}, {7, 55, false},  {78, 57, false},
};

constexpr int kStarArm  = UI_COMPACT ? 2 : 4;            // nửa độ dài nét sao
constexpr int kStarW    = UI_COMPACT ? 1 : 2;            // độ dày nét
constexpr int kStarSide = 2 * (kStarArm + 1) + 1;        // cạnh canvas một ngôi sao

// Canvas (có hoặc không có kênh trong suốt) với buffer PSRAM. Trả nullptr nếu hết bộ nhớ.
lv_obj_t* makeCanvas(lv_obj_t* parent, int w, int h, bool alpha, uint8_t** bufOut) {
    const size_t bytes = alpha ? LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(w, h)
                               : LV_CANVAS_BUF_SIZE_TRUE_COLOR(w, h);
    uint8_t* buf = static_cast<uint8_t*>(Psram::alloc(bytes));
    if (buf == nullptr) return nullptr;

    lv_obj_t* c = lv_canvas_create(parent);
    lv_canvas_set_buffer(c, buf, w, h, alpha ? LV_IMG_CF_TRUE_COLOR_ALPHA : LV_IMG_CF_TRUE_COLOR);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE);
    *bufOut = buf;
    return c;
}

// Nét ngắn đầu tròn.
void drawStroke(lv_obj_t* canvas, int x1, int y1, int x2, int y2, int width, uint32_t rgb) {
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = lv_color_hex(rgb);
    d.width = width;
    d.opa = LV_OPA_COVER;
    d.round_start = 1;
    d.round_end = 1;

    lv_point_t pts[2];
    pts[0].x = static_cast<lv_coord_t>(x1);
    pts[0].y = static_cast<lv_coord_t>(y1);
    pts[1].x = static_cast<lv_coord_t>(x2);
    pts[1].y = static_cast<lv_coord_t>(y2);
    lv_canvas_draw_line(canvas, pts, 2, &d);
}

// Chấm tròn (hình tròn đường kính `diameter`, góc trên trái tại (x, y)).
void drawDot(lv_obj_t* canvas, int x, int y, int diameter, uint32_t rgb) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(rgb);
    d.bg_opa = LV_OPA_COVER;
    d.radius = LV_RADIUS_CIRCLE;
    d.border_width = 0;
    lv_canvas_draw_rect(canvas, x, y, diameter, diameter, &d);
}

// Đám mây: một thân dẹt bo tròn + hai bướu tròn phía trên. (x0, y0) là góc trên trái,
// w là bề rộng (cao khoảng 0,6 w).
void drawCloud(lv_obj_t* canvas, uint32_t rgb, int x0, int y0, int w) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(rgb);
    d.bg_opa = LV_OPA_COVER;
    d.radius = LV_RADIUS_CIRCLE;
    d.border_width = 0;

    lv_canvas_draw_rect(canvas, x0,                 y0 + w * 30 / 100, w,           w * 30 / 100, &d);
    lv_canvas_draw_rect(canvas, x0 + w * 12 / 100,  y0 + w * 10 / 100, w * 36 / 100, w * 36 / 100, &d);
    lv_canvas_draw_rect(canvas, x0 + w * 38 / 100,  y0,                w * 46 / 100, w * 46 / 100, &d);
}

// Cánh đồng: một khối chữ nhật phủ từ đường chân trời (đường thẳng) xuống đáy màn hình.
void drawGrass(lv_obj_t* canvas, uint32_t rgb) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(rgb);
    d.bg_opa = LV_OPA_COVER;
    d.radius = 0;
    d.border_width = 0;

    const int y = pct(UI_H, kHorizonPercent);
    lv_canvas_draw_rect(canvas, 0, y, UI_W, UI_H - y, &d);
}

// Rải các nét ngắn hơi nghiêng trên cỏ.
void drawDashes(lv_obj_t* canvas, Rng& rng, int count, int len, int width, uint32_t rgb) {
    const int top  = pct(UI_H, kHorizonPercent + 4);   // dưới đường chân trời, không lòi lên trời
    const int span = UI_H - top - 2;
    for (int i = 0; i < count; ++i) {
        const int x  = static_cast<int>(rng.next(UI_W));
        const int y  = top + static_cast<int>(rng.next(span));
        const int dx = (static_cast<int>(rng.next(5)) - 2) * len / 5;
        const int dy = len * (70 + static_cast<int>(rng.next(61))) / 100;
        drawStroke(canvas, x, y, x + dx, y + dy, width, rgb);
    }
}

// Trăng lưỡi liềm: đĩa trắng, khoét bằng một đĩa màu trời lệch sang phải (mở về bên phải).
void drawMoon(lv_obj_t* canvas, uint32_t skyRgb) {
    const int r  = pct(UI_H, 8);              // bán kính trăng
    const int cx = pct(UI_W, 44);
    const int cy = pct(UI_H, 20);
    drawDot(canvas, cx - r, cy - r, 2 * r, kMoonRgb);

    const int r2 = r * 85 / 100;
    const int ox = cx + r * 45 / 100;
    const int oy = cy - r * 10 / 100;
    drawDot(canvas, ox - r2, oy - r2, 2 * r2, skyRgb);
}

// Cảnh tĩnh: trời, (trăng), cỏ, vệt cỏ. Hạt giống cố định nên ngày và đêm có vệt cỏ trùng nhau.
void drawScene(lv_obj_t* canvas, const ThemePalette& pal, bool night) {
    lv_canvas_fill_bg(canvas, lv_color_hex(pal.sky), LV_OPA_COVER);
    if (night) drawMoon(canvas, pal.sky);
    drawGrass(canvas, pal.grass);

    Rng rng = {20241105u};
    const int width    = UI_COMPACT ? 3 : 6;
    const int len      = UI_COMPACT ? 7 : 14;
    const int nGreen   = UI_COMPACT ? 6 : 14;
    const int nYellow  = UI_COMPACT ? 16 : 36;
    const int nDot     = UI_COMPACT ? 6 : 12;

    drawDashes(canvas, rng, nGreen,  len, width + 1, pal.dashB);
    drawDashes(canvas, rng, nYellow, len, width,     pal.dashA);

    const int dotTop  = pct(UI_H, kHorizonPercent + 4);
    const int dotSpan = UI_H - dotTop - 2;
    for (int i = 0; i < nDot; ++i) {
        const int x = static_cast<int>(rng.next(UI_W));
        const int y = dotTop + static_cast<int>(rng.next(dotSpan));
        drawDot(canvas, x, y, width + 1, pal.dashA);
    }
    lv_obj_invalidate(canvas);
}

// Một ngôi sao (dấu cộng, hoặc dấu hoa thị nếu sáng) vẽ giữa canvas nhỏ trong suốt.
void drawStar(lv_obj_t* canvas, bool bright) {
    lv_canvas_fill_bg(canvas, lv_color_hex(kStarRgb), LV_OPA_TRANSP);
    const int c = kStarSide / 2;
    const int a = bright ? kStarArm + 1 : kStarArm;
    drawStroke(canvas, c - a, c, c + a, c, kStarW, kStarRgb);
    drawStroke(canvas, c, c - a, c, c + a, kStarW, kStarRgb);
    if (bright) {
        const int d = a * 7 / 10;
        drawStroke(canvas, c - d, c - d, c + d, c + d, kStarW, kStarRgb);
        drawStroke(canvas, c - d, c + d, c + d, c - d, kStarW, kStarRgb);
    }
}

}  // namespace

PetBackground::~PetBackground() {
    // Không đụng LVGL ở đây: các object (nếu còn) đã bị hủy cùng screen của app.
    Psram::release(dayBuf_);
    Psram::release(nightBuf_);
    for (Cloud& c : clouds_) { Psram::release(c.buf); c.buf = nullptr; }
    for (Star&  s : stars_)  { Psram::release(s.buf); s.buf = nullptr; }
    dayBuf_ = nullptr;
    nightBuf_ = nullptr;
}

bool PetBackground::create(lv_obj_t* parent, bool night) {
    destroy();
    if (parent == nullptr) return false;
    night_  = night;
    target_ = night ? 255 : 0;
    fade_   = target_;
    started_ = false;

    day_ = makeCanvas(parent, UI_W, UI_H, false, &dayBuf_);
    if (day_ == nullptr) {
        Serial.println("[PetBackground] khong du PSRAM cho anh nen");
        return false;
    }
    lv_obj_set_pos(day_, 0, 0);
    drawScene(day_, kPalettes[0], false);

    nightC_ = makeCanvas(parent, UI_W, UI_H, false, &nightBuf_);
    if (nightC_ != nullptr) {
        lv_obj_set_pos(nightC_, 0, 0);
        drawScene(nightC_, kPalettes[1], true);
        lv_obj_set_style_opa(nightC_, static_cast<lv_opa_t>(fade_), 0);
    }

    // Sao: nằm trên nền, dưới mây. Độ sáng do updateStars() đặt.
    for (int i = 0; i < kStars; ++i) {
        Star& s = stars_[i];
        s.obj = makeCanvas(parent, kStarSide, kStarSide, true, &s.buf);
        if (s.obj == nullptr) continue;
        drawStar(s.obj, kStarPos[i].bright);
        lv_obj_set_pos(s.obj, pct(UI_W, kStarPos[i].xPct) - kStarSide / 2,
                              pct(UI_H, kStarPos[i].yPct) - kStarSide / 2);
        lv_obj_set_style_opa(s.obj, LV_OPA_TRANSP, 0);
    }
    updateStars(0);

    // Mây: vẽ màu ban ngày, ban đêm đổi màu bằng recolor.
    for (int i = 0; i < kClouds; ++i) {
        Cloud& c = clouds_[i];
        const CloudCfg& cfg = kCloudCfg[i];
        const int w = pct(UI_W, cfg.wPct);
        const int h = w * 60 / 100 + 1;
        c.obj = makeCanvas(parent, w, h, true, &c.buf);
        if (c.obj == nullptr) continue;
        c.w = w;
        c.y = pct(UI_H, cfg.yPct);
        c.x256 = static_cast<int32_t>(pct(UI_W, cfg.xPct)) * 256;

        lv_canvas_fill_bg(c.obj, lv_color_hex(kPalettes[0].cloud), LV_OPA_TRANSP);
        drawCloud(c.obj, kPalettes[0].cloud, 0, 0, w);
        lv_obj_set_style_img_recolor(c.obj, lv_color_hex(kPalettes[1].cloud), 0);
        lv_obj_set_style_img_recolor_opa(c.obj, static_cast<lv_opa_t>(fade_), 0);
        lv_obj_set_pos(c.obj, static_cast<lv_coord_t>(c.x256 >> 8), static_cast<lv_coord_t>(c.y));
    }
    return true;
}

void PetBackground::destroy() {
    // Xóa object trước, rồi mới giải phóng buffer mà chúng đang trỏ tới.
    for (Cloud& c : clouds_) {
        if (c.obj != nullptr) lv_obj_del(c.obj);
        c.obj = nullptr;
        Psram::release(c.buf);
        c.buf = nullptr;
    }
    for (Star& s : stars_) {
        if (s.obj != nullptr) lv_obj_del(s.obj);
        s.obj = nullptr;
        Psram::release(s.buf);
        s.buf = nullptr;
    }
    if (nightC_ != nullptr) lv_obj_del(nightC_);
    nightC_ = nullptr;
    Psram::release(nightBuf_);
    nightBuf_ = nullptr;
    if (day_ != nullptr) lv_obj_del(day_);
    day_ = nullptr;
    Psram::release(dayBuf_);
    dayBuf_ = nullptr;
}

void PetBackground::setNight(bool night) {
    night_  = night;
    target_ = night ? 255 : 0;   // tick() mờ dần tới mức này
}

void PetBackground::tick(uint32_t nowMs) {
    if (day_ == nullptr) return;
    if (!started_) {
        started_ = true;
        lastMs_ = nowMs;
        return;
    }
    uint32_t dt = nowMs - lastMs_;   // đúng cả khi millis() tràn số
    if (dt < kStepMs) return;
    lastMs_ = nowMs;
    if (dt > 250) dt = 250;          // vừa quay lại sau một khoảng dừng: không nhảy vọt

    // Chuyển ngày/đêm.
    bool fadeChanged = false;
    if (fade_ != target_) {
        int step = static_cast<int>(dt * 255u / kFadeMs);
        if (step < 1) step = 1;
        fade_ = (target_ > fade_) ? ((fade_ + step > target_) ? target_ : fade_ + step)
                                  : ((fade_ - step < target_) ? target_ : fade_ - step);
        fadeChanged = true;
        if (nightC_ != nullptr) lv_obj_set_style_opa(nightC_, static_cast<lv_opa_t>(fade_), 0);
    }

    // Mây trôi sang phải, ra khỏi mép phải thì quay lại từ mép trái.
    for (int i = 0; i < kClouds; ++i) {
        Cloud& c = clouds_[i];
        if (c.obj == nullptr) continue;
        // px/s = speedTenths/10 * (UI_W/160); đơn vị 1/256 px.
        c.x256 += static_cast<int32_t>(static_cast<uint32_t>(kCloudCfg[i].speedTenths) * UI_W * dt / 6250u);
        if (c.x256 >= static_cast<int32_t>(UI_W) * 256) c.x256 = -static_cast<int32_t>(c.w) * 256;
        lv_obj_set_pos(c.obj, static_cast<lv_coord_t>(c.x256 >> 8), static_cast<lv_coord_t>(c.y));
        if (fadeChanged) lv_obj_set_style_img_recolor_opa(c.obj, static_cast<lv_opa_t>(fade_), 0);
    }

    if (fade_ > 0 || fadeChanged) updateStars(nowMs);
}

// Nhấp nháy: mỗi sao có chu kỳ và pha riêng, độ sáng 35..100% rồi nhân với mức chuyển cảnh.
void PetBackground::updateStars(uint32_t nowMs) {
    for (int i = 0; i < kStars; ++i) {
        Star& s = stars_[i];
        if (s.obj == nullptr) continue;
        const uint32_t period = 1600u + static_cast<uint32_t>(i * 370) % 1400u;
        const uint32_t t = (nowMs + static_cast<uint32_t>(i) * 523u) % period;
        const uint32_t dist = (t * 2 > period) ? (t * 2 - period) : (period - t * 2);   // 0..period
        const uint32_t bright = 90u + 165u * dist / period;                              // 90..255
        const uint32_t opa = static_cast<uint32_t>(fade_) * bright / 255u;
        lv_obj_set_style_opa(s.obj, static_cast<lv_opa_t>(opa), 0);
    }
}
