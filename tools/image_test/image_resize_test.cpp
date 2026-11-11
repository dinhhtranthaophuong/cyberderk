// Test trên máy tính cho services/image_resize (không cần board).
// Build và chạy từ thư mục gốc dự án:
//   g++ -std=c++17 -Wall -Wextra -fsanitize=address,undefined -Isrc tools/image_test/image_resize_test.cpp src/services/image_resize.cpp -o /tmp/image_resize_test
//   /tmp/image_resize_test
#include <cstdio>
#include <cstdint>
#include <vector>

#include "services/image_resize.hpp"

using namespace ImageResize;

static int g_fail = 0, g_total = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_total;                                                           \
        if (!(cond)) {                                                       \
            ++g_fail;                                                        \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                    \
    } while (0)

static uint16_t rgb(int r, int g, int b) { return (uint16_t)((r << 11) | (g << 5) | b); }

int main() {
    // fitInside
    CHECK(fitInside(320, 240, 320, 240).w == 320 && fitInside(320, 240, 320, 240).h == 240);
    CHECK(fitInside(100, 50, 320, 240).w == 100);          // không phóng to
    CHECK(fitInside(100, 50, 320, 240).h == 50);
    CHECK(fitInside(4000, 3000, 320, 240).w == 320);
    CHECK(fitInside(4000, 3000, 320, 240).h == 240);
    CHECK(fitInside(3000, 4000, 320, 240).h == 240);       // ảnh dọc: giới hạn theo chiều cao
    CHECK(fitInside(3000, 4000, 320, 240).w == 180);
    CHECK(fitInside(4000, 3000, 71, 46).w == 61);           // thumbnail: 4:3 vào ô 71x46
    CHECK(fitInside(4000, 3000, 71, 46).h == 46);
    CHECK(fitInside(10000, 10, 71, 46).h == 1);              // cạnh tối thiểu 1
    CHECK(fitInside(0, 10, 71, 46).w == 0);
    CHECK(fitInside(10, 10, 0, 46).h == 0);
    CHECK(fitInside(2000000, 1500000, 320, 240).w == 320);  // ảnh khổng lồ không tràn số

    // chooseJpegScale
    CHECK(chooseJpegScale(4000, 3000, {320, 240}) == 8);    // 500x375 >= 320x240
    CHECK(chooseJpegScale(1600, 1200, {320, 240}) == 4);    // 400x300 >= 320x240, 1/8 = 200x150 thiếu
    CHECK(chooseJpegScale(640, 480, {320, 240}) == 2);
    CHECK(chooseJpegScale(640, 480, {321, 240}) == 1);      // 1/2 không đủ chiều rộng
    CHECK(chooseJpegScale(320, 240, {320, 240}) == 1);
    CHECK(chooseJpegScale(200, 100, {200, 100}) == 1);
    CHECK(chooseJpegScale(0, 100, {10, 10}) == 1);
    {
        // Với mọi cỡ: hệ số chọn ra luôn cho ảnh >= cỡ đích (không bao giờ phải phóng to).
        bool ok = true;
        for (int w = 16; w <= 4096 && ok; w += 37)
            for (int h = 16; h <= 3072 && ok; h += 41) {
                Size fit = fitInside(w, h, 71, 46);
                int s = chooseJpegScale(w, h, fit);
                if (w / s < fit.w || h / s < fit.h) ok = false;
                if (s != 1 && s != 2 && s != 4 && s != 8) ok = false;
            }
        CHECK(ok);
    }

    // boxDownscale: màu đơn sắc giữ nguyên
    {
        std::vector<uint16_t> src(64 * 48, rgb(10, 20, 5)), dst(16 * 12, 0);
        boxDownscale(src.data(), 64, 48, dst.data(), 16, 12);
        bool ok = true;
        for (uint16_t p : dst) ok = ok && (p == rgb(10, 20, 5));
        CHECK(ok);
    }
    // trung bình trắng/đen (cột xen kẽ) ra xám giữa
    {
        std::vector<uint16_t> src(4 * 2), dst(2 * 1, 0);
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 4; ++x) src[y * 4 + x] = (x % 2) ? rgb(31, 63, 31) : 0;
        boxDownscale(src.data(), 4, 2, dst.data(), 2, 1);
        CHECK(dst[0] == dst[1]);
        CHECK(((dst[0] >> 11) & 0x1F) == 16);  // (0 + 31 + 0 + 31) / 4 làm tròn = 15.5 -> 16
        CHECK(((dst[0] >> 5) & 0x3F) == 32);   // 31.5 -> 32
    }
    // tỉ lệ lẻ (không chia hết): không ghi ngoài dst, không đọc ngoài src (ASan bắt)
    {
        std::vector<uint16_t> src(37 * 29, rgb(1, 2, 3)), dst(11 * 7 + 4, 0xABCD);
        boxDownscale(src.data(), 37, 29, dst.data(), 11, 7);
        bool ok = true;
        for (int i = 0; i < 11 * 7; ++i) ok = ok && dst[i] == rgb(1, 2, 3);
        CHECK(ok);
        for (int i = 11 * 7; i < 11 * 7 + 4; ++i) CHECK(dst[i] == 0xABCD);  // phần đệm còn nguyên
    }
    // đích bằng nguồn: bản sao nguyên
    {
        std::vector<uint16_t> src(5 * 3), dst(5 * 3, 0);
        for (size_t i = 0; i < src.size(); ++i) src[i] = (uint16_t)(i * 1237);
        boxDownscale(src.data(), 5, 3, dst.data(), 5, 3);
        CHECK(src == dst);
    }
    // đầu vào sai: không làm gì, không crash
    {
        uint16_t d[4] = {1, 2, 3, 4};
        boxDownscale(nullptr, 4, 4, d, 2, 2);
        boxDownscale(d, 0, 4, d + 1, 2, 2);
        CHECK(d[0] == 1 && d[3] == 4);
    }

    std::printf("%d/%d kiem tra dat\n", g_total - g_fail, g_total);
    return g_fail ? 1 : 0;
}
