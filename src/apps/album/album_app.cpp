#include "apps/album/album_app.hpp"

#include <Arduino.h>

#include <cstring>

#include "config/display.hpp"
#include "config/keymap.hpp"
#include "core/app_manager.hpp"
#include "core/theme.hpp"
#include "services/input_service.hpp"
#include "services/sd_storage.hpp"
#include "services/slideshow_service.hpp"

namespace {

// ---- Bố cục: tính từ UI_W / UI_H, không ghi cứng pixel theo từng màn ----
constexpr int kCols = UI_COMPACT ? 3 : 4;
constexpr int kRows = UI_COMPACT ? 3 : 4;
constexpr int kPerPage = kCols * kRows;
constexpr int kHeaderH = UI_COMPACT ? 14 : 20;
constexpr int kPad = UI_COMPACT ? 2 : 4;
constexpr int kGap = UI_COMPACT ? 2 : 4;
constexpr int kBorder = 2;
constexpr int kRadius = UI_COMPACT ? 2 : 4;
constexpr int kCellW = (UI_W - 2 * kPad - (kCols - 1) * kGap) / kCols;
constexpr int kCellH = (UI_H - kHeaderH - 2 * kPad - (kRows - 1) * kGap) / kRows;
constexpr int kGridW = kCols * kCellW + (kCols - 1) * kGap;
constexpr int kGridX0 = (UI_W - kGridW) / 2;
constexpr int kGridY0 = kHeaderH + kPad;
constexpr int kThumbW = kCellW - 2 * kBorder;   // cỡ giải mã thumbnail (vừa lọt trong ô)
constexpr int kThumbH = kCellH - 2 * kBorder;
constexpr int kBarH = UI_COMPACT ? 14 : 20;     // thanh tên ảnh ở đáy màn xem toàn màn hình

constexpr int kMaxPhotos = 256;      // giới hạn bộ nhớ cache thumbnail (~7 KB mỗi ảnh trên 320x240)
constexpr int kMaxInFlight = 3;      // số thumbnail được yêu cầu cùng lúc
constexpr int kJobQueueLen = 8;
constexpr int kResultQueueLen = 16;
constexpr uint32_t kWorkerStack = 8192;
constexpr UBaseType_t kWorkerPrio = 1;
constexpr BaseType_t kWorkerCore = 0;
constexpr uint32_t kStopWaitMs = 3000;
constexpr uint32_t kToastMs = 1500;
constexpr int kToastPadV = UI_COMPACT ? 2 : 4;
constexpr int kToastPadH = UI_COMPACT ? 4 : 8;

void makeDsc(lv_img_dsc_t* d, const Image& im) {
    std::memset(d, 0, sizeof(*d));
    d->header.cf = LV_IMG_CF_TRUE_COLOR;
    d->header.always_zero = 0;
    d->header.w = im.w;
    d->header.h = im.h;
    d->data_size = static_cast<uint32_t>(im.w) * static_cast<uint32_t>(im.h) * (LV_COLOR_SIZE / 8);
    d->data = reinterpret_cast<const uint8_t*>(im.pixels);
}

inline void show(lv_obj_t* o) {
    if (o) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
}
inline void hide(lv_obj_t* o) {
    if (o) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace

// ============================== Vòng đời ==============================

void AlbumApp::onCreate(lv_obj_t* parent) {
    stop_.store(false);
    fullToken_.store(0);

    // Liệt kê vài chục file trên SD chỉ mất vài ms nên làm thẳng ở đây; việc nặng (giải mã) ở task nền.
    files_ = SdStorage::listFiles("/images", ".jpg;.jpeg");
    if (static_cast<int>(files_.size()) > kMaxPhotos) files_.resize(kMaxPhotos);
    thumbs_.resize(files_.size());
    unrequested_ = count();
    inFlight_ = 0;
    sel_ = 0;
    viewerOpen_ = false;
    viewIndex_ = 0;
    fullNeedsSend_ = false;

    const Theme& th = Themes::current();

    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_size(root_, UI_W, UI_H);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    // Một "ổ nhận phím" duy nhất: container không tự vào group, nên thêm tay (mục 4 của kiến trúc).
    lv_group_add_obj(InputService::defaultGroup(), root_);
    lv_group_focus_obj(root_);
    lv_obj_add_event_cb(root_, keyEvent, LV_EVENT_KEY, this);

    title_ = lv_label_create(root_);
    lv_label_set_text(title_, "Album");
    lv_obj_set_style_text_color(title_, lv_color_hex(th.text), 0);
    lv_obj_align(title_, LV_ALIGN_TOP_LEFT, kPad, UI_COMPACT ? 1 : 3);

    countLbl_ = lv_label_create(root_);
    lv_label_set_text(countLbl_, "");
    lv_obj_set_style_text_color(countLbl_, lv_color_hex(th.text), 0);
    lv_obj_align(countLbl_, LV_ALIGN_TOP_RIGHT, -kPad, UI_COMPACT ? 1 : 3);

    buildGrid();
    buildViewer();   // mặc định ẩn

    // Thông báo tạo sau cùng để nằm trên cả lưới lẫn màn xem ảnh; mặc định ẩn.
    toast_ = lv_label_create(root_);
    lv_label_set_text(toast_, "");
    lv_obj_set_style_bg_color(toast_, lv_color_hex(th.panel), 0);
    lv_obj_set_style_bg_opa(toast_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(toast_, lv_color_hex(th.textOnPanel), 0);
    lv_obj_set_style_radius(toast_, kRadius, 0);
    lv_obj_set_style_pad_hor(toast_, kToastPadH, 0);
    lv_obj_set_style_pad_ver(toast_, kToastPadV, 0);
    lv_obj_clear_flag(toast_, LV_OBJ_FLAG_CLICKABLE);
    hide(toast_);
    toastActive_ = false;
    toastUntil_ = 0;

    if (count() > 0) {
        jobQ_ = xQueueCreate(kJobQueueLen, sizeof(Job));
        resQ_ = xQueueCreate(kResultQueueLen, sizeof(Result));
        doneSem_ = xSemaphoreCreateBinary();
        if (jobQ_ && resQ_ && doneSem_) {
            if (xTaskCreatePinnedToCore(workerEntry, "album", kWorkerStack, this, kWorkerPrio, &task_,
                                        kWorkerCore) != pdPASS) {
                task_ = nullptr;
            }
        }
        if (!task_) Serial.println("[Album] khong tao duoc task giai ma");
    }
    refreshPage();
    Serial.printf("[Album] %d anh\n", count());
}

void AlbumApp::onDestroy() {
    // 1) Ngừng vẽ các ảnh sắp bị giải phóng (đề phòng; màn hình bị hủy ngay sau đó trong cùng lần chuyển app).
    for (Cell& c : cells_) hide(c.img);
    hide(viewImg_);

    // 2) Dừng task nền và chờ nó thoát (task không bao giờ lấy LvglLock nên không kẹt).
    bool workerGone = true;
    if (task_) {
        stop_.store(true);
        workerGone = (xSemaphoreTake(doneSem_, pdMS_TO_TICKS(kStopWaitMs)) == pdTRUE);
        task_ = nullptr;
        if (!workerGone) Serial.println("[Album] task giai ma khong thoat kip, giu lai bo nho de khong crash");
    }

    // 3) Giải phóng. Nếu task chưa thoát thì chấp nhận rò rỉ còn hơn để nó đọc bộ nhớ đã giải phóng.
    if (workerGone) {
        if (resQ_) {
            Result r;
            while (xQueueReceive(resQ_, &r, 0) == pdTRUE) ImageService::free(&r.img);
        }
        if (jobQ_) vQueueDelete(jobQ_);
        if (resQ_) vQueueDelete(resQ_);
        if (doneSem_) vSemaphoreDelete(doneSem_);
        jobQ_ = nullptr;
        resQ_ = nullptr;
        doneSem_ = nullptr;

        for (Thumb& t : thumbs_) ImageService::free(&t.img);
        ImageService::free(&fullImg_);
        thumbs_.clear();
        files_.clear();
    }

    // 4) Callback widget có thể còn chạy sau đây: đưa mọi con trỏ về nullptr để chúng tự bỏ qua.
    root_ = nullptr;
    title_ = nullptr;
    countLbl_ = nullptr;
    emptyLbl_ = nullptr;
    cells_.clear();
    viewer_ = nullptr;
    viewImg_ = nullptr;
    viewMsg_ = nullptr;
    viewBar_ = nullptr;
    viewCaption_ = nullptr;
    viewHint_ = nullptr;
    toast_ = nullptr;
    toastActive_ = false;
    toastUntil_ = 0;
    viewerOpen_ = false;
}

bool AlbumApp::onKey(uint32_t key) {
    if (key != KEY_OPTION || !root_ || count() == 0) return false;
    // Đang xem ảnh thì lấy ảnh đang xem, ở lưới thì lấy ô đang chọn.
    setAsWidgetPhoto(viewerOpen_ ? viewIndex_ : sel_);
    return true;
}

void AlbumApp::onTick(uint32_t nowMs) {
    if (!root_) return;

    // Tự ẩn thông báo bằng millis() (so sánh có dấu để chịu được tràn số).
    if (toastActive_ && static_cast<int32_t>(nowMs - toastUntil_) >= 0) hideToast();

    if (!resQ_) return;

    // Nhận kết quả từ task nền (tối đa vài cái mỗi lần để không làm khựng UI).
    for (int i = 0; i < 4; ++i) {
        Result r;
        if (xQueueReceive(resQ_, &r, 0) != pdTRUE) break;
        applyResult(r);
    }
    pumpRequests();
}

// ============================== Dựng giao diện ==============================

void AlbumApp::buildGrid() {
    const Theme& th = Themes::current();

    if (count() == 0) {
        emptyLbl_ = lv_label_create(root_);
        lv_label_set_long_mode(emptyLbl_, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(emptyLbl_, UI_W - 4 * kPad);
        lv_obj_set_style_text_align(emptyLbl_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(emptyLbl_, lv_color_hex(th.text), 0);
        lv_label_set_text(emptyLbl_, SdStorage::isMounted() ? "Chua co anh. Chep file JPG vao /images tren the SD"
                                                            : "Khong doc duoc the SD");
        lv_obj_center(emptyLbl_);
        return;
    }

    cells_.resize(kPerPage);
    for (int slot = 0; slot < kPerPage; ++slot) {
        Cell& c = cells_[slot];
        const int col = slot % kCols;
        const int row = slot / kCols;

        c.box = lv_obj_create(root_);
        lv_obj_remove_style_all(c.box);
        lv_obj_set_size(c.box, kCellW, kCellH);
        lv_obj_set_pos(c.box, kGridX0 + col * (kCellW + kGap), kGridY0 + row * (kCellH + kGap));
        lv_obj_clear_flag(c.box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(c.box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(c.box, lv_color_hex(th.panel), 0);
        lv_obj_set_style_bg_opa(c.box, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(c.box, kRadius, 0);
        lv_obj_set_style_border_width(c.box, kBorder, 0);
        lv_obj_set_style_border_color(c.box, lv_color_hex(th.panel), 0);

        c.img = lv_img_create(c.box);
        lv_obj_clear_flag(c.img, LV_OBJ_FLAG_CLICKABLE);
        hide(c.img);

        c.mark = lv_label_create(c.box);
        lv_label_set_text(c.mark, "...");
        lv_obj_set_style_text_color(c.mark, lv_color_hex(th.textOnPanel), 0);
        lv_obj_center(c.mark);
    }
}

void AlbumApp::buildViewer() {
    const Theme& th = Themes::current();

    viewer_ = lv_obj_create(root_);
    lv_obj_remove_style_all(viewer_);
    lv_obj_set_pos(viewer_, 0, 0);
    lv_obj_set_size(viewer_, UI_W, UI_H);
    lv_obj_clear_flag(viewer_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(viewer_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(viewer_, lv_color_hex(th.bg), 0);
    lv_obj_set_style_bg_opa(viewer_, LV_OPA_COVER, 0);

    viewImg_ = lv_img_create(viewer_);
    lv_obj_clear_flag(viewImg_, LV_OBJ_FLAG_CLICKABLE);
    hide(viewImg_);

    viewMsg_ = lv_label_create(viewer_);
    lv_label_set_text(viewMsg_, "Dang tai...");
    lv_obj_set_style_text_color(viewMsg_, lv_color_hex(th.text), 0);
    lv_obj_center(viewMsg_);

    viewBar_ = lv_obj_create(viewer_);
    lv_obj_remove_style_all(viewBar_);
    lv_obj_set_size(viewBar_, UI_W, kBarH);
    lv_obj_align(viewBar_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(viewBar_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(viewBar_, lv_color_hex(th.scrim), 0);
    lv_obj_set_style_bg_opa(viewBar_, th.scrimOpa, 0);

    viewCaption_ = lv_label_create(viewBar_);
    lv_label_set_long_mode(viewCaption_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(viewCaption_, UI_W - 2 * kPad);
    lv_obj_set_style_text_color(viewCaption_, lv_color_hex(th.text), 0);
    lv_label_set_text(viewCaption_, "");
    lv_obj_align(viewCaption_, LV_ALIGN_LEFT_MID, kPad, 0);

    // Gợi ý nhỏ, mờ ở góc trên phải, có nền scrim để vẫn đọc được trên ảnh sáng.
    viewHint_ = lv_label_create(viewer_);
    lv_label_set_text(viewHint_, "3: dat nen");
    lv_obj_set_style_text_color(viewHint_, lv_color_hex(th.text), 0);
    lv_obj_set_style_text_opa(viewHint_, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(viewHint_, lv_color_hex(th.scrim), 0);
    lv_obj_set_style_bg_opa(viewHint_, th.scrimOpa / 2, 0);
    lv_obj_set_style_radius(viewHint_, kRadius, 0);
    lv_obj_set_style_pad_hor(viewHint_, kToastPadH / 2, 0);
    lv_obj_set_style_pad_ver(viewHint_, 1, 0);
    lv_obj_clear_flag(viewHint_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(viewHint_, LV_ALIGN_TOP_RIGHT, -kPad, kPad);

    hide(viewer_);
}

int AlbumApp::pageStart() const { return (sel_ / kPerPage) * kPerPage; }

void AlbumApp::applyCell(int slot) {
    if (slot < 0 || slot >= static_cast<int>(cells_.size())) return;
    Cell& c = cells_[slot];
    if (!c.box) return;
    const Theme& th = Themes::current();

    const int idx = pageStart() + slot;
    if (idx >= count()) {
        hide(c.box);
        return;
    }
    show(c.box);
    lv_obj_set_style_border_color(c.box, lv_color_hex(idx == sel_ ? th.focus : th.panel), 0);

    Thumb& t = thumbs_[idx];
    if (t.state == ThumbState::READY) {
        lv_img_set_src(c.img, &t.dsc);
        lv_obj_align(c.img, LV_ALIGN_CENTER, 0, 0);
        show(c.img);
        hide(c.mark);
    } else {
        hide(c.img);
        lv_label_set_text(c.mark, t.state == ThumbState::FAILED ? "x" : "...");
        lv_obj_center(c.mark);
        show(c.mark);
    }
}

void AlbumApp::refreshPage() {
    for (int slot = 0; slot < static_cast<int>(cells_.size()); ++slot) applyCell(slot);
    updateHeader();
}

void AlbumApp::updateHeader() {
    if (!countLbl_) return;
    if (count() == 0) {
        lv_label_set_text(countLbl_, "");
        return;
    }
    lv_label_set_text_fmt(countLbl_, "%d/%d", sel_ + 1, count());
}

void AlbumApp::setSelection(int index) {
    const int n = count();
    if (n == 0) return;
    if (index < 0) index = 0;
    if (index >= n) index = n - 1;
    if (index == sel_) return;

    const int oldPage = pageStart();
    const int oldSlot = sel_ - oldPage;
    sel_ = index;
    if (pageStart() != oldPage) {
        refreshPage();
    } else {
        applyCell(oldSlot);
        applyCell(sel_ - oldPage);
        updateHeader();
    }
}

// ============================== Phím ==============================

void AlbumApp::keyEvent(lv_event_t* e) {
    // Có thể chạy sau onDestroy() nhưng trước khi app bị delete: kiểm tra con trỏ trước khi dùng.
    AlbumApp* self = static_cast<AlbumApp*>(lv_event_get_user_data(e));
    if (!self || !self->root_) return;
    if (lv_event_get_code(e) != LV_EVENT_KEY) return;
    self->handleKey(lv_event_get_key(e));
}

void AlbumApp::handleKey(uint32_t key) {
    if (viewerOpen_) {
        handleViewerKey(key);
    } else {
        handleGridKey(key);
    }
}

void AlbumApp::handleGridKey(uint32_t key) {
    const int n = count();
    if (key == LV_KEY_ESC) {
        AppManager::goHome();
        return;
    }
    if (n == 0) return;

    const int lastRowStart = ((n - 1) / kCols) * kCols;
    int next = sel_;
    switch (key) {
        case LV_KEY_LEFT:
            next = (sel_ + n - 1) % n;
            break;
        case LV_KEY_RIGHT:
            next = (sel_ + 1) % n;
            break;
        case LV_KEY_UP:
            if (sel_ < kCols) {   // hàng đầu: quay vòng xuống hàng cuối cùng, cùng cột nếu có
                next = lastRowStart + (sel_ % kCols);
                if (next >= n) next = n - 1;
            } else {
                next = sel_ - kCols;
            }
            break;
        case LV_KEY_DOWN:
            if (sel_ >= lastRowStart) {   // hàng cuối: quay vòng lên hàng đầu
                next = sel_ % kCols;
            } else {
                next = sel_ + kCols;
                if (next >= n) next = n - 1;
            }
            break;
        case LV_KEY_ENTER:
            openViewer(sel_);
            return;
        default:
            return;
    }
    setSelection(next);
}

void AlbumApp::handleViewerKey(uint32_t key) {
    const int n = count();
    switch (key) {
        case LV_KEY_ESC:
        case LV_KEY_ENTER:
            closeViewer();
            break;
        case LV_KEY_LEFT:
        case LV_KEY_UP:
            if (n > 1) showViewerIndex((viewIndex_ + n - 1) % n);
            break;
        case LV_KEY_RIGHT:
        case LV_KEY_DOWN:
            if (n > 1) showViewerIndex((viewIndex_ + 1) % n);
            break;
        default:
            break;
    }
}

// ============================== Xem toàn màn hình ==============================

void AlbumApp::releaseFull() {
    hide(viewImg_);   // ngừng tham chiếu pixels trước khi giải phóng
    ImageService::free(&fullImg_);
}

void AlbumApp::openViewer(int index) {
    if (!viewer_ || index < 0 || index >= count()) return;
    viewerOpen_ = true;
    show(viewer_);
    showViewerIndex(index);
}

void AlbumApp::showViewerIndex(int index) {
    if (index < 0 || index >= count()) return;
    viewIndex_ = index;
    releaseFull();

    lv_label_set_text(viewMsg_, "Dang tai...");
    lv_obj_center(viewMsg_);
    show(viewMsg_);

    const std::string& path = files_[index];
    const size_t slash = path.find_last_of('/');
    const char* name = path.c_str() + (slash == std::string::npos ? 0 : slash + 1);
    lv_label_set_text_fmt(viewCaption_, "%s  %d/%d", name, index + 1, count());

    fullToken_.fetch_add(1);   // mọi kết quả/ job cũ của ảnh khác thành "cũ", sẽ bị bỏ
    fullNeedsSend_ = true;
    pumpRequests();
}

void AlbumApp::closeViewer() {
    viewerOpen_ = false;
    fullNeedsSend_ = false;
    fullToken_.fetch_add(1);
    releaseFull();
    hide(viewer_);
    sel_ = viewIndex_;   // lưới đi theo ảnh vừa xem
    refreshPage();
}

// ============================== Đặt ảnh widget Main Screen ==============================

void AlbumApp::showToast(const char* text, uint32_t nowMs) {
    if (!toast_) return;
    lv_label_set_text(toast_, text);
    // Nằm phía trên thanh tên ảnh để không che chữ.
    lv_obj_align(toast_, LV_ALIGN_BOTTOM_MID, 0, -(kBarH + kPad));
    show(toast_);
    toastUntil_ = nowMs + kToastMs;
    toastActive_ = true;
}

void AlbumApp::hideToast() {
    hide(toast_);
    toastActive_ = false;
}

void AlbumApp::setAsWidgetPhoto(int index) {
    if (index < 0 || index >= count()) return;
    // Đường dẫn do SdStorage::listFiles trả về (bắt đầu bằng '/'); kiểm tra lại cho chắc.
    const std::string& path = files_[index];
    const bool ok = !path.empty() && path[0] == '/' && SlideshowService::setPhoto(path.c_str());
    showToast(ok ? "Da dat lam anh nen" : "Khong luu duoc", millis());
}

// ============================== Hàng đợi và task nền ==============================

bool AlbumApp::sendJob(const Job& job, bool toFront) {
    if (!jobQ_) return false;
    const BaseType_t r = toFront ? xQueueSendToFront(jobQ_, &job, 0) : xQueueSendToBack(jobQ_, &job, 0);
    return r == pdTRUE;
}

int AlbumApp::nextWantedThumb() const {
    const int n = count();
    const int p0 = pageStart();
    const int p1 = (p0 + kPerPage < n) ? p0 + kPerPage : n;
    for (int i = p0; i < p1; ++i) {
        if (thumbs_[i].state == ThumbState::NONE) return i;   // trang đang xem trước
    }
    for (int d = 0; d < n; ++d) {   // rồi lan dần ra hai phía
        const int fwd = p1 + d;
        const int bwd = p0 - 1 - d;
        if (fwd < n && thumbs_[fwd].state == ThumbState::NONE) return fwd;
        if (bwd >= 0 && thumbs_[bwd].state == ThumbState::NONE) return bwd;
        if (fwd >= n && bwd < 0) break;
    }
    return -1;
}

void AlbumApp::pumpRequests() {
    if (!jobQ_) return;

    if (viewerOpen_ && fullNeedsSend_) {
        Job j;
        j.kind = JOB_FULL;
        j.index = static_cast<uint32_t>(viewIndex_);
        j.token = fullToken_.load();
        if (sendJob(j, true)) fullNeedsSend_ = false;   // chen đầu hàng đợi để không chờ thumbnail
    }

    while (unrequested_ > 0 && inFlight_ < kMaxInFlight) {
        const int i = nextWantedThumb();
        if (i < 0) break;
        Job j;
        j.kind = JOB_THUMB;
        j.index = static_cast<uint32_t>(i);
        j.token = 0;
        if (!sendJob(j, false)) break;
        thumbs_[i].state = ThumbState::QUEUED;
        ++inFlight_;
        --unrequested_;
    }
}

void AlbumApp::applyResult(const Result& r) {
    if (r.kind == JOB_THUMB) {
        if (inFlight_ > 0) --inFlight_;
        const int idx = static_cast<int>(r.index);
        if (idx < 0 || idx >= count() || thumbs_[idx].state != ThumbState::QUEUED) {
            Image tmp = r.img;
            ImageService::free(&tmp);
            return;
        }
        Thumb& t = thumbs_[idx];
        if (r.ok && r.img.pixels) {
            t.img = r.img;
            makeDsc(&t.dsc, t.img);
            t.state = ThumbState::READY;
        } else {
            t.state = ThumbState::FAILED;
        }
        const int slot = idx - pageStart();
        if (slot >= 0 && slot < kPerPage) applyCell(slot);
        return;
    }

    // JOB_FULL
    Image tmp = r.img;
    if (!viewerOpen_ || !viewImg_ || r.token != fullToken_.load()) {
        ImageService::free(&tmp);   // ảnh cũ (đã đổi ảnh khác hoặc đã đóng)
        return;
    }
    if (r.ok && tmp.pixels) {
        releaseFull();
        fullImg_ = tmp;
        makeDsc(&fullDsc_, fullImg_);
        lv_img_set_src(viewImg_, &fullDsc_);
        lv_obj_align(viewImg_, LV_ALIGN_CENTER, 0, 0);
        show(viewImg_);
        hide(viewMsg_);
    } else {
        lv_label_set_text(viewMsg_, "Loi anh");
        lv_obj_center(viewMsg_);
        show(viewMsg_);
    }
}

void AlbumApp::workerEntry(void* arg) { static_cast<AlbumApp*>(arg)->workerLoop(); }

// Chạy ở Core 0. Tuyệt đối không gọi LVGL ở đây.
void AlbumApp::workerLoop() {
    while (!stop_.load()) {
        Job job;
        if (xQueueReceive(jobQ_, &job, pdMS_TO_TICKS(50)) != pdTRUE) continue;
        if (stop_.load()) break;
        if (job.kind == JOB_FULL && job.token != fullToken_.load()) continue;   // đã đổi ảnh, khỏi giải mã

        Result r;
        std::memset(&r, 0, sizeof(r));
        r.kind = job.kind;
        r.index = job.index;
        r.token = job.token;

        if (job.index < files_.size()) {
            const char* path = files_[job.index].c_str();
            if (job.kind == JOB_FULL) {
                r.ok = ImageService::decode(path, UI_W, UI_H, &r.img);
            } else {
                r.ok = ImageService::decode(path, kThumbW, kThumbH, &r.img);
            }
        }

        bool sent = false;
        while (!stop_.load()) {
            if (xQueueSend(resQ_, &r, pdMS_TO_TICKS(50)) == pdTRUE) {
                sent = true;
                break;
            }
        }
        if (!sent) ImageService::free(&r.img);
    }
    xSemaphoreGive(doneSem_);
    vTaskDelete(nullptr);
}

// ============================== Factory (mục 6.9) ==============================

IApp* createAlbumApp() { return new AlbumApp(); }