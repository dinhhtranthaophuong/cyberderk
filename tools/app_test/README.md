# tools/app_test — test máy tính cho Main Screen, Album, Pet (+ AppManager)

Chạy MÃ THẬT (core/app_manager, app_registry, theme, input_service, apps/main_screen, apps/album, apps/pet,
services/image_service + slideshow_service) trên LVGL 8.3.11 THẬT, dưới ASan + UBSan (và TSan).
Giả lập: FreeRTOS (pthread), heap_caps, SD trong bộ nhớ, TJpgDec (định dạng JPEG giả), TimeService,
màn hình (kiểm tra vùng vẽ), icon, keypad. Đồng hồ `millis()` là đồng hồ ảo do test tiến.

## Chạy
    sh run.sh            # ASan+UBSan gnu++11/17/20 + TSan
    sh run.sh quick      # chỉ ASan+UBSan gnu++17
    sh run.sh leaks      # một nhóm: main_nav main_enter main_empty main_clock album_nav album_viewer album_option
                         #   album_empty album_bad album_exit_mid album_race pet_nav pet_actions themes manager
                         #   millis_wrap leaks psram_fail fuzz
    sh run.sh mutants    # chèn 30 lỗi cố ý, test phải bắt (~40 phút)
Cần g++, gcc, git. LVGL: tự tìm `.pio/libdeps/*/lvgl`, nếu không có thì clone v8.3.11 vào ./lvgl.
`SRCDIR=../../src sh run.sh` để test mã hiện tại của dự án thay vì bản sao trong ./src.

## Kết quả (bản sao trong ./src)
4600 kiểm tra, 0 lỗi ở ASan+UBSan gnu++11/17/20 và TSan. 300 vòng vào/ra 6 app: 0 B, 0 khối, 0 object, 0 task chênh.
Fuzz 20000 phím + đổi theme + rút/cắm SD + giải mã chậm: 0 B chênh. Chèn lỗi hết PSRAM ở từng vị trí: không crash, không rò.
Kiểm thử đột biến: 27/30 bị bắt; 3 còn lại là tương đương (M18 goTo(COUNT) đã bị AppManager chặn,
M20 ~PetSprite đã giải phóng buffer, M28 LVGL tự gỡ object bị xóa khỏi group).

## Giới hạn
- Header `time_service.hpp`, `sd_storage.hpp` là bản stub theo API đã biết; TimeService/SD thật không được test ở đây.
- LVGL hết bộ nhớ tự treo (LV_ASSERT_MALLOC) nên chỉ chèn lỗi vào Psram::alloc.
- Không bao phủ Music, Settings, Calendar, Visualizer.
- Ảnh JPEG là định dạng giả, không kiểm tra bộ giải mã thật.
