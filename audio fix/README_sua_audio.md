# Sửa 3 lỗi nhỏ của audio_service

API không đổi (không sửa `audio_service.hpp`, không đụng ARCHITECTURE.md mục 6.7 ngoài phần ghi chú lỗi).

1. `begin()` rò tài nguyên khi I2S lỗi: giờ trả lại buffer PSRAM, gỡ driver I2S nếu `i2s_set_pin` lỗi (để gọi lại được), và dùng lại hai khóa đã tạo thay vì tạo mới mỗi lần gọi.
2. `seek()` tràn số: chặn mỗi lần gọi ở ±1 ngày (86400 s) và cộng dồn bão hòa trong int64, không còn tràn int32 với `seek(INT_MAX)` hoặc bấm liên tục.
3. `finished()` đọc-rồi-xóa cờ không nguyên tử: đọc/xóa/đặt cờ trong `portENTER_CRITICAL(&gFinMux)`.

Cách áp dụng: `audio_service.patch` là diff theo bản `audio_service.cpp` bạn đã tải lên chat này (unified diff). Nếu file trong repo đã khác bản đó thì áp tay từng đoạn theo patch, đừng ghi đè bằng `audio_service.cpp` đính kèm (nó dựng lại từ bản tải lên).
Test: thay `tools/audio_test/` bằng thư mục `audio_test/` kèm theo, rồi `sh run.sh` (thêm `TSAN=1` nếu muốn). Test mới: `begin_pin_fail`, `finished_race`; `begin_i2s_fail` giờ là kiểm tra bắt buộc (trước đây chỉ báo "phát hiện"); `run.sh` coi mọi lỗi UBSan là hỏng.
