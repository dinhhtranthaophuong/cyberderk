#include "services/image_service.hpp"

#include <Arduino.h>
#include <TJpg_Decoder.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstring>

#include "core/psram.hpp"
#include "services/image_resize.hpp"
#include "services/sd_storage.hpp"

// Ghi chú thiết kế
// - TJpg_Decoder dùng một đối tượng toàn cục (TJpgDec) và một callback không có tham số
//   người dùng, nên mọi lần giải mã đi qua một mutex và ngữ cảnh đích nằm ở biến tĩnh.
//   Nhờ vậy nhiều task (Album, Slideshow...) gọi ImageService cùng lúc vẫn an toàn.
// - File JPG được đọc cả vào PSRAM TRƯỚC khi lấy mutex giải mã (SdStorage tự khóa SD theo chunk),
//   nên khóa giải mã không bao giờ giữ lúc chờ thẻ SD.
// - Chỉ hỗ trợ JPG baseline (không progressive). Bỏ qua EXIF orientation.

namespace {

constexpr int kMaxSourceSide = 16384;   // từ chối ảnh vô lý (tránh tính toán tràn số)

struct DecodeCtx {
    uint16_t* dst = nullptr;
    int w = 0;
    int h = 0;
};
DecodeCtx g_ctx;   // chỉ đụng tới khi đang giữ mutex()

SemaphoreHandle_t mutex() {
    static SemaphoreHandle_t m = xSemaphoreCreateMutex();   // C++11: khởi tạo an toàn đa luồng
    return m;
}

class DecodeLock {
 public:
    DecodeLock() : ok_(xSemaphoreTake(mutex(), portMAX_DELAY) == pdTRUE) {}
    ~DecodeLock() {
        if (ok_) xSemaphoreGive(mutex());
    }
    bool ok() const { return ok_; }
    DecodeLock(const DecodeLock&) = delete;
    DecodeLock& operator=(const DecodeLock&) = delete;

 private:
    bool ok_;
};

// Callback của TJpg_Decoder: nhận từng khối (đã ở cỡ sau khi thu nhỏ), chép vào buffer đích.
bool onBlock(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (!g_ctx.dst || !bitmap) return false;
    if (x < 0 || y < 0 || x >= g_ctx.w) return true;   // ngoài vùng: bỏ qua nhưng tiếp tục
    int copyW = w;
    if (x + copyW > g_ctx.w) copyW = g_ctx.w - x;
    for (int row = 0; row < h; ++row) {
        const int yy = y + row;
        if (yy >= g_ctx.h) break;
        std::memcpy(g_ctx.dst + static_cast<size_t>(yy) * g_ctx.w + x,
                    bitmap + static_cast<size_t>(row) * w,
                    static_cast<size_t>(copyW) * sizeof(uint16_t));
    }
    return true;
}

// Giải mã `data` (JPG trong RAM) vào *out. Gọi khi đang giữ DecodeLock.
bool decodeBuffer(const uint8_t* data, size_t size, int targetW, int targetH, Image* out) {
    TJpgDec.setJpgScale(1);
    TJpgDec.setSwapBytes(false);   // RGB565 nguyên bản, khớp LV_COLOR_16_SWAP 0
    TJpgDec.setCallback(onBlock);

    uint16_t jw = 0, jh = 0;
    if (TJpgDec.getJpgSize(&jw, &jh, data, static_cast<uint32_t>(size)) != JDR_OK) return false;
    if (jw == 0 || jh == 0 || jw > kMaxSourceSide || jh > kMaxSourceSide) return false;

    // Cỡ cuối cùng cần có, rồi hệ số giải mã lớn nhất mà vẫn không phải phóng to sau đó.
    const ImageResize::Size fit = ImageResize::fitInside(jw, jh, targetW, targetH);
    const int scale = ImageResize::chooseJpegScale(jw, jh, fit);

    // Bộ giải mã cho ra khoảng jw/scale x jh/scale (làm tròn xuống); callback tự cắt phần dư.
    int dw = jw / scale;
    int dh = jh / scale;
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;

    uint16_t* decoded = static_cast<uint16_t*>(Psram::alloc(static_cast<size_t>(dw) * dh * sizeof(uint16_t)));
    if (!decoded) return false;
    std::memset(decoded, 0, static_cast<size_t>(dw) * dh * sizeof(uint16_t));

    g_ctx.dst = decoded;
    g_ctx.w = dw;
    g_ctx.h = dh;
    TJpgDec.setJpgScale(scale);
    const JRESULT r = TJpgDec.drawJpg(0, 0, data, static_cast<uint32_t>(size));
    g_ctx.dst = nullptr;
    TJpgDec.setJpgScale(1);
    if (r != JDR_OK) {
        Psram::release(decoded);
        return false;
    }

    // Còn lớn hơn cỡ đích (vì bộ giải mã chỉ có 1/2, 1/4, 1/8): thu nhỏ nốt bằng box filter.
    if (dw > fit.w || dh > fit.h) {
        const int ow = (fit.w < dw) ? fit.w : dw;
        const int oh = (fit.h < dh) ? fit.h : dh;
        uint16_t* scaled = static_cast<uint16_t*>(Psram::alloc(static_cast<size_t>(ow) * oh * sizeof(uint16_t)));
        if (!scaled) {
            Psram::release(decoded);
            return false;
        }
        ImageResize::boxDownscale(decoded, dw, dh, scaled, ow, oh);
        Psram::release(decoded);
        decoded = scaled;
        dw = ow;
        dh = oh;
    }

    out->pixels = decoded;
    out->w = dw;
    out->h = dh;
    return true;
}

}  // namespace

namespace ImageService {

bool decode(const char* path, int targetW, int targetH, Image* out) {
    if (!out) return false;
    *out = Image{};
    if (!path || targetW <= 0 || targetH <= 0) return false;

    size_t fileSize = 0;
    void* file = SdStorage::readFileToPsram(path, &fileSize);   // khóa SD chỉ giữ theo chunk
    if (!file) return false;

    bool ok = false;
    {
        DecodeLock lock;
        if (lock.ok()) ok = decodeBuffer(static_cast<const uint8_t*>(file), fileSize, targetW, targetH, out);
    }
    Psram::release(file);
    if (!ok) *out = Image{};
    return ok;
}

void free(Image* img) {
    if (!img) return;
    Psram::release(img->pixels);
    *img = Image{};
}

}  // namespace ImageService
