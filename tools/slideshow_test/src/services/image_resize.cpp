#include "services/image_resize.hpp"

#include <cstddef>

namespace ImageResize {

Size fitInside(int srcW, int srcH, int maxW, int maxH) {
    if (srcW <= 0 || srcH <= 0 || maxW <= 0 || maxH <= 0) return {0, 0};
    if (srcW <= maxW && srcH <= maxH) return {srcW, srcH};

    // So sánh tỉ lệ bằng nhân chéo (int64 để không tràn với ảnh lớn).
    const int64_t lhs = static_cast<int64_t>(srcW) * maxH;
    const int64_t rhs = static_cast<int64_t>(srcH) * maxW;
    Size s;
    if (lhs > rhs) {  // ảnh "rộng" hơn vùng đích: giới hạn theo chiều rộng
        s.w = maxW;
        s.h = static_cast<int>(static_cast<int64_t>(srcH) * maxW / srcW);
    } else {
        s.h = maxH;
        s.w = static_cast<int>(static_cast<int64_t>(srcW) * maxH / srcH);
    }
    if (s.w < 1) s.w = 1;
    if (s.h < 1) s.h = 1;
    return s;
}

int chooseJpegScale(int srcW, int srcH, Size fit) {
    if (srcW <= 0 || srcH <= 0 || fit.w <= 0 || fit.h <= 0) return 1;
    static const int kScales[] = {8, 4, 2};
    for (int s : kScales) {
        if (srcW / s >= fit.w && srcH / s >= fit.h) return s;
    }
    return 1;
}

void boxDownscale(const uint16_t* src, int sw, int sh, uint16_t* dst, int dw, int dh) {
    if (!src || !dst || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    if (dw > sw) dw = sw;
    if (dh > sh) dh = sh;

    for (int dy = 0; dy < dh; ++dy) {
        const int y0 = static_cast<int>(static_cast<int64_t>(dy) * sh / dh);
        int y1 = static_cast<int>(static_cast<int64_t>(dy + 1) * sh / dh);
        if (y1 <= y0) y1 = y0 + 1;
        if (y1 > sh) y1 = sh;

        for (int dx = 0; dx < dw; ++dx) {
            const int x0 = static_cast<int>(static_cast<int64_t>(dx) * sw / dw);
            int x1 = static_cast<int>(static_cast<int64_t>(dx + 1) * sw / dw);
            if (x1 <= x0) x1 = x0 + 1;
            if (x1 > sw) x1 = sw;

            uint32_t r = 0, g = 0, b = 0;
            for (int y = y0; y < y1; ++y) {
                const uint16_t* row = src + static_cast<size_t>(y) * sw;
                for (int x = x0; x < x1; ++x) {
                    const uint16_t p = row[x];
                    r += (p >> 11) & 0x1F;
                    g += (p >> 5) & 0x3F;
                    b += p & 0x1F;
                }
            }
            const uint32_t n = static_cast<uint32_t>((y1 - y0) * (x1 - x0));
            // Làm tròn tới số nguyên gần nhất.
            const uint32_t rr = (r + n / 2) / n;
            const uint32_t gg = (g + n / 2) / n;
            const uint32_t bb = (b + n / 2) / n;
            dst[static_cast<size_t>(dy) * dw + dx] = static_cast<uint16_t>((rr << 11) | (gg << 5) | bb);
        }
    }
}

}  // namespace ImageResize
