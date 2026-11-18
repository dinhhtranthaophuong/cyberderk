#include "apps/pet/pet_app.hpp"

#include <Arduino.h>
#include "apps/pet/pet_art.hpp"
#include "config/display.hpp"
#include "core/app_manager.hpp"

// PET_DEBUG = 1: in chỉ số ra serial mỗi 3 giây và sau mỗi hành động (để chỉnh tốc độ giảm).
// Đặt 0 (hoặc -DPET_DEBUG=0) để tắt.
#ifndef PET_DEBUG
#define PET_DEBUG 0
#endif

namespace {

// ---------------------------------------------------------------------------
// Nút theo thứ tự L1, L2, L3 (trái, trên xuống), R1, R2, R3 (phải, trên xuống).
// `fallback` chỉ dùng khi hết PSRAM không vẽ được icon pixel.
// ---------------------------------------------------------------------------
struct PetButtonInfo {
    const char* fallback;
    const char* name;      // tên để in log
};

const PetButtonInfo kButtons[6] = {
    {"Eat",             "Eat"},
    {LV_SYMBOL_PLAY,    "Play"},
    {LV_SYMBOL_TINT,    "Bath"},
    {"Zz",              "Sleep"},
    {"",                "Skin"},
    {"",                "Spare"},
};

// Icon của 4 nút đầu (Eat, Play, Bath, Sleep).
const PetIcon kIconOfSlot[4] = {PetIcon::EAT, PetIcon::PLAY, PetIcon::BATH, PetIcon::SLEEP};

// Chỉ số tương ứng với 4 mặt nạ (Eat, Play, Bath, Sleep).
const PetStat kStatOfMask[4] = {PetStat::HUNGER, PetStat::FUN, PetStat::CLEAN, PetStat::ENERGY};

// Icon cá và cuộn len rộng 9 trên lưới 10 ô nên lệch phải nửa ô: dịch sang trái nửa ô
// (1 px trên màn 160x128, 2 px trên 2.8") để nằm đúng giữa nút. Hai icon còn lại đã cân.
// Muốn dịch nhiều hơn thì đổi hằng số này.
constexpr int kIconNudge = PetIcons::kScale / 2;
const int kIconNudgeX[4] = {-kIconNudge, -kIconNudge, 0, 0};

// Màu placeholder (giao diện chính thức chốt sau, ARCHITECTURE.md mục 11).
constexpr uint32_t kSkyRgb      = 0x8EF8FF;   // nền dự phòng nếu không vẽ được PetBackground
constexpr uint32_t kBtnRgb      = 0x2B3A42;   // nút xám xanh đậm: nổi trên cả trời lẫn cỏ
constexpr uint32_t kBtnPressRgb = 0x1B262C;
constexpr uint32_t kRimRgb      = 0xF5F5F5;   // viền nút bình thường
constexpr uint32_t kRimFocusRgb = 0xFF6A00;   // viền nút đang chọn

// Kích thước/bố cục suy ra từ cấu hình màn, không ghi cứng pixel.
constexpr int kBtn      = ICON_SIZE;                   // đường kính nút tròn
constexpr int kMarginX  = UI_W / 40;                   // lề trái/phải
constexpr int kGapY     = (UI_H - 3 * kBtn) / 4;       // khoảng cách dọc giữa các nút
constexpr int kBorder   = UI_COMPACT ? 2 : 3;
constexpr int kInner    = kBtn - 2 * kBorder;          // đường kính phần ruột của nút
constexpr int kSwatch   = kInner - (UI_COMPACT ? 4 : 6);   // ô màu skin nhỏ hơn ruột một chút
constexpr int kCatFeetY = UI_H * 86 / 100;             // đáy ảnh mèo (gồm bóng): chân đứng trên cỏ

// Style dùng chung, khởi tạo một lần và KHÔNG reset: object có thể còn tham chiếu
// style trong lúc AppManager hủy screen sau onDestroy().
lv_style_t s_btn;
lv_style_t s_btnFocused;
lv_style_t s_btnPressed;
bool s_stylesReady = false;

void initStyles() {
    if (s_stylesReady) return;
    s_stylesReady = true;

    lv_style_init(&s_btn);
    lv_style_set_radius(&s_btn, LV_RADIUS_CIRCLE);
    lv_style_set_bg_opa(&s_btn, LV_OPA_COVER);
    lv_style_set_bg_color(&s_btn, lv_color_hex(kBtnRgb));
    lv_style_set_border_width(&s_btn, kBorder);
    lv_style_set_border_color(&s_btn, lv_color_hex(kRimRgb));   // chỉ đổi màu viền khi focus nên không lệch kích thước
    lv_style_set_text_color(&s_btn, lv_color_white());

    lv_style_init(&s_btnFocused);
    lv_style_set_border_color(&s_btnFocused, lv_color_hex(kRimFocusRgb));

    lv_style_init(&s_btnPressed);
    lv_style_set_bg_color(&s_btnPressed, lv_color_hex(kBtnPressRgb));
}

// Nhớ nút đang chọn khi vào lại app (app bị hủy khi thoát nên cần biến tĩnh).
int s_lastFocus = 0;

#if PET_DEBUG
void logStats(const char* tag) {
    const PetState& st = PetState::instance();
    Serial.printf("[Pet] %s hunger=%u fun=%u clean=%u energy=%u %s skin=%u\n", tag,
                  static_cast<unsigned>(st.value(PetStat::HUNGER)),
                  static_cast<unsigned>(st.value(PetStat::FUN)),
                  static_cast<unsigned>(st.value(PetStat::CLEAN)),
                  static_cast<unsigned>(st.value(PetStat::ENERGY)),
                  st.sleeping() ? "NGU" : "THUC",
                  static_cast<unsigned>(st.skin()));
}
#endif

}  // namespace

// Factory (ARCHITECTURE.md mục 6.9): phạm vi toàn cục, không nằm trong namespace.
IApp* createPetApp() { return new PetApp(); }

// ---------------------------------------------------------------------------

void PetApp::onCreate(lv_obj_t* parent) {
    destroying_ = false;
    initStyles();

    // Tính bù thời gian đã trôi qua kể từ lần trước (chỉ số giảm cả khi app không mở).
    PetState& st = PetState::instance();
    st.update(millis());
    lastLogMs_ = millis();

    // Nền toàn màn, không cuộn, không vào group focus.
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(root_, lv_color_hex(kSkyRgb), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    // Nền cánh đồng + bầu trời, tạo trước để nằm dưới cùng. Nếu hết PSRAM thì
    // dùng nền trời đơn sắc của root_ (app vẫn chạy).
    background_.create(root_, st.sleeping());

    // Mèo đứng trên cỏ: chân ở khoảng 86% chiều cao, đầu nhô lên khỏi đường chân trời.
    // Nếu hết PSRAM thì app vẫn chạy (chỉ thiếu hình mèo).
    if (sprite_.create(root_, st.mood(), st.skin())) {
        lv_obj_set_pos(sprite_.obj(), (UI_W - PetSprite::width()) / 2,
                       kCatFeetY - PetSprite::height());
        sprite_.setNight(st.sleeping());
    }

    // Icon pixel của 4 nút. Nếu hết PSRAM thì dùng nhãn chữ dự phòng.
    icons_.create();

    // 6 nút. Tạo theo thứ tự L1 L2 L3 R1 R2 R3 để LV_KEY_PREV/NEXT
    // (LVGL tự dời focus theo thứ tự group) đi đúng thứ tự đó.
    for (int i = 0; i < kSlots; ++i) {
        const int col = i / kRows;
        const int row = i % kRows;

        lv_obj_t* b = lv_btn_create(root_);
        lv_obj_remove_style_all(b);
        lv_obj_add_style(b, &s_btn, 0);
        lv_obj_add_style(b, &s_btnFocused, LV_STATE_FOCUSED);
        lv_obj_add_style(b, &s_btnFocused, LV_STATE_FOCUS_KEY);
        lv_obj_add_style(b, &s_btnPressed, LV_STATE_PRESSED);
        lv_obj_set_size(b, kBtn, kBtn);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);

        const int x = (col == 0) ? kMarginX : (UI_W - kBtn - kMarginX);
        const int y = kGapY + row * (kBtn + kGapY);
        lv_obj_set_pos(b, x, y);

        // Icon (hoặc nhãn chữ dự phòng) ở giữa nút.
        if (i <= SLOT_SLEEP) {
            const lv_img_dsc_t* dsc = icons_.get(kIconOfSlot[i]);
            if (dsc != nullptr) {
                lv_obj_t* img = lv_img_create(b);
                lv_img_set_src(img, dsc);
                lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_align(img, LV_ALIGN_CENTER, kIconNudgeX[i], 0);
            } else if (kButtons[i].fallback[0] != '\0') {
                lv_obj_t* label = lv_label_create(b);
                lv_label_set_text(label, kButtons[i].fallback);
                lv_obj_set_style_text_color(label, lv_color_white(), 0);
                lv_obj_center(label);
            }
        }

        // Tạo SAU icon để mặt nạ nằm đè lên icon.
        if (i < kMasks) {
            masks_[i].create(b, kInner);
        } else if (i == SLOT_SKIN) {
            swatch_.create(b, kSwatch);
        }

        btns_[i] = b;
    }

    // Gắn callback sau khi đã tạo đủ 6 nút (indexOf cần mảng đầy đủ).
    for (int i = 0; i < kSlots; ++i) {
        lv_obj_add_event_cb(btns_[i], onButtonEvent, LV_EVENT_ALL, this);
    }

    applySkin();
    refresh();

    // Khôi phục nút đang chọn lần trước.
    if (s_lastFocus < 0 || s_lastFocus >= kSlots) s_lastFocus = 0;
    lv_group_focus_obj(btns_[s_lastFocus]);
}

void PetApp::onResume() {
    if (root_ == nullptr || destroying_) return;
    PetState::instance().update(millis());
    refresh();
}

void PetApp::onPause() {
}

void PetApp::onDestroy() {
    destroying_ = true;   // chặn callback ghi s_lastFocus khi group tự dời focus lúc xóa nút

    // Xóa mèo, nền, mặt nạ, ô màu (object + buffer PSRAM) trong lúc object còn sống,
    // rồi xóa toàn bộ cây object của app, cuối cùng mới giải phóng buffer icon.
    sprite_.destroy();
    background_.destroy();
    for (int i = 0; i < kMasks; ++i) masks_[i].destroy();
    swatch_.destroy();

    if (root_ != nullptr) {
        lv_obj_del(root_);   // xóa luôn 6 nút và các icon trong nút
    }
    root_ = nullptr;
    for (int i = 0; i < kSlots; ++i) btns_[i] = nullptr;

    icons_.destroy();
}

bool PetApp::onKey(uint32_t key) {
    (void)key;   // KEY_OPTION: menu phụ, làm ở bước sau
    return false;
}

void PetApp::onTick(uint32_t nowMs) {
    (void)nowMs;   // dùng millis() cho mọi tính toán để cùng một mốc thời gian với PetState
    if (root_ == nullptr || destroying_) return;

    const uint32_t now = millis();
    PetState::instance().update(now);
    refresh();
    sprite_.tick(now);
    background_.tick(now);

#if PET_DEBUG
    if (now - lastLogMs_ >= 3000) {
        lastLogMs_ = now;
        logStats("tick");
    }
#endif
}

// ---------------------------------------------------------------------------

// Mặt nạ và biểu cảm chỉ vẽ lại khi giá trị thực sự đổi (PetDisc/PetSprite tự kiểm tra).
void PetApp::refresh() {
    if (root_ == nullptr) return;
    const PetState& st = PetState::instance();
    for (int i = 0; i < kMasks; ++i) {
        masks_[i].showDepletion(st.value(kStatOfMask[i]));
    }
    // Trời tối khi mèo ngủ (vòng Sleep đã hết một vòng), sáng khi mèo dậy.
    background_.setNight(st.sleeping());
    sprite_.setNight(st.sleeping());
    sprite_.setMood(st.mood());
}

void PetApp::applySkin() {
    if (root_ == nullptr) return;
    const PetSkin skin = PetState::instance().skin();
    sprite_.setSkin(skin);

    uint32_t colors[3] = {0, 0, 0};
    int n = petSkinBands(skin);
    if (n > 3) n = 3;
    for (int b = 0; b < n; ++b) colors[b] = petSkinBandColor(skin, b);
    swatch_.showBands(colors, n);
}

int PetApp::indexOf(lv_obj_t* obj) const {
    if (obj == nullptr) return -1;
    for (int i = 0; i < kSlots; ++i) {
        if (btns_[i] == obj) return i;
    }
    return -1;
}

// Callback có thể chạy sau onDestroy() (LVGL gửi DEFOCUSED/DELETE khi dọn group
// và hủy screen) nhưng trước khi app bị delete: luôn kiểm tra trước khi dùng.
void PetApp::onButtonEvent(lv_event_t* e) {
    PetApp* self = static_cast<PetApp*>(lv_event_get_user_data(e));
    if (self == nullptr || self->destroying_ || self->root_ == nullptr) return;

    const int idx = self->indexOf(lv_event_get_target(e));
    if (idx < 0) return;

    switch (lv_event_get_code(e)) {
        case LV_EVENT_FOCUSED:
            s_lastFocus = idx;    // kể cả khi focus đổi bằng PREV/NEXT
            break;
        case LV_EVENT_KEY:
            self->handleKey(idx, lv_event_get_key(e));
            break;
        default:
            break;
    }
}

void PetApp::handleKey(int idx, uint32_t key) {
    switch (key) {
        case LV_KEY_UP:    moveFocus(idx, -1, 0); break;
        case LV_KEY_DOWN:  moveFocus(idx, +1, 0); break;
        case LV_KEY_LEFT:  moveFocus(idx, 0, -1); break;
        case LV_KEY_RIGHT: moveFocus(idx, 0, +1); break;
        case LV_KEY_ENTER: activate(idx);         break;
        case LV_KEY_ESC:   AppManager::goHome();  break;   // trì hoãn tới tick() kế tiếp, an toàn trong callback
        default: break;    // PREV/NEXT do LVGL tự dời focus
    }
}

// Lên/xuống đổi hàng trong cột, trái/phải đổi cột; quay vòng ở mép.
void PetApp::moveFocus(int from, int dRow, int dCol) {
    const int row = from % kRows;
    const int col = from / kRows;
    const int nRow = (row + dRow + kRows) % kRows;
    const int nCol = (col + dCol + kCols) % kCols;
    const int next = nCol * kRows + nRow;
    if (next != from && btns_[next] != nullptr) {
        lv_group_focus_obj(btns_[next]);
    }
}

void PetApp::activate(int idx) {
    if (idx < 0 || idx >= kSlots) return;

    PetState& st = PetState::instance();
    const uint32_t now = millis();
    st.update(now);   // chốt phần giảm theo thời gian trước khi áp dụng hành động

    switch (idx) {
        case SLOT_EAT:
        case SLOT_PLAY:
        case SLOT_BATH: {
            const bool done = (idx == SLOT_EAT)  ? st.eat(now)
                            : (idx == SLOT_PLAY) ? st.play(now)
                                                 : st.bath(now);
            if (!done) {   // mèo đang ngủ: không ăn/chơi/tắm được
                Serial.printf("[Pet] nut %s: meo dang ngu\n", kButtons[idx].name);
                return;
            }
            refresh();
            sprite_.react(idx == SLOT_EAT ? PetFx::EAT : idx == SLOT_PLAY ? PetFx::PLAY : PetFx::BATH, now);
            break;
        }
        case SLOT_SLEEP:
            if (!st.toggleSleep()) {   // đang thức mà năng lượng đã đầy
                Serial.println("[Pet] chua buon ngu");
                return;
            }
            refresh();
            break;
        case SLOT_SKIN:
            st.nextSkin();
            applySkin();
            break;
        default:
            Serial.printf("[Pet] nut %s: chua co tac dung\n", kButtons[idx].name);
            return;
    }

#if PET_DEBUG
    logStats(kButtons[idx].name);
#endif
}
