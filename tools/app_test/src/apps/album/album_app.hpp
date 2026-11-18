#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <string>
#include <vector>

#include "core/app_base.hpp"
#include "services/image_service.hpp"

// App Album: lưới thumbnail + xem ảnh toàn màn hình.
// - Thư mục ảnh: /images (JPG). Giải mã ở task Core 0 qua ImageService, KHÔNG chạm LVGL ở đó.
// - Kết quả giải mã đi về UI qua queue; onTick (Core 1, đang giữ LvglLock) mới tạo/sửa object LVGL.
class AlbumApp : public IApp {
 public:
    AlbumApp() = default;
    ~AlbumApp() override = default;

    AppId id() const override { return AppId::APP_ALBUM; }
    void onCreate(lv_obj_t* parent) override;
    void onDestroy() override;
    bool onKey(uint32_t key) override;   // KEY_OPTION: đặt ảnh đang chọn/đang xem làm ảnh widget Main Screen
    void onTick(uint32_t nowMs) override;

 private:
    enum class ThumbState : uint8_t { NONE, QUEUED, READY, FAILED };
    enum JobKind : uint8_t { JOB_THUMB = 0, JOB_FULL = 1 };

    struct Thumb {
        Image img;
        lv_img_dsc_t dsc;
        ThumbState state = ThumbState::NONE;
    };
    struct Cell {
        lv_obj_t* box = nullptr;
        lv_obj_t* img = nullptr;
        lv_obj_t* mark = nullptr;   // "..." khi đang tải, "x" khi lỗi
    };
    struct Job {
        uint8_t kind;
        uint32_t index;
        uint32_t token;
    };
    struct Result {
        uint8_t kind;
        uint32_t index;
        uint32_t token;
        bool ok;
        Image img;   // quyền sở hữu pixels chuyển cho bên nhận
    };

    // --- Giao diện (chỉ gọi khi đang giữ LvglLock) ---
    void buildGrid();
    void buildViewer();
    void applyCell(int slot);
    void refreshPage();
    void updateHeader();
    void setSelection(int index);
    void handleKey(uint32_t key);
    void handleGridKey(uint32_t key);
    void handleViewerKey(uint32_t key);
    void openViewer(int index);
    void showViewerIndex(int index);
    void closeViewer();
    void releaseFull();
    void applyResult(const Result& r);
    void setAsWidgetPhoto(int index);
    void showToast(const char* text, uint32_t nowMs);
    void hideToast();

    // --- Hàng đợi / task nền ---
    void pumpRequests();
    int nextWantedThumb() const;
    bool sendJob(const Job& job, bool toFront);
    void workerLoop();
    static void workerEntry(void* arg);
    static void keyEvent(lv_event_t* e);

    int pageStart() const;
    int count() const { return static_cast<int>(files_.size()); }

    // Trạng thái chung
    std::vector<std::string> files_;   // chỉ đọc sau onCreate, worker đọc song song an toàn
    std::vector<Thumb> thumbs_;        // kích thước cố định sau onCreate (địa chỉ phần tử ổn định)
    int sel_ = 0;
    bool viewerOpen_ = false;
    int viewIndex_ = 0;
    bool fullNeedsSend_ = false;
    int inFlight_ = 0;                 // số job thumbnail đang chờ kết quả
    int unrequested_ = 0;              // số thumbnail còn ở trạng thái NONE
    Image fullImg_;
    lv_img_dsc_t fullDsc_{};

    // LVGL
    lv_obj_t* root_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* countLbl_ = nullptr;
    lv_obj_t* emptyLbl_ = nullptr;
    std::vector<Cell> cells_;
    lv_obj_t* viewer_ = nullptr;
    lv_obj_t* viewImg_ = nullptr;
    lv_obj_t* viewMsg_ = nullptr;
    lv_obj_t* viewBar_ = nullptr;
    lv_obj_t* viewCaption_ = nullptr;
    lv_obj_t* viewHint_ = nullptr;     // gợi ý "3: dat nen" ở màn xem toàn màn hình
    lv_obj_t* toast_ = nullptr;        // thông báo ngắn, tự ẩn bằng millis() trong onTick
    uint32_t toastUntil_ = 0;
    bool toastActive_ = false;

    // Task nền
    TaskHandle_t task_ = nullptr;
    QueueHandle_t jobQ_ = nullptr;
    QueueHandle_t resQ_ = nullptr;
    SemaphoreHandle_t doneSem_ = nullptr;
    std::atomic<bool> stop_{false};
    std::atomic<uint32_t> fullToken_{0};
};
