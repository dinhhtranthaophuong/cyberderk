# slideshow_test — test trên máy tính cho services/slideshow_service

Chạy: `cd tools/slideshow_test && sh run.sh` (thêm `tsan` để chỉ chạy ThreadSanitizer).
Cần g++ có ASan/UBSan/TSan (Linux/WSL). Dùng mã THẬT trong `../../services/` (nếu không có thì dùng `./src`).
Mã thật được test: slideshow_service.cpp, image_service.cpp, image_resize.cpp.
Giả lập (stubs/): Arduino, LVGL (đúng bố cục lv_img_dsc_t), FreeRTOS (task = thread, vTaskDelete(nullptr) tự thoát),
PSRAM (đếm khối, chặn free sai, chèn lỗi cấp phát), SD (thẻ ảo, chèn lỗi đọc/ghi, cổng chặn), TJpgDec (JPG giả xác định).

Kiểm thử đột biến: `python3 mutants.py` (chèn ~60 lỗi cố ý, mỗi lỗi test phải bắt được; mất ~15 phút).
Kết quả: 61/63 bị bắt, 1 sống sót đã bắt thêm bằng test mới (M05), 1 tương đương (M04: tăng gen khi đổi kích thước là dự phòng,
không có job cùng gen nào đang chạy ở trạng thái READY).

Ghi chú (không phải lỗi, để biết):
- T4: đọc /data/slideshow.txt lỗi lúc khởi động → NO_PHOTO cố định trong phiên (không đọc lại khi thẻ về).
- T3: ảnh toàn cảnh (tỉ lệ > ~2.65:1) bị phóng to khi phủ khung 154x232 vì hộp giải mã là hộp BAO 4x khung.
- T7: khung > 2047 px bị cắt bởi bitfield 11 bit của lv_img_header_t (khung thật không bị).
- T9c: request(kích thước khác) lúc đang LOADING bị bỏ qua; lần request sau mới sửa.
