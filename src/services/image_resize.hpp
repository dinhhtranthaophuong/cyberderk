#pragma once
// Phần tính toán thuần của ImageService (không phụ thuộc Arduino/LVGL/TJpg),
// tách riêng để test được trên máy tính.
#include <cstdint>

namespace ImageResize {

struct Size {
    int w;
    int h;
};

// Cỡ lớn nhất giữ tỉ lệ của (srcW x srcH) nằm trong (maxW x maxH). KHÔNG phóng to:
// ảnh nhỏ hơn vùng đích giữ nguyên. Mỗi cạnh tối thiểu 1. Đầu vào <= 0 trả {0, 0}.
Size fitInside(int srcW, int srcH, int maxW, int maxH);

// Bộ giải mã JPG chỉ thu nhỏ được 1/1, 1/2, 1/4, 1/8. Trả hệ số lớn nhất (8, 4, 2, 1)
// mà ảnh sau khi giải mã (src / hệ số) vẫn >= cỡ đích `fit` ở cả hai cạnh,
// nên phần thu nhỏ còn lại (nếu có) chỉ là thu nhỏ, không bao giờ phải phóng to.
int chooseJpegScale(int srcW, int srcH, Size fit);

// Thu nhỏ RGB565 bằng trung bình theo vùng (box filter). dw <= sw, dh <= sh.
// src và dst không được trùng nhau. Đầu vào sai thì không làm gì.
void boxDownscale(const uint16_t* src, int sw, int sh, uint16_t* dst, int dw, int dh);

}  // namespace ImageResize
