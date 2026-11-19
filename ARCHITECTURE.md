# ARCHITECTURE.md: Cyberdeck ESP32-S3

> Tài liệu "hợp đồng" chung cho mọi chat/app. Chat nào cũng phải tuân theo.
> Nếu cần đổi API hoặc header chung: **báo trước, sửa file này, rồi mới sửa code.**
> Đọc kèm `concept.md` và `instruction.md`.

**Trạng thái:** Chat 0 (Core + hạ tầng) được làm lại từ đầu theo từng bước, bước 1-5 đã chạy trên board: TFT + LVGL, phím qua serial, AppManager + launcher, bàn phím 3x3 thật, và bước 5 (màn ST7789 2.8", theme, bảng đăng ký app, Settings). Bước 6 (thẻ SD: mount đã chạy trên board, `services/sd_storage` đã viết, test trên máy tính và chạy đúng trên board). Còn các service/app khác (mục 8). Chat 3 (Image service + Album) xong và chạy đúng trên board. Chat 4 (Slideshow widget) xong về mặt code, đã build được. Chat 5 (Audio service + Music Player) xong và chạy đúng trên board: phát MP3 từ SD qua I2S, đĩa quay có ảnh, âm lượng, playlist, đã test trên board. Chat 6 (Visualizer) xong và chạy ổn trên board: FFT 256 điểm + VisualizerApp với 3 kiểu hiệu ứng (thanh phổ đối xứng ở giữa, waveform, thanh đối xứng hai viền), phím 7/9 đổi kiểu, 4/6 chỉnh dạng. Chat 2 (Time service) xong và chạy đúng trên board: DS3231 qua Wire thuần, giờ địa phương (múi giờ mặc định `ICT-7`), Main Screen dùng `TimeService::now()`, giữ giờ khi rút nguồn. `syncNtp` đã viết và test bằng SNTP giả trên máy tính nhưng chưa thử trên board. Chat 9 (Tối ưu + kiểm thử) xong: kiểm thử rò rỉ và độ ổn định trên board, test máy tính cho audio_service, slideshow_service, Main Screen/Album/Pet, đo thời gian chuyển app, bộ nhớ, stack và chạy dài 30 phút trên board. Số liệu ở mục 12.1.

---

## 1. Phần cứng đã chốt

| Hạng mục | Chọn |
|---|---|
| MCU | ESP32-S3 DevKitC-1 **N16R8** (16MB flash quad, 8MB PSRAM octal) |
| Màn hình | **Đang dùng: ST7789 2.8" 240x320** (320x240 khi xoay ngang), không cảm ứng. Dự phòng: ST7735S 1.8" (160x128). Cấu hình màu riêng của module này ở mục 3.1 |
| microSD | Module SPI + thẻ 8GB FAT32 (đã đấu và mount được, đọc khoảng 1,5 MB/s). Bus riêng, không chung với TFT |
| Audio | MAX98357A (I2S) + loa 4Ω 3W, mono (đã chạy trên board), chân SD của module phải nối 3V3 (để hở thì amp bị tắt, loa im); chân GAIN để hở (9 dB) |
| Input | 9 công tắc cơ ma trận 3x3, không cần diode (đã chạy) |
| RTC | DS3231 (I2C, địa chỉ 0x68, 100 kHz; module có kèm EEPROM AT24C32 ở 0x57, không dùng). Đã chạy trên board, giữ giờ khi rút nguồn nhờ pin. Đồng bộ NTP khi có WiFi: đã viết, chưa thử trên board |

### 1.1 Chân bị cấm trên N16R8
GPIO 26-37 (flash/PSRAM), 0/3/45/46 (strapping), 19/20 (USB), 43/44 (UART0).

### 1.2 Bảng gán chân (nguồn sự thật duy nhất: `config/pins.hpp`)

| Thiết bị | Tín hiệu | GPIO |
|---|---|---|
| TFT (FSPI/SPI2) | SCLK / MOSI / CS | 12 / 11 / 10 |
| | DC / RST / BL | 9 / 8 / 7 |
| microSD (HSPI/SPI3) | SCK / MOSI / MISO / CS | 15 / 16 / 17 / 18 |
| I2S MAX98357A | BCLK / LRC / DIN | 4 / 5 / 6 |
| Keypad hàng (output) | R1 / R2 / R3 | 38 / 39 / 40 |
| Keypad cột (input pull-up) | C1 / C2 / C3 | 41 / 42 / 21 |
| I2C (DS3231) | SDA / SCL | 1 / 2 |
| Dự phòng | | 47, 48 |

`config/pins.hpp` có `static_assert` so khớp các chân TFT với `build_flags`, đổi chân TFT phải đổi cả hai nơi. Chân đèn nền TFT (GPIO 7) do code điều khiển, không truyền cho TFT_eSPI. Màn ST7789 đang dùng nối chân LED vào 3V3 (đèn nền luôn sáng), GPIO 7 bỏ trống. Sơ đồ nối màn: `docs/st7789_wiring.svg`.

**I2S:** `Pins::I2sBclk/I2sLrc/I2sDin` = 4/5/6 nằm trong `config/pins.hpp` (đã có trong `static_assert` kiểm tra không trùng GPIO nào khác); code audio phải lấy chân từ đây, không hardcode. **Chân SD của module MAX98357A phải nối 3V3**: để hở thì amp bị tắt và loa im (không báo lỗi gì). Chân GAIN để hở (9 dB).

> **Lưu ý SPI host:** `sd_storage.cpp` dùng `SPIClass(HSPI)`. TFT_eSPI 2.5.43 trên ESP32-S3 mặc định dùng host FSPI (SPI2), nên SD ở HSPI (SPI3) không trùng. Nếu vẫn có lỗi khởi tạo, treo hoặc màn hình nhiễu khi bật SD, thêm `-DUSE_FSPI_PORT` hoặc `-DUSE_HSPI_PORT` vào `build_flags` cho đúng phiên bản TFT_eSPI/Arduino core đang dùng.

---

## 2. Cấu trúc thư mục

```
platformio.ini
docs/
  keypad_wiring.svg/.png   // sơ đồ nối bàn phím 3x3
  st7789_wiring.svg/.png   // sơ đồ nối màn ST7789
  sd_wiring.svg/.png       // sơ đồ nối module microSD (chỉ để xem)
tools/
  sd_test/sd_test.cpp      // sketch test thẻ SD riêng, build bằng env sd_test (không thuộc firmware chính)
  image_test/image_resize_test.cpp  // test máy tính cho image_resize (g++ + ASan, lệnh chạy ghi đầu file)
  i2s_test/i2s_test.cpp    // test bíp 440 Hz, env i2s_test
  fft_test/fft_test.cpp    // test máy tính cho fft (g++ + ASan + UBSan, lệnh chạy ghi đầu file)
  time_test/               // test máy tính cho time_service bằng STUB (stubs/: DS3231 giả, đồng hồ giả, WiFi/SNTP giả), g++ + ASan + UBSan (và TSan), lệnh chạy ghi đầu file time_test.cpp
  rtc_test/rtc_test.cpp    // sketch test DS3231 trên board (lệnh serial s/t/r/i/p/h), env rtc_test, dùng chính services/time_service.cpp
  rtc_test/sync_time.py    // đặt giờ board từ laptop qua serial (luôn gửi giờ UTC+7)
  visualizer_test/         // test máy tính cho VisualizerApp bằng STUB LVGL (stubs/), không phải LVGL thật
  leak_test/leak_test.cpp  // sketch kiểm thử rò rỉ + độ ổn định trên board (giai đoạn 0-7: vào/ra từng app, nhạc nền, rút/cắm SD, đổi ảnh widget), env leak_test (build_src_filter bỏ main.cpp, thêm file này; không sửa main.cpp)
  perf_test/perf_test.cpp  // sketch đo hiệu năng trên board: thời gian chuyển app, bộ nhớ mỗi app, stack từng task, chạy dài (soak) có nhạc; env perf_test (cùng cách dựng với leak_test, thêm -DPERF_TEST=1). Cờ tùy chọn: PERF_SOAK_MIN (mặc định 30), PERF_REPS (8), PERF_SKIP_VISITS, PERF_SKIP_SOAK, PERF_NO_TASKSTATE, PERF_TASK_NAMES. Chạy kèm `pio device monitor | tee perf.log` để không mất log
  audio_test/              // test máy tính cho audio_service bằng stub (I2S/FreeRTOS/SD giả), ASan + UBSan + TSan, kiểm tra đột biến; 4 cấu hình
  slideshow_test/          // test máy tính cho slideshow_service (stub, JPEG giả, SD giả trong RAM), ASan/UBSan/TSan, kiểm tra đột biến; 4 cấu hình
  app_test/                // test máy tính cho Main Screen/Album/Pet bằng LVGL 8.3.11 thật + stub phần cứng, ASan/UBSan/TSan, kiểm tra đột biến (30 mutant); chạy `sh run.sh quick`
src/
  main.cpp
  lv_conf.h             // cấu hình LVGL (heap trong PSRAM, tick = millis())
  config/
    pins.hpp            // bảng chân ở trên
    display.hpp         // SCREEN_W/H và hằng số suy ra
    keymap.hpp          // quy ước phím (mục 4)
  core/
    app_base.hpp        // interface IApp + AppId
    app_factories.hpp   // khai báo factory (weak) của 7 app (mục 6.9)
    app_manager.hpp/.cpp// FSM
    app_registry.hpp/.cpp // bảng đăng ký app: khóa, tên, icon, factory, chọn hiển thị (mục 6.10)
    theme.hpp/.cpp      // theme giao diện (mục 6.11)
    lvgl_port.hpp/.cpp  // TFT<->LVGL, task LVGL, LvglLock
    psram.hpp/.cpp      // wrapper cấp phát PSRAM
  assets/
    icons/ 
      icons.hpp/.cpp (Icons::get), icons_gen.hpp + icons_t0/t1/t2.cpp 
  services/
    input_service.hpp/.cpp
    keypad_matrix.hpp/.cpp       // quét ma trận 3x3
    settings_store.hpp/.cpp      // lưu cài đặt vào flash NVS (mục 6.12)
    sd_storage.hpp/.cpp   // thẻ SD: mount, liệt kê, đọc/ghi, Reader (mục 6.5)
    image_service.hpp/.cpp
    image_resize.hpp/.cpp   // tính toán thuần của ImageService (fitInside, chọn scale JPG, box filter), test được trên máy tính
    audio_service.hpp/.cpp
    time_service.hpp/.cpp
    slideshow_service.hpp/.cpp  // ảnh đại diện Main Screen: giải mã 1 ảnh ở Core 0, cache PSRAM (mục 6.13)
  apps/
    main_screen/  main_screen.hpp/.cpp (+ slideshow_widget.hpp/.cpp)
    settings/     settings_app.cpp (chọn theme, chọn app hiện trên Main Screen)
    album/        album_app.hpp/.cpp, disc_cover.hpp/.cpp (ảnh mặt đĩa)
    music/        music_app.hpp/.cpp
    visualizer/   visualizer_app.hpp/.cpp, fft.hpp/.cpp (FFT thuần, test được trên máy tính)
    calendar/     calendar_app.hpp/.cpp
    pet/          pet_app, pet_state, pet_types, pet_art (dữ liệu pixel thuần), pet_pixel, pet_sprite, pet_icons, pet_disc, pet_background
  lib/
    minimp3/
      minimp3.h
```

**Quy tắc vàng:**
1. App **chỉ gọi service**, không tự đụng SPI/SD/I2S/I2C.
2. Mỗi app chỉ viết trong `apps/<tên>/`. Không sửa `core/` và `services/` trừ khi chat đó được giao.
3. Mỗi cặp `.hpp` + `.cpp` riêng cho từng module. Không `delay()`; dùng `millis()` hoặc `vTaskDelay()`.
4. Include theo gốc `src/` (đã có `-Isrc`): `#include "core/psram.hpp"`, `#include "config/pins.hpp"`.
5. **Màu giao diện chỉ lấy từ `Themes::current()`** (mục 6.11), không ghi mã hex trực tiếp trong app. Nhờ vậy thêm theme không phải sửa app. Màu của hình ảnh cố định (lông mèo Pet, nền ngày/đêm của Pet) không thuộc theme.

---

## 3. Cấu hình màn hình

### 3.1 `platformio.ini`

Phiên bản được **ghim cố ý**: TFT_eSPI lỗi `tft.init()` trên ESP32-S3 với Arduino core mới (Bodmer/TFT_eSPI #3346). `espressif32@6.5.0` dùng Arduino core 2.0.14. Không nâng cấp khi chưa test lại `tft.init()`. PlatformIO không hỗ trợ comment cùng dòng với giá trị, nên mọi comment đặt ở dòng riêng.

```ini
[platformio]
default_envs = cyberdeck_st7789

[env]
platform = espressif32@6.5.0
board = esp32-s3-devkitc-1
framework = arduino
board_build.arduino.memory_type = qio_opi
board_upload.flash_size = 16MB
board_build.partitions = default_16MB.csv
monitor_speed = 115200
monitor_filters = esp32_exception_decoder
build_flags =
  -DBOARD_HAS_PSRAM
  -DARDUINO_USB_CDC_ON_BOOT=1
  -DUSER_SETUP_LOADED=1
  -DTFT_MOSI=11
  -DTFT_SCLK=12
  -DTFT_CS=10
  -DTFT_DC=9
  -DTFT_RST=8
  -DTFT_MISO=-1
  -DLOAD_GLCD=1
  -DLOAD_FONT2=1
  -DSMOOTH_FONT=0
  -DLV_CONF_INCLUDE_SIMPLE
  -DLV_LVGL_H_INCLUDE_SIMPLE
  -Isrc
lib_deps =
  bodmer/TFT_eSPI@2.5.43
  lvgl/lvgl@8.3.11
  bodmer/TJpg_Decoder@1.1.0

; Phần chung của màn ST7735S 1.8" (160x128 khi xoay ngang). Không phải env.
[tft_st7735]
build_flags =
  -DST7735_DRIVER
  -DTFT_WIDTH=128
  -DTFT_HEIGHT=160
  -DSPI_FREQUENCY=27000000
  -DSCREEN_W=160
  -DSCREEN_H=128

; Phần chung của màn ST7789 2.8" (240x320, xoay ngang = 320x240). Không phải env.
[tft_st7789]
build_flags =
  -DST7789_DRIVER
  -DTFT_WIDTH=240
  -DTFT_HEIGHT=320
  -DSPI_FREQUENCY=40000000
  -DSCREEN_W=320
  -DSCREEN_H=240

; Env mặc định: màn ST7789 2.8". Module này bị đổi chỗ đỏ/xanh dương nên cần TFT_BGR.
; Đảo màu (inversion) xử lý trong lvgl_port.cpp bằng tft.invertDisplay(false), không dùng cờ build.
[env:cyberdeck_st7789]
build_flags =
  ${env.build_flags}
  ${tft_st7789.build_flags}
  -DTFT_RGB_ORDER=TFT_BGR

; Env màn ST7735S 1.8" (dự phòng), thử BLACKTAB trước.
[env:cyberdeck_st7735]
build_flags =
  ${env.build_flags}
  ${tft_st7735.build_flags}
  -DST7735_BLACKTAB

; Env để thử tab màu khác nếu hình lệch/sai màu: tab_red, tab_green, tab_green2, tab_green3
; (cùng cấu trúc, chỉ đổi -DST7735_REDTAB / GREENTAB / GREENTAB2 / GREENTAB3).
```

**Cấu hình đã chốt của màn ST7789 đang dùng:** `-DTFT_RGB_ORDER=TFT_BGR` trong env và `tft.invertDisplay(false);` ngay sau `tft.init();` trong `core/lvgl_port.cpp` (hằng số `true`/`false` phụ thuộc module; ở module này là `false`). Cờ `-DTFT_INVERSION_ON` không dùng.

**Cách dò khi đổi module màn hình:** chèn tạm vào `lvgl_port.cpp` sau `tft.fillScreen(TFT_BLACK);` đoạn tô lần lượt đỏ, xanh lá, xanh dương, trắng, đen (mỗi màu 1 giây) rồi đối chiếu: trắng/đen đổi chỗ (và các màu thành màu bù) nghĩa là đảo sai chiều, đổi `invertDisplay(true/false)`; đỏ và xanh dương đổi chỗ nghĩa là thiếu/thừa `TFT_RGB_ORDER=TFT_BGR`; hình nhiễu, lệch, lật gương thì nghi sai driver (module 2.8" phổ biến cũng có thể là ILI9341: đổi `ST7789_DRIVER` thành `ILI9341_DRIVER`). Hình nhiễu do dây dài: hạ `SPI_FREQUENCY` xuống 27000000. Hình bị ngược đầu: đổi `setRotation(1)` thành `3`. Nếu module không có chân CS thì `-DTFT_CS=-1`.

### 3.2 `config/display.hpp`

```cpp
#pragma once
// SCREEN_W / SCREEN_H đến từ build_flags
constexpr int UI_W = SCREEN_W;
constexpr int UI_H = SCREEN_H;
constexpr bool UI_COMPACT = (SCREEN_W < 240);        // true với 160x128
constexpr int  GRID_COLS = 2;
constexpr int  GRID_ROWS = 3;                        // lưới app 2 cột x 3 hàng (theo mockup)
constexpr int  ICON_SIZE = UI_COMPACT ? 28 : 56;
constexpr int  LVGL_BUF_LINES = UI_H / 10;           // buffer 1/10 màn hình
```

**Quy ước UI:** không ghi cứng pixel; dùng `lv_pct()`, flex/grid của LVGL và các hằng số ở trên. Ảnh/asset resize sẵn đúng độ phân giải trên PC. Font mặc định Montserrat 12 (nhỏ nhưng đã được chọn giữ trên màn 320x240); chỗ cần chữ lớn hơn (tiêu đề, đồng hồ) thì thêm cỡ 16/20 riêng ở chỗ đó.

### 3.3 `lv_conf.h`
Đặt ở `src/lv_conf.h`. Điểm chính: `LV_COLOR_DEPTH 16`, `LV_COLOR_16_SWAP 0` (đảo byte làm ở `flush_cb` bằng `pushColors(..., true)`), heap LVGL trong PSRAM (`LV_MEM_CUSTOM` + `heap_caps_malloc(MALLOC_CAP_SPIRAM)`), tick = `millis()`, chu kỳ đọc input 10 ms, font Montserrat 12 (mặc định) và 14, thêm cỡ khác khi cần. Các tuỳ chọn không khai báo lấy mặc định của `lv_conf_internal.h`.

---

## 4. Quy ước phím 3x3

```
 ┌───┬───┬───┐
 │ 1 │ 2 │ 3 │      1 = BACK    2 = UP     3 = OPTION
 ├───┼───┼───┤      4 = LEFT    5 = ENTER  6 = RIGHT
 │ 4 │ 5 │ 6 │      7 = PREV    8 = DOWN   9 = NEXT
 ├───┼───┼───┤
 │ 7 │ 8 │ 9 │      Giữ phím 1 (>= 800 ms) = HOME (về MAIN_SCREEN)
 └───┴───┴───┘
```

| Phím | Mã LVGL / custom | Ý nghĩa chung |
|---|---|---|
| 2 / 8 / 4 / 6 | `LV_KEY_UP/DOWN/LEFT/RIGHT` | Điều hướng |
| 5 | `LV_KEY_ENTER` | Chọn / xác nhận |
| 1 | `LV_KEY_ESC` | Quay lại (thoát app khi ở màn gốc của app) |
| 7 / 9 | `LV_KEY_PREV` / `LV_KEY_NEXT` | Phím phụ (trang trước/sau, đổi chế độ); LVGL tự dời focus tuần tự |
| 3 | `KEY_OPTION` (= `0x2001`) | Menu phụ của app (tùy app) |
| Giữ 1 | `KEY_HOME` (= `0x2002`) | Về Main Screen từ bất kỳ app nào |

`keymap.hpp` định nghĩa `KEY_OPTION`, `KEY_HOME`, `KEY_HOME_HOLD_MS` và `keyForNumber()`. `InputService` đẩy phím vào `lv_indev` (loại `LV_INDEV_TYPE_KEYPAD`) trong `read_cb` (chu kỳ 10 ms). Hai phím cùng lúc: bỏ qua, nên không cần diode.

**Bàn phím thật (`services/keypad_matrix`):** hàng GPIO 38/39/40 là output, cột GPIO 41/42/21 là `INPUT_PULLUP`, đấu theo `docs/keypad_wiring.svg`. Lúc rảnh mọi hàng kéo LOW và ngắt FALLING trên 3 cột chỉ bật một cờ, `poll()` chỉ đọc cờ đó (không đụng GPIO). Khi có phím: huỷ ngắt, quét mỗi 5 ms với hàng không quét ở high-Z (không bao giờ nối tắt hai hàng), chống dội 2 lần quét giống nhau, thả hết thì quay lại trạng thái ranh. Phím giữ lúc khởi động và phím còn lại sau khi nhấn nhiều phím đều bị bỏ qua tới khi thả hết.

**Nguồn phím:** bàn phím ma trận thật và Serial Monitor chạy song song, cùng đi qua `route()` trong `input_service.cpp`. Gõ `1`..`9` tạo đúng phím tương ứng, gõ `h` giả lập giữ phím 1 (HOME). Build với `-DKEYPAD_DEBUG=1` để in mã phím của bàn phím thật ra serial.

**Hành vi hai phím đặc biệt:**
- **Phím 1:** nhấn ngắn → gửi một xung `LV_KEY_ESC` khi **nhả** phím; giữ >= 800 ms → gửi `KEY_HOME` một lần và **không** gửi ESC.
- **Phím 3:** không đi vào `lv_indev`; gửi `KEY_OPTION` một lần mỗi lần nhấn, qua `AppManager::postKey()`.

**Trong app:**
- **Phím mũi tên không tự dời focus trong LVGL 8.3** (chỉ `LV_KEY_NEXT`/`LV_KEY_PREV` tự dời). Chúng chỉ đến widget dưới dạng `LV_EVENT_KEY`, nên mỗi app tự xử lý. Mẫu điều hướng lưới quay vòng ở mép: `moveFocus()` trong `apps/main_screen/main_screen.cpp`. Mẫu danh sách dọc: `lv_group_focus_prev/next(InputService::defaultGroup())` trong `apps/settings/settings_app.cpp`.
- `LV_KEY_ESC` cũng đi vào LVGL: app tự xử lý `LV_EVENT_KEY` với `LV_KEY_ESC` (thoát về màn gốc của app hoặc `AppManager::goHome()`). Màn thay thế do AppManager dựng cho app chưa viết tự về Main khi nhận ESC.
- `KEY_OPTION` tới `IApp::onKey()`. `KEY_HOME` do AppManager xử lý trước, app không nhận.

---

## 5. Mô hình đa luồng

| Core | Chạy gì |
|---|---|
| **Core 1** | Task LVGL (`lv_timer_handler` mỗi ~5 ms, stack 12 KB, ưu tiên 2), InputService (trong read_cb), `loop()` gọi `AppManager::tick` |
| **Core 0** | Task SD/ảnh, Task audio (decode + I2S), task NTP/nền |

**Quy tắc:**
- **LVGL không thread-safe.** Task ở Core 0 **không gọi LVGL trực tiếp**. Hoặc đẩy dữ liệu qua queue để task UI xử lý, hoặc bọc bằng `LvglLock` (RAII mutex đệ quy, `core/lvgl_port.hpp`) khi thật sự cần.
- `loop()` (task Arduino) và task LVGL cùng chạy trên Core 1 nhưng là hai task khác nhau, nên `AppManager::tick()` tự lấy `LvglLock` bên trong. Hàm `onCreate/onTick/onKey` của app luôn được gọi khi **đang giữ** `LvglLock`. Event callback của widget chạy trong task LVGL, cũng đang giữ khóa.
- Task LVGL chạy ngay sau `LvglPort::init()`, nên trong `setup()` mọi thao tác LVGL (`InputService::init`, `AppManager::init`) phải nằm trong `LvglLock`.
- TFT và SD ở **hai bus SPI khác nhau** nên không cần mutex giữa hai thiết bị này.
- Bus I2C (DS3231) chỉ do `TimeService` đụng (quy tắc vàng số 1). `TimeService` có mutex đệ quy riêng nên `now()`/`set()` gọi được từ mọi task; riêng `syncNtp()` chặn tới 8 giây nên chỉ gọi từ task nền Core 0.
- Nhưng SD bị **nhiều task dùng chung** (audio đọc nhạc, ImageService đọc ảnh) nên `SdStorage` có mutex nội bộ (đệ quy). Giữ khóa ngắn: đọc theo chunk 4-8 KB hoặc đọc cả file vào PSRAM rồi nhả khóa **trước khi** giải mã.
- Audio dùng ring buffer >= 64 KB trong PSRAM để không bị đói dữ liệu khi SD bận ảnh.
- Mọi buffer lớn (ảnh, audio, framebuffer, draw buffer LVGL) cấp bằng `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` qua `psram.hpp`. Ai cấp phát người đó giải phóng, hoặc ghi rõ trong comment API.

---

## 6. Các header "hợp đồng"

### 6.1 `core/psram.hpp`

```cpp
#pragma once
#include <cstddef>
namespace Psram {
void* alloc(size_t bytes);            // trả nullptr nếu hết bộ nhớ
void  release(void* p);               // an toàn với nullptr
size_t freeBytes();
}
```

### 6.2 `core/app_base.hpp`: interface app

```cpp
#pragma once
#include <lvgl.h>
#include <cstdint>

enum class AppId : uint8_t {
    MAIN_SCREEN, APP_PET, APP_MUSIC_PLAYER,
    APP_VISUALIZER, APP_CALENDAR, APP_ALBUM,
    APP_SETTINGS, COUNT
};

class IApp {
public:
    virtual ~IApp() = default;
    virtual AppId id() const = 0;

    // Tạo toàn bộ object LVGL dưới `parent` + cấp phát tài nguyên.
    virtual void onCreate(lv_obj_t* parent) = 0;
    // Được hiển thị/được nhận focus (bắt đầu timer, task nền).
    virtual void onResume() {}
    // Mất focus (dừng timer; KHÔNG giải phóng trừ khi onDestroy).
    virtual void onPause() {}
    // Dọn sạch object LVGL + bộ nhớ + task của app. Không được rò rỉ.
    virtual void onDestroy() = 0;
    // Phím custom (KEY_OPTION...). Trả true nếu đã xử lý.
    virtual bool onKey(uint32_t key) { return false; }
    // Gọi định kỳ trên Core 1 (non-blocking), tùy chọn.
    virtual void onTick(uint32_t nowMs) {}
};
```

### 6.3 `core/app_manager.hpp`: FSM

```cpp
#pragma once
#include <cstdint>
#include "app_base.hpp"

namespace AppManager {
void init();                       // vào MAIN_SCREEN (app tạo qua bảng đăng ký, mục 6.10)
void goTo(AppId id);               // TRÌ HOÃN: chuyển thật sự ở lần tick() kế tiếp
void goHome();                     // = goTo(MAIN_SCREEN)
void reload();                     // TRÌ HOÃN: dựng lại màn hình của app hiện tại (vd sau khi đổi theme)
AppId current();
void tick(uint32_t nowMs);         // gọi từ loop Core 1; tự lấy LvglLock

// Đẩy phím custom (KEY_HOME, KEY_OPTION) vào hàng đợi. Thread-safe, không chặn.
// InputService gọi hàm này; app KHÔNG cần gọi.
void postKey(uint32_t key);
}
```

Quy tắc:
- Chỉ **một app** tồn tại tại một thời điểm (tiết kiệm RAM). Thứ tự khi chuyển: `onPause` + `onDestroy` app cũ (**chưa delete**) → dọn group focus mặc định → tạo screen mới (`Themes::applyToScreen`) → `onCreate` app mới → `lv_scr_load` → `onResume` → hủy screen cũ → `delete` app cũ.
- **Event callback của widget có thể chạy sau `onDestroy()`** (LVGL gửi DEFOCUSED/DELETE khi dọn group và hủy screen) nhưng trước khi app bị delete. Callback phải chịu được việc này: `onDestroy()` nên đặt con trỏ widget về `nullptr` và callback kiểm tra trước khi dùng (mẫu: `indexOf()` trong `main_screen.cpp`, `settings_app.cpp`).
- Screen do AppManager tạo lấy **nền, màu chữ mặc định và tắt scroll từ theme đang chọn** (`Themes::applyToScreen`, widget con kế thừa màu chữ). Theme mặc định của LVGL là theme sáng với chữ tối, nên nếu thiếu việc đặt màu chữ thì label trên nền tối gần như vô hình (lỗi "màn hình đen" đã gặp ở bước 3). App muốn màu chữ khác thì tự đặt cho từng widget, lấy từ theme.
- `goTo()`/`goHome()` **không chuyển ngay** mà đặt cờ; việc chuyển thực hiện trong `tick()`. Nhờ vậy gọi từ event callback LVGL là an toàn (không xóa object đang xử lý event). Gọi `goTo` tới chính app đang chạy thì bỏ qua.
- `reload()` cũng trì hoãn như `goTo()`: app hiện tại được dựng lại từ đầu với theme hiện tại. Trạng thái cần giữ qua lần dựng lại (vị trí focus, hàng đang chọn) thì app tự giữ ở biến tĩnh (mẫu: `lastFocusIdx` trong `main_screen.cpp`, `lastRow` trong `settings_app.cpp`).
- `KEY_HOME` do AppManager xử lý (về Main Screen, bỏ qua nếu đang ở Main). `KEY_OPTION` chuyển tới `IApp::onKey()` của app hiện tại.
- Mỗi lần chuyển, AppManager in một dòng `[AppManager] -> <tên> (N ms) | heap .. KB, psram .. KB` ra serial, dùng để theo dõi rò rỉ bộ nhớ. `N ms` là thời gian dựng/hủy trong `tick()` (số đo thật sự của việc chuyển app, xem mục 12.1).
- `tick()` chặn `loop()` suốt lần chuyển app (đo được tới khoảng 340 ms khi vào Pet hoặc thoát Album). Không gọi việc nặng khác trong `loop()` cùng lúc.
- Riêng `AudioService` chạy nền độc lập với app, nên thoát Music Player nhạc vẫn phát (hoặc dừng, quyết định ở chat Music).

### 6.4 `services/input_service.hpp`

```cpp
#pragma once
#include <lvgl.h>
namespace InputService {
void init();                       // tạo lv_indev + group mặc định; gọi khi đang giữ LvglLock
lv_indev_t* indev();
lv_group_t* defaultGroup();        // app thêm widget cần focus vào group này
}
```

Gọi sau `LvglPort::init()` và trước `AppManager::init()`. Nguồn phím: bàn phím ma trận + Serial (mục 4); `init()` gọi `KeypadMatrix::init()`. Chỉ nút bấm (`lv_btn`...) tự vào group mặc định; container `lv_obj` thì không.

### 6.5 `services/sd_storage.hpp`

Đã viết, test trên máy tính đạt (48 kiểm tra) và chạy đúng trên board. API dùng `std::string` (không dùng Arduino `String`).

```cpp
#pragma once
#include <FS.h>
#include <cstddef>
#include <string>
#include <vector>

namespace SdStorage {
// Mount thẻ trên bus HSPI riêng: thử 25 MHz, thất bại thì 10 rồi 4 MHz.
// Gọi lại khi đã mount thì trả true ngay. Gọi trong setup(), không cần LvglLock.
bool begin();
bool isMounted();

// Liệt kê file theo đuôi, không phân biệt hoa/thường; `exts` là danh sách cách nhau bởi ';',
// ví dụ ".jpg;.jpeg" (nullptr = mọi file). Trả đường dẫn đầy đủ, sắp xếp không phân biệt hoa/thường.
// Bỏ qua thư mục và file ẩn (bắt đầu bằng '.'). Rỗng nếu thư mục không tồn tại hoặc chưa mount.
std::vector<std::string> listFiles(const char* dir, const char* exts);

bool exists(const char* path);

// Đọc cả file vào PSRAM. Người gọi giải phóng bằng Psram::release(). Trả nullptr nếu lỗi
// (không có file, file rỗng, hết PSRAM, đọc thiếu); *outSize = 0 khi lỗi. Khóa SD giữ theo chunk 8 KB.
void* readFileToPsram(const char* path, size_t* outSize);

// Ghi đè file (tạo mới nếu chưa có). KHÔNG tự tạo thư mục cha. Trả true nếu ghi đủ.
bool writeFile(const char* path, const void* data, size_t len);

// Đọc theo dòng (audio). Mỗi lần open/read/seek tự khóa rồi nhả khóa. Hàm hủy tự đóng file.
class Reader {
 public:
  bool open(const char* path);            // mở lại file khác thì tự đóng file cũ
  size_t read(void* dst, size_t n);       // 0 = hết file hoặc lỗi
  bool seek(size_t pos);
  size_t size() const;
  bool isOpen() const;
  void close();
};
}
```

Mọi hàm gọi được từ nhiều task: dùng chung một mutex đệ quy bên trong, khóa chỉ giữ trong từng lần đọc/ghi (tối đa 8 KB), không giữ lúc giải mã. Trước `begin()` hoặc khi mount lỗi, mọi hàm trả giá trị rỗng/false, không treo.

**Khi thẻ bị rút (đo trên board, chưa sửa):**
- **`isMounted()` không đáng tin:** vẫn trả `true` sau khi rút thẻ (chỉ phản ánh lần mount lúc khởi động). Muốn biết thẻ còn hay không thì dựa vào kết quả thật của lần đọc (`exists`/`listFiles`/`readFileToPsram`), đừng dựa vào `isMounted()`.
- **Mỗi lần gọi SD khi không có thẻ chặn khoảng 300 ms** rồi mới trả rỗng/false. Gọi liên tiếp nhiều lần (ví dụ liệt kê rồi đọc từng file) có thể làm treo giao diện vài trăm ms mỗi lần. Hàm không treo vĩnh viễn và thẻ cắm lại thì tự đọc được tiếp. Tối ưu "fail nhanh" (nhớ thời điểm lỗi, bỏ qua vài giây) đã cân nhắc và để backlog (mục 11).
- Đang phát nhạc mà rút thẻ: `AudioService` về `STOPPED`, không treo (mục 12.1).

Thư mục trên thẻ SD: `/images/` (JPG, nhận cả `.jpg` và `.jpeg`), `/music/` (MP3), `/data/` (sự kiện lịch, trạng thái thú ảo, `slideshow.txt` là đường dẫn ảnh widget Main Screen, do `SlideshowService` tự tạo, không tạo tay). Thẻ phải được format FAT32 bằng SD Card Formatter (format nhanh của Windows trên thẻ từng dùng có thể để lại phân vùng mà ESP32 không đọc được, lỗi `no valid FAT volume`).

### 6.6 `services/image_service.hpp`

```cpp
#pragma once
#include <cstdint>

struct Image {
    uint16_t* pixels = nullptr;   // RGB565 trong PSRAM
    int w = 0, h = 0;
};

namespace ImageService {
// Giải mã JPG từ SD, thu nhỏ để vừa trong targetW x targetH (giữ tỉ lệ).
// Dùng scale 1/2, 1/4, 1/8 của bộ giải mã khi có thể.
// Chạy được ở task Core 0; KHÔNG chạm LVGL.
bool decode(const char* path, int targetW, int targetH, Image* out);
void free(Image* img);        // release pixels, reset về rỗng
}
```

Để dùng với LVGL: app tự bọc `Image` thành `lv_img_dsc_t` (cf `LV_IMG_CF_TRUE_COLOR`) và **chỉ tạo/sửa object LVGL ở task UI**.

decode() an toàn khi gọi từ nhiều task cùng lúc (mutex bên trong, vì TJpg_Decoder dùng đối tượng toàn cục). File được đọc cả vào PSRAM trước khi giải mã. Chỉ hỗ trợ JPG baseline, không progressive, bỏ qua EXIF orientation, không bao giờ phóng to ảnh (ảnh nhỏ hơn khung giữ nguyên). Người gọi giải phóng bằng ImageService::free().

### 6.7 `services/audio_service.hpp`

```cpp
#pragma once
#include <Arduino.h>

enum class AudioState : uint8_t { STOPPED, PLAYING, PAUSED, ERROR };

namespace AudioService {
bool begin();                              // I2S + task decode ở Core 0
bool play(const char* path);               // MP3/WAV từ SD
void pause();
void resume();
void stop();
void seek(int deltaSeconds);               // tua tương đối (+/-)
void setVolume(uint8_t v0to100);

AudioState state();
uint32_t positionMs();
uint32_t durationMs();                     // 0 nếu chưa biết
bool finished();                           // bài vừa kết thúc (đọc xong tự xóa cờ)

// Chỉ phát MP3 (không có WAV). Giải mã bằng minimp3, tự bỏ qua ID3v2. Mọi tần số lấy mẫu MP3 phổ biến; ra loa mono (L=R).
// Hai task ở Core 0: giải mã (đọc SD, ưu tiên 3, stack 32 KB) và xuất I2S (ưu tiên 4). Ring PCM mono 128 KB trong PSRAM.
// Thời lượng lấy từ header Xing/Info nếu có, không thì ước theo bitrate. Tua seek() ước theo byte nên chính xác với CBR.
// play() bỏ bài đang phát. Hết bài thì trạng thái về STOPPED và finished() trả true một lần. Không tự chuyển bài; việc đó do app.
// readPcmTap() trả PCM mono trước khi áp âm lượng; trả 0 khi không ở trạng thái PLAYING.
// Nhạc chạy nền độc lập với app: thoát Music thì bài đang phát chạy hết rồi dừng.
// begin() gọi lười từ MusicApp::onCreate (gọi lại an toàn). Có thể chuyển sang setup().

// Cho Visualizer: sao chép tối đa n mẫu PCM 16-bit mono gần nhất.
// Trả số mẫu thực tế. Không chặn, không khóa lâu.
size_t readPcmTap(int16_t* dst, size_t n);
}
```

**Ghi chú từ kiểm thử (mục 12.1):**
- `positionMs()` tính từ số mẫu đã **đẩy ra I2S** (không phải số mẫu đã giải mã). Nhờ vậy đối chiếu `positionMs()` với đồng hồ thật trong từng cửa sổ 500 ms phát hiện được dứt tiếng (tỉ lệ < 0,85 là dứt). Muốn đo thì đọc `millis()` ngay lúc gọi, đừng dùng mốc thời gian của vòng lặp trước.
- Sau `begin()` có hai task thường trú `audio_dec` và `audio_out` (tổng số task của firmware 7 → 9), không bị hủy khi thoát Music. Stack còn trống tối thiểu đo được: `audio_dec` 15060 B khi đang phát MP3 (31844 B lúc chưa phát), `audio_out` 3232 B.
- 3 lỗi nhỏ đã được sửa: (1) `begin()` trả lại buffer PSRAM, gỡ driver I2S nếu `i2s_set_pin` lỗi, và tái sử dụng 2 mutex; (2) `seek()` chặn ở ±1 ngày và cộng dồn bão hòa trong `int64_t`; (3) `finished()` đọc/xóa cờ trong critical section `gFinMux`.

### 6.8 `services/time_service.hpp`

```cpp
#pragma once
#include <cstdint>

struct DateTime {
    uint16_t year; uint8_t month, day;   // month 1-12
    uint8_t hour, minute, second;
    uint8_t weekday;                     // 0 = Chủ Nhật
};

namespace TimeService {
bool begin();                            // DS3231; nếu không có thì dùng giờ hệ thống
DateTime now();
void set(const DateTime& dt);            // ghi vào DS3231
bool syncNtp(const char* tz);            // tùy chọn, khi có WiFi
}
```

Đã viết (`services/time_service.hpp/.cpp`), test máy tính đạt (mục 12) và chạy đúng trên board. **API ở trên không đổi**; header thật trong `src/` có thêm comment chi tiết. Hành vi cụ thể:

- **Wire thuần**, tự đọc thanh ghi DS3231 (không dùng RTClib). I2C: `Pins::I2cSda/I2cScl` = GPIO 1/2, 100 kHz, timeout 50 ms, địa chỉ 0x68.
- **Giờ lưu trong chip và giờ `now()` trả về là giờ ĐỊA PHƯƠNG**, không phải UTC. Múi giờ mặc định `ICT-7`; `begin()` đặt múi giờ hệ thống và nạp giờ RTC vào đồng hồ hệ thống.
- `begin()` trả `true` chỉ khi có DS3231 **và** giờ trong chip đáng tin. Trả `false` khi: không có chip, lỗi I2C, **cờ OSF bật** (chip từng mất nguồn/pin) hoặc giờ trong chip hỏng. Khi `false`, `now()` dùng giờ hệ thống (bắt đầu từ 1970-01-01 07:00 ICT) cho tới khi có `set()` hoặc `syncNtp()` thành công. Gọi lại `begin()` trả kết quả lần đầu và không đọc lại chip (kể cả sau khi `set()` đã khôi phục giờ; lần khởi động sau mới trả `true`).
- `now()`: cache tối đa 200 ms (nhiều nơi gọi không dồn I2C); lỗi I2C thì rơi về giờ hệ thống rồi tự phục hồi; thứ trong tuần luôn tự tính từ ngày chứ không tin thanh ghi.
- `set()`: giá trị không hợp lệ (năm ngoài 2000-2099, ngày không tồn tại...) bị bỏ qua và in log; `weekday` do người gọi truyền vào bị bỏ qua; ghi xong thì xóa cờ OSF và đặt luôn giờ hệ thống. Nếu ghi chip lỗi, `now()` dùng giờ hệ thống cho tới lần `set()` thành công kế tiếp.
- `syncNtp()`: yêu cầu WiFi đã kết nối sẵn (service không quản lý WiFi); **chặn tới 8 giây** nên chỉ gọi từ task nền Core 0, không gọi từ task LVGL hoặc `loop()`. `tz` rỗng hoặc `nullptr` = `ICT-7`. Hiện chưa có chỗ nào gọi hàm này.
- Gọi `TimeService::begin()` trong `setup()` sau `SdStorage::begin()` và trước `SettingsStore::begin()` / `AppManager::init()`; không cần `LvglLock`.
- Chưa có giao diện đặt giờ trong app. Hiện đặt giờ bằng `tools/rtc_test` (mục 10).

### 6.9 `core/app_factories.hpp`: factory của app

Mỗi app cung cấp **một** hàm factory (đặt trong `apps/<tên>/<tên>_app.cpp`), trả về đối tượng cấp phát bằng `new`; AppManager sở hữu và `delete` sau `onDestroy()`.

```cpp
IApp* createMainScreenApp() __attribute__((weak));
IApp* createPetApp()        __attribute__((weak));
IApp* createMusicApp()      __attribute__((weak));
IApp* createVisualizerApp() __attribute__((weak));
IApp* createCalendarApp()   __attribute__((weak));
IApp* createAlbumApp()      __attribute__((weak));
IApp* createSettingsApp()   __attribute__((weak));
```

Khai báo `weak` nên app nào chưa được viết thì AppManager tự hiện màn hình thay thế ("<TÊN>" + "(chua cai dat)"), dự án vẫn build và chạy được. Ví dụ trong `album_app.cpp`: `IApp* createAlbumApp() { return new AlbumApp(); }` (định nghĩa ở phạm vi toàn cục, không nằm trong namespace).

### 6.10 `core/app_registry.hpp`: bảng đăng ký app

```cpp
struct AppInfo {
    AppId id;
    const char* key;     // khóa ổn định để lưu vào flash, KHÔNG đổi sau khi đã phát hành
    const char* label;   // tên ngắn, ASCII (font mặc định chưa có dấu tiếng Việt)
    const void* icon;    // lv_img_dsc_t* của bộ icon 0 (mặc định); nullptr = launcher hiện tên
    IApp* (*create)();   // factory; nullptr nếu app chưa viết (hiện màn hình thay thế)
    bool selectable;     // true = người dùng bật/tắt hiển thị trên Main Screen trong Settings
};

namespace AppRegistry {
constexpr size_t kLauncherSlots = 6;                    // = GRID_COLS * GRID_ROWS
constexpr size_t kMaxSelected = kLauncherSlots - 1;     // ô cuối luôn là Settings

const AppId* launcherSlots();       // các app đang chọn theo thứ tự bảng, rồi ô trống (COUNT), ô cuối = APP_SETTINGS
const AppInfo& info(AppId id);
size_t count();                     // duyệt bảng: thứ tự = thứ tự trong Settings và trên Main Screen
const AppInfo& at(size_t index);
constexpr uint8_t kThemeCount = 3;          // = Icons::kThemeCount
const void* icon(AppId id, uint8_t theme);  // lv_img_dsc_t* theo bộ icon; theme ngoài khoảng -> bộ 0; nullptr nếu app không có icon
AppId findByKey(const char* key);   // COUNT nếu không có

bool isSelected(AppId id);
size_t selectedCount();
bool setSelected(AppId id, bool on);// false nếu không chọn được (không selectable, hoặc đã đủ kMaxSelected)
void resetSelection();              // mặc định: các app selectable đầu tiên trong bảng, tối đa kMaxSelected
}
```

Bảng `kInfos` trong `app_registry.cpp` là nơi duy nhất liệt kê app. Thứ tự các app `selectable` hiện tại (theo mockup): Music, Pet, Calendar, Visualizer, Album; Main và Settings có `selectable = false`. `AppManager::createApp()` tra bảng này (không còn `switch`).

**Thêm app mới: 3 việc.** Thêm một giá trị vào `AppId`, khai báo factory weak trong `app_factories.hpp`, thêm một dòng vào `kInfos`. Settings và Main Screen tự cập nhật; không sửa `app_manager` hay `main_screen`. Đổi tên chỉ sửa dòng trong bảng. App muốn có icon thì thêm một giá trị vào `Icons::Id`, 3 ảnh PNG (mỗi theme một ảnh, 64x64, trắng trên nền trong suốt), sinh lại `icons_t*.cpp` và thêm một `case` trong `AppRegistry::icon()`. App chưa có icon vẫn hiện tên.

**Icon app:** 3 bộ vẽ tay theo kiểu pixel art (theme `night` = bộ 0 phù thủy/trăng, `mint` = bộ 1 rừng, `pink` = bộ 2 hoa anh đào), mỗi bộ 6 icon (music, pet, visual, calendar, album, settings), 64x64, luôn màu trắng, không tô màu theo theme. Dữ liệu `lv_img_dsc_t` (RGB565 trắng + alpha) nằm trong flash, không tốn RAM, lấy bằng `Icons::get(Icons::Id, theme)`. Chỉ hiện khi `!UI_COMPACT`; màn nhỏ hoặc app chưa có icon thì hiện tên. Chọn bộ icon theo khóa theme trong `iconSet()` của `main_screen.cpp`; theme mới chưa có bộ riêng dùng bộ 0.

Danh sách app hiện trên Main Screen là lựa chọn của người dùng (tối đa `kMaxSelected`), lưu bằng khóa (`key`) nên thêm hoặc đổi thứ tự app trong bảng không làm hỏng dữ liệu đã lưu. Ô trống vẫn là nút có viền focus, bấm 5 không làm gì.

### 6.11 `core/theme.hpp`: theme giao diện

```cpp
struct Theme {
    const char* key;            // khóa ổn định để lưu vào flash
    const char* name;           // tên hiển thị (ASCII)
    uint32_t bg;                // nền màn hình
    uint32_t panel;             // khối lớn, nền ô/nút nổi bật
    uint32_t accent;            // màu nhấn
    uint32_t text;              // chữ trên nền màn hình
    uint32_t textOnPanel;       // chữ trên khối panel
    uint32_t focus;             // viền khi focus
    uint32_t icon;              // màu icon
    uint32_t scrim;             // khối làm mờ đặt sau chữ khi có ảnh nền
    uint8_t scrimOpa;           // độ đặc của scrim 0..255
    const lv_img_dsc_t* bgImage;// ảnh nền toàn màn hình RGB565 (UI_W x UI_H), nullptr = nền màu bg
};

namespace Themes {
size_t count();
const Theme& at(size_t index);      // ngoài khoảng -> theme đầu
const Theme& current();
bool select(const char* key);       // false và giữ nguyên nếu khóa lạ. Gọi AppManager::reload() sau đó để vẽ lại
void applyToScreen(lv_obj_t* screen);// AppManager gọi khi dựng màn hình mới
}
```

Chỉ gọi từ ngữ cảnh UI (đang giữ `LvglLock`). Theme có sẵn trong `theme.cpp`: `pink` (mặc định), `night`, `mint`. Thêm theme: thêm một khối vào cuối mảng `kThemes`, không đổi khóa theme đã có. Muốn có bộ icon riêng cho theme mới thì làm thêm một bộ icon và một dòng trong `iconSet()` (`main_screen.cpp`); chưa làm thì Main Screen dùng bộ icon 0.

Màu trong app lấy bằng `lv_color_hex(Themes::current().panel)`... Ảnh nền: đặt `bgImage` để `applyToScreen` đặt ảnh làm nền màn hình, chữ trong suốt nổi lên trên; để chữ dễ đọc, đặt khối `scrim` bán trong suốt phía sau vùng có chữ. Ảnh nền chưa có ảnh nào (chờ SD/ImageService). Theme nạp từ thẻ SD (không cần nạp lại firmware) là hướng làm sau.

### 6.12 `services/settings_store.hpp`: lưu cài đặt

```cpp
namespace SettingsStore {
void begin();   // đọc từ flash NVS, áp vào Themes và AppRegistry. Gọi trước AppManager::init()
void save();    // ghi theme hiện tại + danh sách app đang chọn
}
```

Lưu hai khóa trong namespace NVS `cyberdeck`: `theme` (khóa theme) và `apps` (khóa app cách nhau dấu phẩy, theo thứ tự bảng). Chưa có dữ liệu thì dùng mặc định; khóa lạ, trùng, hoặc vượt `kMaxSelected` bị bỏ qua. NVS lỗi thì giữ nguyên trạng thái. Nạp lại firmware không xóa cài đặt; xóa bằng `pio run -t erase`.

### 6.13 `services/slideshow_service.hpp`: ảnh widget Main Screen

Người dùng chọn một ảnh (từ Album), Main Screen hiển thị ảnh đó ở nửa trái. Không có chu kỳ, không tự chuyển ảnh. Chỉ có một task giải mã tồn tại tạm thời (Core 0, stack 8 KB, ưu tiên 1), chạy xong tự xóa.

```cpp
#pragma once
#include <lvgl.h>

namespace SlideshowService {
// ALBUM gọi: chọn ảnh hiển thị. Xóa cache cũ, lưu đường dẫn vào /data/slideshow.txt.
// false nếu đường dẫn không hợp lệ; nếu ghi SD lỗi thì vẫn áp dụng trong RAM (mất sau khi
// khởi động lại) và cũng trả false. Chỉ gọi khi Main Screen KHÔNG hiển thị.
bool setPhoto(const char* path);
void clearPhoto();                    // bỏ ảnh đã chọn, widget hiện khung trống

// MAIN SCREEN gọi trong onCreate: đọc đường dẫn (lần đầu), nếu có ảnh mà chưa có cache thì
// bật task Core 0 giải mã vừa khung targetW x targetH. Không chặn.
void request(int targetW, int targetH);
bool settled();                       // true khi không còn chờ (có ảnh, không có ảnh, hoặc lỗi)
const lv_img_dsc_t* image();          // nullptr nếu không có; con trỏ sống đến setPhoto/clearPhoto kế tiếp
}
```

- Ảnh được xử lý kiểu "cover": service tự phóng/thu cho phủ kín khung rồi cắt chính giữa, lấy mẫu song tuyến tính RGB565 ra đúng kích thước khung (không méo, không dải trống). Ảnh nhỏ hơn khung được phóng to để phủ kín (ảnh quá nhỏ sẽ hơi mờ). Bước này không phụ thuộc cách `ImageService::decode` thu ảnh: service chỉ nhờ decode giải mã ở cỡ gần nguyên bản (tối đa 4 lần khung).
- Kết quả cache trong PSRAM (khoảng 71 KB với khung 154x232), vào/ra Main Screen không giải mã lại. Pixel thuộc service, không thuộc widget, nên không bị giải phóng khi thoát Main Screen. Bộ đệm giải mã lớn hơn chỉ tồn tại tạm thời trong task, giải phóng ngay sau khi xử lý. Cache này là thiết kế có chủ đích, nên phép đo rò rỉ phải đặt ảnh widget trước khi lấy mốc.
- Giải mã lỗi thì Main Screen hiện chữ cố định "dinhh py" và thử lại ở lần vào Main kế tiếp.
- Mọi trạng thái chung bảo vệ bằng một mutex; khóa luôn lấy theo thứ tự mutex của service rồi tới khóa SD.

---

## 7. Hợp đồng riêng của từng app

| App | Gọi service | Phím đặc thù | Ghi chú |
|---|---|---|---|
| Main Screen | Time (`TimeService::now()`, giờ địa phương từ DS3231), Image (slideshow), Slideshow (SlideshowService) | 4/6 đi vòng widget trái → cột 1 → cột 2 → widget trái; 2/8 đổi hàng trong lưới (quay vòng); 5 mở app (widget trái mở Album) | Nửa trái: `SlideshowWidget` (`slideshow_widget.hpp/.cpp`), focus được, viền màu focus của theme; ngày giờ đè ở đáy (2 dòng khi `UI_COMPACT`); Ảnh nửa trái lấy từ `SlideshowService` (mục 6.13): `onCreate` gọi `request(kW, kH)`, `onTick` gọi `settled()`/`image()` rồi `setImage()` một lần. Khung trống (chưa chọn ảnh hoặc lỗi) hiện chữ "dinhh py", màu theo theme (`panel`, `textOnPanel`, `focus`, `scrim`). Nửa phải: lưới icon 2 cột x 3 hàng theo `AppRegistry::launcherSlots()` (mục 6.10): app người dùng chọn, ô trống (vẫn focus được, không làm gì), ô cuối là Settings. Nút lưới hiện icon 64x64 (AppRegistry::icon(id, theme), luôn màu trắng, không dùng ICON_SIZE) khi !UI_COMPACT, app chưa có icon hoặc màn nhỏ thì hiện tên. Nền nút, viền focus và chữ lấy từ theme (panel, focus, textOnPanel). Nhớ vị trí focus (kể cả widget trái) khi quay lại. ESC bị bỏ qua vì là màn gốc. Mở app bằng `AppManager::goTo()`. Khi thiếu RTC tin cậy, đồng hồ hiện giờ giả `1/1 07:00` (chưa đổi thành `--:--`, mục 11) |
| Settings | SettingsStore | 2/8 lên xuống (quay vòng), 4/6 hoặc 5 đổi theme ở hàng đầu, 5 bật/tắt app ở các hàng sau, 1 thoát | Hàng đầu: theme (`< tên >`). Các hàng sau: mỗi app `selectable` của bảng đăng ký với `[x]`/`[ ]`; chọn tối đa `kMaxSelected`, vượt thì báo "Da du N app". Chân màn hình ghi số app đã chọn. Mỗi thay đổi lưu flash ngay; đổi theme thì `AppManager::reload()` và nhớ hàng đang chọn. Danh sách cuộn dọc khi nhiều app. Do chat 0 viết. Vào/ra khoảng 17-19 ms, gần như không chiếm bộ nhớ |
| Pet | Không dùng (trạng thái lưu trong RAM; sau này nối SdStorage vào /data/) | 4/6/2/8 chọn nút, 5 kích hoạt | 6 nút tròn quanh thú (3 trái, 3 phải): Eat, Play, Bath, Sleep, Skin và một ô trống. Eat/Play/Bath/Sleep có icon pixel và mặt nạ tối dần theo chỉ số (kim đồng hồ từ 12 giờ). Skin hiện màu lông và xoay vòng: tam thể, tuxedo, trắng, cam vằn. Mèo buồn khi một trong Eat/Play/Bath dưới 40. Sleep là vòng năng lượng: hết một vòng thì mèo tự ngủ, nền chuyển sang đêm, ngủ đầy thì tự dậy. Mèo là lưới pixel 20x20 phóng số nguyên lần (x3 trên 160x128, x6 trên 320x240), có hoạt ảnh chớp mắt, giật tai, vẫy đuôi, thở khi ngủ, phản ứng khi bấm nút. Nền gồm canvas ngày/đêm, mây trôi và sao nhấp nháy. Trạng thái thú ở một đối tượng toàn cục (PetState) nên vẫn chạy khi ở app khác. App gọi tick của sprite và nền trong onTick. Độc lập, không cần SD để chạy. Hình mèo và nền ngày/đêm không đổi theme. **Chi phí (đo trên board):** nặng nhất trong các app: chiếm khoảng 455 KB PSRAM khi đang mở (trả lại đủ khi thoát) và vào mất khoảng 165-180 ms (các app khác 17-70 ms), trong lúc đó `loop()` bị chặn tới khoảng 270 ms. `PET_DECAY_FAST = 1` là chế độ thử (chỉ số giảm khoảng 100 điểm mỗi 10 phút); khi dùng thật nên đặt 0 (mục 11) |
| Music | Audio, SdStorage, ImageService (qua DiscCover), đọc /data/slideshow.txt | 2/8 chọn bài; 5 phát bài đang chọn, hoặc Play/Pause nếu là bài đang phát; 4/6 tua ∓10 giây; 7/9 bài trước/sau; 3 (KEY_OPTION) tăng âm lượng; 1 về Home | Trái là đĩa quay (ảnh mặt đĩa dùng chung ảnh đã chọn cho widget Main Screen, chọn ở Album bằng phím 3), thanh tiến độ, thời gian, nút điều khiển; phải là tên bài (cuộn ngang), số bài, âm lượng (biểu tượng loa + thanh) và playlist quét /music/*.mp3 (tối đa 64 bài). Âm lượng hiển thị 10..100, bước 10; mức hiển thị 100 ứng với mức 80 của AudioService::setVolume (mức cao hơn bị rè trên loa này). Mặc định 50. Ảnh đĩa: task Core 0 giải mã, cắt vuông giữa, dựng sẵn 45 khung xoay cách nhau 8° vào PSRAM (khoảng 1,2 MB với đĩa 116 px), cắt tròn, phần góc tô màu nền theme; lúc phát chỉ đổi khung mỗi 100 ms. Chưa có ảnh hoặc lỗi thì đĩa hiện hai cung quay. Theme có ảnh nền (bgImage) sẽ lộ phần góc. Tự chuyển bài khi hết bài chỉ khi app đang mở; rời app thì bài chạy hết rồi dừng. **Bộ nhớ còn giữ sau khi thoát (chấp nhận, có chủ đích):** bộ đệm 45 khung đĩa (khoảng 1,2 MB PSRAM) được giữ lại như cache để lần vào sau không dựng lại; nó được thay khi đổi ảnh widget. Lần vào Music đầu tiên làm PSRAM trống giảm khoảng 1,38 MB và heap nội bộ giảm khoảng 47 KB (gồm cache đĩa, ring PCM 128 KB, hai task audio), sau đó không tăng thêm (đo 8 lần vào/ra: lệch tối đa vài chục byte). Khi đã có cache, vào/ra Music chiếm thêm khoảng 2 KB PSRAM + 1 KB heap, mất 62-72 ms (lần đầu hơi lâu hơn, khoảng 17 ms). Đây là phần PSRAM chiếm thường trực lớn nhất của firmware |
| Visualizer | Audio (`readPcmTap`) | 7/9 đổi kiểu hiệu ứng, 4/6 chỉnh dạng (số thanh; waveform: độ phóng biên độ), 1 thoát | Phím 7/9 không tới widget dưới dạng LV_EVENT_KEY (LVGL 8.3 chỉ dùng chúng để dời focus), nên app đặt hai widget "gác" 1x1 trong suốt hai bên widget chính trong group: focus bị dời sang widget gác nghĩa là người dùng bấm 7 hoặc 9, app trả focus về widget chính ngay trong callback. Kiểu và mức 4/6 nhớ ở biến tĩnh. Các hằng số chỉnh độ nhạy/độ mượt/fps (kFrameMs, kDbFloor, kDbRange, kTiltDb, kAttack, kFall) nằm đầu visualizer_app.cpp. Màu thanh lấy từ accent, vạch đỉnh từ focus; theme quá tối trên nền đen thì rơi về text, rồi trắng. Chi phí đo trên board: vào/ra 17-20 ms, chiếm khoảng 7 KB PSRAM + 3 KB heap khi mở |
| Calendar | Time, SdStorage (`/data/events.json`) | 2/8/4/6 di chuyển ngày, 7/9 đổi tháng | Tiêu đề tháng, hàng `S M T W T F S`, lưới ngày 7 cột x 6 hàng. Không thuộc phạm vi chat 9 nên chưa được đo hay kiểm thử rò rỉ |
| Album | Image, SdStorage | 5 xem toàn màn hình, 1 quay lại, 3 = đặt ảnh làm ảnh widget Main Screen | Lưới 4x4 (3x3 khi UI_COMPACT), tối đa 256 ảnh trong /images. Thumbnail giải mã ở task nền Core 0, kết quả về UI qua queue và onTick. 2/4/6/8 di chuyển quay vòng, 5 xem toàn màn hình (4/6 đổi ảnh, 5 hoặc 1 đóng), 1 ở lưới về Home. Phím 3 (KEY_OPTION) gọi SlideshowService::setPhoto() cho ảnh đang xem, hoặc ô đang chọn khi ở lưới. Thông báo "Da dat lam anh nen" hoặc "Khong luu duoc" hiện 1,5 giây, tự ẩn bằng millis() trong onTick. Màn xem ảnh có gợi ý mờ "3: dat nen" ở góc trên phải. Chi phí đo trên board (9 ảnh): vào khoảng 50-59 ms, chiếm khoảng 58 KB PSRAM + 10 KB heap + 1 task worker tạm. **Thoát Album chậm hơn các app khác**: thường 30-70 ms, thỉnh thoảng 120-270 ms (phải dừng worker và dọn thumbnail), làm `loop()` bị chặn tương ứng |

### 7.1 Quy ước giao diện

- Ảnh mockup (tông hồng, 6 màn) chỉ để **minh họa** bố cục. Không bắt chước nét vẽ tay: dùng các khối/hình học của LVGL (bo góc, viền, màu phẳng). **Không dùng hiệu ứng glow.** Font chữ dùng mặc định của LVGL.
- Màu lấy từ theme (quy tắc vàng số 5, mục 6.11). Theme mặc định `pink` giữ tông hồng của mockup.
- Icon app tách ra một chỗ riêng để cập nhật (mục 6.10).
- Màn 2.8" (320x240) là màn đang dùng, mỗi nửa màn hình rộng khoảng 160 px. Màn 1.8" (160x128) chật hơn rất nhiều: dùng `UI_COMPACT` để rút gọn khi cần.
- Ngoại lệ màu nền: Visualizer dùng nền đen cố định (`lv_color_black()`, không phải hex) để thanh màu nổi bật; mọi màu còn lại vẫn lấy từ `Themes::current()`. Nếu `accent` của một theme quá tối trên nền đen thì rơi về `text`.

---

## 8. Lộ trình chat

| # | Chat | Phụ thuộc | Trạng thái |
|---|---|---|---|
| 0 | Core + hạ tầng: `platformio.ini`, `config/`, `lvgl_port`, `input_service`, `app_manager`, `app_registry`, `theme`, `settings_store`, app Settings, `psram` | - | Bước 1-5 xong và chạy đúng trên board (TFT/LVGL, phím serial, AppManager, bàn phím 3x3, màn ST7789 + theme + bảng đăng ký + Settings). Bước 6: `sd_storage` xong và chạy đúng trên board. Phần cứng I2S (Chat 5) và DS3231 (Chat 2) đã làm sau đó |
| 1 | Main Screen (khung) + chuyển app, app stub | 0 | Xong và chạy đúng trên board: widget trái (đồng hồ + ảnh qua `SlideshowService`), focus/điều hướng hai nửa, phím 5 mở Album, icon app 3 bộ theo theme (64x64, trắng). Màu nút, viền focus, khung ảnh lấy từ theme. Đã có test máy tính với LVGL thật (`tools/app_test`, chat 9). Đồng hồ đã chuyển sang `TimeService::now()` (Chat 2), hiện đúng giờ thật trên board. |
| 2 | Time service | 0 | Xong và chạy đúng trên board: `TimeService` (Wire thuần, giờ địa phương ICT-7, cờ OSF, cache 200 ms, mutex), `tools/rtc_test` + `sync_time.py`, Main Screen dùng `TimeService::now()`. Test máy tính 160661 kiểm tra (ASan + UBSan). `syncNtp` đã viết, chưa thử trên board, chưa có chỗ gọi |
| 3 | Image service + Album | 0 | Xong và chạy đúng trên board: giải mã JPG, lưới thumbnail, xem toàn màn hình. Test máy tính cho phần resize (32 kiểm tra) và cho Album (`tools/app_test`). Phím 3 đặt ảnh widget đã test trên board (mục 12) |
| 4 | Slideshow widget (Core 0) | 1, 3 | Xong và chạy đúng trên board. Giải mã 1 ảnh ở Core 0, cache PSRAM, cắt giữa kiểu cover, lưu đường dẫn vào /data/slideshow.txt. Không có chu kỳ chuyển ảnh (người dùng chọn từ Album). Phím chọn ảnh ở Album (phím 3) đã thêm. Test máy tính `tools/slideshow_test` (chat 9) |
| 5 | Audio service + Music Player | 0 | Xong và chạy đúng trên board: AudioService (minimp3 + I2S), Music Player, ảnh đĩa xoay, âm lượng. Test máy tính `tools/audio_test` (chat 9). Chân I2S đã có trong `config/pins.hpp`; `audio_service.cpp`. |
| 6 | Visualizer | 5 | Xong và chạy ổn trên board: fft + VisualizerApp 3 kiểu, 7/9 đổi kiểu, 4/6 chỉnh dạng. Test máy tính đạt (fft 2695 kiểm tra, VisualizerApp 1142 kiểm tra bằng stub LVGL). Đã kiểm tra rò rỉ qua nhiều lần vào/ra trên board (chat 9). Chưa đo FPS bằng số (xem bằng mắt thấy mượt) |
| 7 | Calendar | 2 | |
| 8 | Pet | 0 | Xong và chạy đúng trên board: chỉ số, icon pixel, nền ngày/đêm động, mèo ngủ, hoạt ảnh. Chưa lưu SD (chờ SD và DS3231). Tốc độ giảm chỉ số đã chốt ở mức hiện tại (PET_DECAY_FAST = 1; nên đặt 0 khi dùng thật). Test máy tính `tools/app_test` (chat 9) |
| 9 | Tối ưu + kiểm thử | tất cả | Xong (trừ Calendar, ngoài phạm vi): A) kiểm thử rò rỉ và độ ổn định trên board (`leak_test`, giai đoạn 0-7) đạt; B) test máy tính audio_service, slideshow_service, Main Screen/Album/Pet đạt; C) đo hiệu năng trên board (`perf_test`) đạt, soak 30 phút không rò rỉ, không dứt tiếng. Chi tiết và số đo ở mục 12.1. Các điểm còn mở ghi ở mục 11 |

Thứ tự gợi ý để sớm thấy kết quả: 0 → 1 → 8 → 2 → 3 → 4 → 5 → 6 → 7 → 9.

---

## 9. Prompt mở đầu cho mỗi chat con

> Đây là chat cho **app/service X** của dự án Cyberdeck. Hãy tuân thủ `ARCHITECTURE.md`, `concept.md`, `instruction.md`.
> Chỉ viết code trong `apps/<x>/` (hoặc `services/<x>` nếu chat này được giao service đó). Gọi các service qua đúng header đã định nghĩa, **không sửa header chung**. Nếu cần đổi API, hãy báo trước và đề xuất bản cập nhật cho `ARCHITECTURE.md`.
> Bắt buộc: `.hpp/.cpp` tách riêng, không `delay()`, buffer lớn trong PSRAM, không gọi LVGL từ Core 0. Cung cấp hàm factory `createXxxApp()` (mục 6.9). Tự xử lý `LV_KEY_ESC` và phím mũi tên trong app (mục 4). Callback widget phải chịu được việc chạy sau `onDestroy()` (mục 6.3). **Màu lấy từ `Themes::current()`, không ghi hex** (mục 6.11). App mới đăng ký bằng một dòng trong `app_registry.cpp` (mục 6.10).

## 10. Checklist test phần cứng (sketch tối giản trước khi ghép LVGL)

- [x] Quét keypad, in mã phím ra serial (có cả giữ lâu)
- [x] TFT: tô màu + vẽ chữ bằng TFT_eSPI thuần (ST7735S và ST7789)
- [x] SD: mount, liệt kê file, đo tốc độ đọc (`tools/sd_test`, 25 MHz, khoảng 1,5 MB/s)
- [x] I2S: phát tiếng bíp 440 Hz
- [x] DS3231: đọc/ghi giờ, in ra serial (`tools/rtc_test`, giữ giờ khi rút nguồn đã thử khoảng 1 giờ)
- [x] Kiểm tra `psramFound()` và `ESP.getFreePsram()`

**DS3231 (`tools/rtc_test`):** env `rtc_test` trong `platformio.ini` có `build_src_filter` gồm `../tools/rtc_test/rtc_test.cpp` và `services/time_service.cpp` (cùng `build_flags` như `sd_test`). Nạp: `pio run -e rtc_test -t upload`, mở: `pio device monitor -e rtc_test`. Lệnh gõ trong monitor: `s YYYY-MM-DD HH:MM:SS` đặt giờ địa phương, `t` in giờ, `r` đọc thanh ghi thô + nhiệt độ chip + cờ OSF và đối chiếu với `TimeService::now()`, `i` quét I2C và in lại kết quả `begin()`, `p` bật/tắt dòng giờ mỗi giây, `h` trợ giúp. `tools/rtc_test/sync_time.py <cổng>` đặt giờ từ laptop và luôn gửi giờ UTC+7, không phụ thuộc múi giờ laptop (WSL thường để UTC); cần tắt `pio device monitor` trước vì một chương trình mới mở được cổng serial.

## 11. Quyết định còn mở

- [ ] Tab màu thực tế của ST7735S 1.8" (đang dùng `cyberdeck_st7735` = BLACKTAB và hiển thị đúng; xác nhận lại tab nếu từng thử env khác)
- [x] Màn 2.8": ST7789 240x320 không cảm ứng, đã chọn và chạy đúng (`TFT_BGR` + `invertDisplay(false)`, mục 3.1)
- [x] Nhạc tiếp tục phát khi thoát Music Player: bài đang phát chạy hết rồi dừng (mục 6.7)
- [x] Định dạng nhạc ưu tiên và thư viện giải mã: MP3 + minimp3 (xem các mục đã chốt bên dưới)
- [ ] Nguồn pin và mạch sạc (làm sau cùng)
- [ ] Host SPI thực tế của TFT_eSPI trên bản Arduino core đang dùng (mục 1.2: mặc định FSPI; xác nhận khi làm SD)
- [x] Ô trống cuối lưới Main Screen: đã chốt là Settings
- [x] Nền ngày/đêm và hình ảnh mèo của Pet đã chốt (màu lấy từ ảnh mẫu)
- [ ] Nền hoàng hôn và trời mưa: mèo phản ứng ra sao chưa quyết định
- [x] Theme: bảng màu có tên, người dùng chọn trong Settings, lưu NVS (mục 6.11, 6.12)
- [x] Theme áp cho widget slideshow của Main Screen (đã xong)
- [ ] Theme: ảnh nền (cần SD/ImageService), nạp theme từ thẻ SD
- [x] Chế độ hiển thị ảnh widget: hiện luôn cover (phủ kín, cắt giữa). Nếu muốn chọn vùng cắt hoặc hiện nguyên ảnh (letterbox), cần thêm tùy chọn vào SlideshowService
- [ ] Danh sách app: thêm app thứ 7 trở lên thì Settings cuộn được nhưng launcher giới hạn `kMaxSelected` = 5 app chọn cùng lúc
- [x] Nhạc không tự chuyển bài khi rời Music: bài đang phát chạy hết rồi dừng
- [x] Định dạng nhạc: MP3 (minimp3), không hỗ trợ WAV/AAC. Người dùng tự đổi file sang MP3 128 kbps, tên không dấu
- [x] Theme có ảnh nền (bgImage) thì ảnh đĩa Music lộ phần góc (đang bake màu bg của theme)
- [x] Visualizer: không hiện tên bài (không cần API tên bài trong AudioService), 3 kiểu (thanh đối xứng giữa, waveform, thanh đối xứng hai viền), nền đen, FFT 256 ở Core 1, thoát app thì hủy, nhạc chạy nền
- [ ] Visualizer: nếu bass thô trên board thì đổi FFT lên 512 (đổi một hằng constexpr)
- [x] Icon app Main Screen: 3 bộ icon theo 3 theme, 64x64, luôn màu trắng (không tô theo theme); chọn bộ theo khóa theme
- [x] Time service: Wire thuần (không RTClib), I2C 0x68 ở 100 kHz, DS3231 lưu giờ địa phương, múi giờ mặc định `ICT-7` (mục 6.8)
- [x] Khi chưa có RTC tin cậy (không chip hoặc cờ OSF): dùng giờ hệ thống, `begin()` trả `false`. Main Screen khi đó hiện giờ giả `1/1 07:00` (ICT); đã thử trên board: rút I2C thì hiện 1/1 07:00, cắm lại thì về đúng giờ
- [ ] NTP (`TimeService::syncNtp`): đã viết, có test máy tính bằng SNTP giả, chưa thử trên board; chưa có chỗ gọi và chưa có cách cấp SSID/mật khẩu WiFi (quyết định khi làm WiFi)
- [ ] Giao diện đặt giờ trong app (Settings hoặc Calendar): hiện chỉ đặt qua serial bằng `tools/rtc_test`
- [x] Thử trên board với firmware chính: rút dây I2C (kịch bản mất pin/cờ OSF chưa thử)

**Phát hiện từ chat 9 (chưa quyết định, chưa sửa):**
- [x] Music giữ lại khoảng 1,2 MB bộ đệm khung đĩa sau khi thoát (cache thay thế được khi đổi ảnh): chấp nhận, ghi ở mục 7
- [ ] SD "fail nhanh" khi không có thẻ: hiện mỗi lần gọi chặn khoảng 300 ms (mục 6.5). Đề xuất: nhớ thời điểm lỗi và trả lỗi ngay trong vài giây. Để backlog
- [ ] `SdStorage::isMounted()` không phản ánh việc rút thẻ (mục 6.5): sửa hoặc đổi tên/ghi chú cho đúng
- [ ] Main Screen: khi thiếu RTC nên hiện `--:--` thay vì giờ giả `1/1 07:00`
- [ ] `PET_DECAY_FAST` đang là 1 (giảm khoảng 100 điểm mỗi 10 phút, để thử); đặt 0 khi dùng thật
- [x] 3 lỗi nhỏ của `audio_service` (mục 6.7): rò khi `begin()` lỗi I2S, `seek()` tràn số, `finished()` không nguyên tử (đã sửa)
- [ ] Pet vào chậm (khoảng 165-180 ms) và chiếm 455 KB PSRAM: có thể tối ưu (dựng sprite/nền ít hơn hoặc lười) nếu muốn
- [ ] `loop()` có thể bị chặn tới khoảng 340 ms khi chuyển app (thoát Album, vào Pet), không làm dứt tiếng nhạc; nếu sau này cần phản hồi phím nhanh hơn thì cân nhắc tách việc dọn Album khỏi `tick()`
- [ ] Thời gian chuyển app đo "đầu-cuối" bởi `perf_test` luôn cao hơn số `[AppManager]` khoảng 60-90 ms (chưa rõ nguyên nhân: chờ vẽ khung/lượt `loop()`), cần tìm hiểu nếu muốn con số cảm nhận thật

## 12. Nhật ký kiểm thử chat 0 (làm lại từ đầu)

Bản làm trước bị boot loop: `StoreProhibited` trong `TFT_eSPI::init()` (khớp issue #3346 của TFT_eSPI trên ESP32-S3). Cách xử lý là ghim phiên bản (mục 3.1).

**Bước 1: test TFT (đã chạy trên board)**
- [x] Hết boot loop với `espressif32@6.5.0` + `TFT_eSPI@2.5.43`; `tft.init()` chạy được
- [x] Màn ST7735S hiển thị đúng; PSRAM nhận đủ (khoảng 8189 KB trống), heap ổn định
- [x] Log serial đi qua cổng **USB native** (do `ARDUINO_USB_CDC_ON_BOOT=1`), không phải cổng UART

**Bước 2: LVGL + phím qua serial (đã chạy trên board)**
- [x] LVGL vẽ được lên TFT; phím 1, 5, 7, 9 đúng
- [x] Phát hiện: LVGL 8.3 không tự dời focus cho phím mũi tên. Đã chuyển sang app tự xử lý, điều hướng lưới quay vòng chạy đúng

**Bước 3: AppManager + launcher + app stub**
- [x] Test trên máy tính: LVGL 8.3.11 thật + stub phần cứng + AddressSanitizer, 38 kiểm tra đạt (điều hướng quay vòng, mở từng app, ESC/HOME/OPTION, ô trống, nhớ focus, 300 lần vào/ra app không tăng bộ nhớ)
- [x] Test bắt được lỗi use-after-free khi `delete` app trước khi dọn group focus; đã sửa, thứ tự chuyển app ghi ở mục 6.3
- [x] Chạy trên board: chuyển app được nhưng màn hình app stub trông như màn hình đen. Nguyên nhân: chữ mặc định tối trên nền đen. Đã tái hiện bằng test chụp khung hình (độ sáng chữ 32/255), sửa bằng chữ trắng mặc định (255/255) (mục 6.3)
- [x] Đã xác nhận chạy đúng trên board sau bản sửa (task LVGL trên Core 1, heap LVGL trong PSRAM đều ổn)

**Bước 4: bàn phím 3x3 thật**
- [x] Test trên máy tính với mô hình GPIO giả lập có phát hiện chập mạch: 56 kiểm tra bộ quét (rảnh 5 s không thao tác GPIO nào, đủ 8 phím, phím 1 ngắn = ESC khi thả, giữ 800 ms = một HOME và không ESC, dội phím, nhiều phím, ghost chữ L, giữ phím lúc khởi động, 0 chập mạch, tối đa 1 hàng OUTPUT khi quét) và 55 kiểm tra tích hợp với LVGL thật (có điều hướng, mở app, OPTION, ESC, giữ 1 về Home, 300 lần vào/ra app không rò rỉ)
- [x] Đã đấu dây theo `docs/keypad_wiring.svg` và chạy đúng trên board

**Bước 5: màn ST7789, theme, bảng đăng ký, Settings**
- [x] Màn ST7789 2.8": lúc đầu sai màu. Đoạn tô thử đỏ/xanh lá/xanh dương/trắng/đen hiện vàng/tím/lục lam/đen/trắng, nghĩa là vừa đảo màu vừa đổi chỗ đỏ-xanh dương. Sửa bằng `invertDisplay(false)` + `TFT_RGB_ORDER=TFT_BGR`, đã đúng trên board (mục 3.1). Cờ `-DTFT_INVERSION_ON` không có tác dụng như mong đợi nên không dùng
- [x] Bố cục Main Screen và Pet đúng trên 320x240; chữ font mặc định hơi nhỏ nhưng được chọn giữ
- [x] Theme: test trên máy tính đạt (chọn theme, khóa lạ giữ nguyên, nền đổi màu sau `reload()`, giữ vị trí focus, màn app dùng nền mới, 200 lần reload không rò rỉ). Trên board: đổi theme đổi màu nền và nút đúng
- [x] Bảng đăng ký, `SettingsStore`, app Settings: test trên máy tính đạt: bảng đăng ký (giới hạn chọn, thứ tự theo bảng, ô dồn lên, khóa không trùng), lưu/khôi phục NVS (kể cả dữ liệu hỏng, khóa lạ, danh sách rỗng, NVS lỗi), tích hợp (mở Settings từ ô cuối, đổi theme, bật/tắt app, quay vòng, thoát, Main Screen hiện đúng sau khi tắt app, 100 lần vào/đổi theme/thoát không rò rỉ). Tổng 105 kiểm tra tích hợp đạt. Đã xác nhận chạy đúng trên board
- Bài học test: số byte heap tăng một lần ở các lần chuyển app đầu tiên (LVGL cấp cache nhỏ), nên đo rò rỉ sau vài vòng khởi động nguội

**Bước 6: thẻ SD**
- [x] Sketch `tools/sd_test` trên board: lần đầu lỗi `f_mount failed (13) no valid FAT volume` (kể cả sau Format của Windows); format lại bằng SD Card Formatter (Overwrite format) thì mount OK ở 25 MHz, thẻ SDHC 7580 MB, đọc 4 ảnh + 3 file nhạc, tốc độ đọc khoảng 1,5 MB/s. Đấu dây đúng ngay từ đầu (lỗi do định dạng thẻ)
- [x] `services/sd_storage` test trên máy tính (thẻ giả lập bằng thư mục thật, AddressSanitizer + UBSan, 48 kiểm tra đạt): chưa mount thì mọi hàm trả rỗng/false, thử 25 → 10 → 4 MHz, `listFiles` (nhiều đuôi, hoa/thường, bỏ file ẩn và thư mục, sắp xếp), đọc vào PSRAM nhiều chunk đúng nội dung, lỗi đọc giữa chừng, ghi/ghi đè/thiếu thư mục cha, `Reader` (open/read/seek/đóng tự động), khóa cân bằng và không lồng nhau, 1000 vòng không rò rỉ handle
- [x] Đã xác nhận trên board: khởi động in `[SD] 4 anh, 3 bai nhac` (nhạc đã đổi sang MP3), màn hình và phím vẫn bình thường

**Chat 1: Main Screen**
- [x] Trên board: điều hướng 4/6/2/8 (kể cả widget trái), phím 5 mở app, nhớ vị trí focus
- [x] Trên board: 6 icon trắng hiện đúng (5 app + Settings). Phát hiện: hai dòng tô màu icon bằng `Themes::current().icon` làm icon chìm vào nền nên đã bỏ; icon luôn trắng
- [x] Đồng hồ: chạy từ 1/1 00:00 khi chưa có RTC (giờ hệ thống), hành vi dự kiến cho tới khi nối `TimeService`
- [x] Đổi theme: bộ icon, màu nút, viền focus, khung đổi đúng ở cả 3 theme (xem bằng mắt, chat 9)
- [x] 100 lần vào/ra app và đổi theme không rò rỉ: `leak_test` 300 vòng, không rò rỉ (mục 12.1)
- Test máy tính cho Main Screen: `tools/app_test` (mục 12.1)

**Chat 2: Time service**
- [x] Test máy tính cho `time_service` (`tools/time_test`, stub DS3231/đồng hồ/WiFi/SNTP giả): 160661 kiểm tra đạt với ASan + UBSan, đã chạy lại trên máy người dùng. Gồm: BCD, thứ trong tuần so với libc cho cả 2000-2099, chế độ 12 giờ, `begin()` (không chip, chip tốt, OSF, giờ hỏng), cache 200 ms (kể cả `millis()` quay vòng), lỗi I2C và tự phục hồi, `set()` (thanh ghi đúng, giá trị sai bị bỏ qua, ghi lỗi), `syncNtp` (chưa WiFi, thành công, timeout, cờ cũ, khôi phục từ OSF), 4 luồng đọc + 1 luồng ghi không có lần đọc rách. Chèn thử 5 lỗi cố ý vào bản sao thì cả 5 đều bị test bắt
- [x] ThreadSanitizer chạy sạch trong môi trường của chat Time service. Trên máy người dùng TSan không chạy được (`unexpected memory mapping`, do ASLR của kernel mới, không phải lỗi code; cách xử lý: `setarch $(uname -m) -R <file>` hoặc `sudo sysctl vm.mmap_rnd_bits=28`), người dùng bỏ qua bước này
- [x] Trên board (`tools/rtc_test`): đọc/ghi giờ, các lệnh serial và đặt giờ từ laptop chạy đúng. Rút nguồn khoảng 1 giờ rồi cắm lại, giờ vẫn đúng (pin giữ giờ)
- [x] Trên board (firmware chính): Main Screen hiện đúng giờ thật từ `TimeService::now()`
- Bài học: lúc đầu giờ lệch 7 tiếng vì WSL để múi giờ UTC và `sync_time.py` gửi giờ theo múi giờ của máy chạy; đã sửa script luôn gửi UTC+7
- Bài học: test máy tính của sketch `rtc_test` bắt được lỗi trong chính sketch (mốc đo trôi `[drift]` lấy sai lúc RTC nhảy giờ); đã sửa

**Chưa thử trên board:** NTP (`syncNtp`), kịch bản mất pin (cờ OSF) với firmware chính. (Rút dây I2C đã thử ở chat 9: mục 11.)

**Chat 4: Slideshow widget**
- [x] Build được trên board (sau khi sửa lỗi dán nhầm nội dung giữa slideshow_widget.cpp và slideshow_service.cpp)
- [x]: Trên board: ảnh hiện đúng, phủ kín khung, cắt giữa không méo.
- [x] Tắt/bật nguồn vẫn giữ ảnh đã chọn (/data/slideshow.txt)
- [x] Thư mục /data không có: setPhoto trả false, app không treo
- [x] Test máy tính cho SlideshowService: `tools/slideshow_test` (mục 12.1)

**Chat 3: Image service + Album**
- [x] Test máy tính cho `image_resize` (ASan + UBSan, 32 kiểm tra đạt)
- [x] Trên board: giải mã JPG, lưới thumbnail, xem toàn màn hình chạy ổn
- [x] Phím 3 đặt ảnh widget: build được
- [x] Trên board: xem ảnh bấm 3 thấy thông báo, về Main thấy đúng ảnh
- [x] Trên board: ở lưới bấm 3 trên ô khác, ảnh widget đổi theo
- [x] Trên board: thoát Album ngay khi thông báo còn hiện, không crash
- [x] Rút thẻ SD rồi bấm 3: hiện "Khong luu duoc"

**Chat 5: Audio service + Music Player**

- [x] I2S: bíp 440 Hz ra loa (cần nối chân SD của MAX98357A vào 3V3, để hở thì không có tiếng)
- [x] Phát MP3 từ SD, pause/resume, tua, tự chuyển bài khi app mở, rời app thì bài chạy hết rồi dừng
- [x] Loa rè ở âm lượng cao: do quá tải; đã giới hạn mức hiển thị 100 = mức nội bộ 80
- [x] Ảnh đĩa xoay: bản xoay trực tiếp bằng LVGL hơi nặng; đã chuyển sang dựng sẵn khung xoay
- [x] Test máy tính cho `audio_service`: `tools/audio_test` (mục 12.1)

**Chat 6: Visualizer**
- [x] Test máy tính cho fft (ASan + UBSan, 2695 kiểm tra đạt, chạy lại trên máy người dùng)
- [x] Test máy tính cho VisualizerApp bằng stub LVGL (1142 kiểm tra đạt ở 320x240 và 160x128, chạy lại trên máy người dùng): bố cục 3 kiểu, 7/9 qua widget gác, 4/6, ESC, callback sau onDestroy, 300 lần vào/ra không rò rỉ object, 4000 phím ngẫu nhiên. Không kiểm tra được vẽ thật và tốc độ
- [x] Trên board: build và chạy ổn
- [x] 100-300 lần vào/ra app không rò rỉ: `leak_test` (mục 12.1)
- [ ] Đo FPS thực tế từng kiểu (đặc biệt waveform), chỉnh kFrameMs/kDbFloor/kDbRange/kTiltDb nếu cần (xem bằng mắt thì mượt, chưa đo bằng số)

### 12.1 Chat 9: Tối ưu + kiểm thử

Phạm vi: Calendar không thuộc chat này. Không sửa `main.cpp` và không sửa code firmware; mọi sketch/test nằm trong `tools/`. Người dùng không thử kịch bản thẻ SD trống/thiếu thư mục (chỉ mình dùng, đã ghi chú thiết lập thẻ).

**A. Kiểm thử rò rỉ và độ ổn định trên board (`tools/leak_test`, env `leak_test`, giai đoạn 0-7): đạt.**
- Phương pháp: đo mốc ban đầu, vài vòng khởi động nguội, rồi đo độ trôi bộ nhớ (hồi quy bình phương tối thiểu), ngưỡng 512 B, theo dõi số task, phát hiện dứt tiếng bằng `positionMs()` so với đồng hồ thật (cửa sổ 500 ms, tỉ lệ < 0,85 là dứt).
- Các giai đoạn: vào/ra từng app nhiều vòng (0, 2-4), nhạc phát nền (5), rút/cắm thẻ SD giữa chừng (6), đổi ảnh widget khi vào Music (7). Tất cả đạt: không rò rỉ, không treo; thẻ SD tự đọc lại sau khi cắm lại; nhạc về `STOPPED` khi rút thẻ giữa lúc phát; nhạc nền không dứt tiếng khi chuyển app.
- Thử tay trên board: theme đúng ở cả 3 theme; Visualizer, đĩa xoay và mèo mượt; rút I2C thì giờ hiện `1/1 07:00`, cắm lại thì đúng giờ; rút thẻ SD thì Main hiện thông báo đặt sẵn, cắm lại thì widget hiện đúng ảnh đã chọn.

**B. Test máy tính (g++ + stub; ASan/UBSan, TSan chạy bằng `setarch $(uname -m) -R`; kiểm tra đột biến): đạt, đã chạy lại trên máy người dùng.**

| Bộ test | Đối tượng | Kết quả |
|---|---|---|
| `tools/audio_test` | `audio_service` | 4 cấu hình, 2640 kiểm tra, 0 hỏng |
| `tools/slideshow_test` | `slideshow_service` | 4 cấu hình, 123587 kiểm tra, 0 hỏng |
| `tools/app_test` (`sh run.sh quick`) | Main Screen, Album, Pet (LVGL 8.3.11 thật) | 4600 kiểm tra, 0 lỗi; đột biến: 27/30 bị bắt, 3 tương đương (M18, M20, M28) |

- `app_test`: mốc bộ nhớ ổn định qua 300 vòng (82345 B, 195 khối, 18 đối tượng, 0 task); chèn lỗi cấp phát PSRAM (chỉ qua `Psram::alloc`) ở các vị trí app0=23, app5=59, app1=21 đều không rò, không treo; fuzz phím theo mô hình tham chiếu từng bước (Main, Album, Pet) +0 B; thử `millis()` quay vòng cho Pet; có kiểm tra đua giữa `AppManager::tick` và `LvglPort::tick` khi ảnh widget đổi.
- Lỗi do test bắt trong chính test/stub (đã sửa): lỗi stub heap (`ESP.getFreeHeap`), cache ảnh widget 71 KB bị tính nhầm là rò, `run.sh` truyền đối số rỗng làm test bỏ qua toàn bộ.

**C. Đo hiệu năng trên board (`tools/perf_test`, env `perf_test`, thẻ SD có `/music/exile.mp3`, 9 ảnh trong `/images`, theme `mint`): đạt.** Ngày chạy 2026-11-18/19. `PERF_NO_TASKSTATE=1`: stack đọc theo tên task.

*C1. Thời gian chuyển app.* Đo hai kiểu. "AppManager" là số `N ms` mà `AppManager` tự in (dựng/hủy trong `tick()`): đây là số đo đáng tin của việc chuyển app. "Đầu-cuối" là từ lúc gọi `goTo/goHome` đến lúc `AppManager` báo xong, đo bằng `perf_test` (độ phân giải khoảng 5 ms), luôn cao hơn khoảng 60-90 ms (chưa rõ nguyên nhân, mục 11). Mỗi app 8 lần vào/ra (2 lần đầu là "nguội", không tính vào trung bình).

| App | Vào, AppManager (ms) | Ra Main, AppManager (ms) | Vào, đầu-cuối (ms, không nhạc / có nhạc) | Ra, đầu-cuối (ms) |
|---|---|---|---|---|
| Visualizer | 18-19 | 17 | 78 / 87 | 90 / 95-151 |
| Settings | 17-19 | 16-18 | 85 / 88 | 89 / 93 |
| Album | 50-54 | 18-63 (đôi khi 120-270) | 141 / 148 | 134 / 124 |
| Music | 62-72 | 16-18 | 136 / 144 | 89 / 94 |
| Pet | 164-183 | 17-19 | 252 / 270 | 35 / 60 |

- Không có lần chuyển nào thất bại (`fail=0`, 8 lần mỗi app, cả hai chế độ). Lần "nguội" gần như không chậm hơn (trừ Music lần đầu, hơn khoảng 17 ms).
- Số lần vào/ra thu thập từ log chạy dài: Pet 59 lần trung bình 173 ms (171-222); Music 57 lần trung bình 66 ms (63-78); Album 63 lần trung bình 53 ms (52-59); Settings 62 lần 19 ms; Visualizer 53 lần 19 ms.
- `AppManager::tick()` chặn `loop()` lâu nhất: Pet vào khoảng 250-270 ms (tính cả đầu-cuối), Album ra tới khoảng 130-270 ms. Khoảng cách tối đa giữa hai vòng `loop()` đo được: 275 ms (hai phần đo ngắn), 343 ms (30 phút chạy dài).

*C2. Bộ nhớ mỗi app khi đang mở* (so với đang ở Main; PSRAM và heap nội bộ). Số liệu từ `perf_test` (byte) và đối chiếu với dòng `[AppManager]` (KB).

| App | PSRAM chiếm (B) | Heap nội bộ chiếm (B) | Task thêm | Sau khi thoát (hụt tối đa, B) |
|---|---|---|---|---|
| Pet | 464,816 | 500 | 0 | -24 |
| Album | 58,028 | 10,392 | +1 (`album`, tạm) | -60 |
| Visualizer | 7,380 | 2,948 | 0 | -96 |
| Music (khi đã có cache đĩa) | 2,096 | 968 | 0 | -24 |
| Settings | ~0 | 108 | 0 | -72 |

- Mọi app quay về mốc cũ trong sai số, tối đa -96 B (dưới ngưỡng 512 B). Lần vào Music đầu tiên: PSRAM trống giảm 1,375,804 B và heap giảm khoảng 47 KB, rồi giữ nguyên (cache 45 khung đĩa ~1,2 MB + ring PCM 128 KB + hai task audio; mục 7). Đây là chỗ duy nhất bộ nhớ "không về mốc cũ" và là chủ đích.
- Mốc: lúc khởi động (trước khi vào Music) heap trống khoảng 306-314 KB, PSRAM trống khoảng 8,247,000 B; sau khi Music đã dựng cache: heap khoảng 261-267 KB, PSRAM khoảng 6,871,000 B (khối liền lớn nhất 6,815,732 B, không phân mảnh), heap nội bộ khối liền lớn nhất 237,556 B.
- Đỉnh dùng bộ nhớ từ lúc khởi động (nhạc đang phát, đã vào mọi app): PSRAM trống thấp nhất 6,399,863 B (tức dùng tối đa khoảng 1,85 MB trong 8 MB), heap nội bộ trống thấp nhất khoảng 247 KB (dùng tối đa khoảng 67 KB). Còn rất nhiều dư địa.
- Số task: 7 lúc khởi động; 9 sau khi dùng Music (thêm `audio_dec`, `audio_out`); Album thêm 1 task tạm khi mở.

*C3. Stack còn trống tối thiểu từng task (byte; càng nhỏ càng sát).*

| Task | Còn trống tối thiểu (B) | Ghi chú |
|---|---|---|
| `loopTask` | 5068 | stack mặc định Arduino 8 KB, đã dùng khoảng 3,1 KB |
| `lvgl` | 8980 | stack 12 KB, đã dùng khoảng 3,3 KB |
| `audio_dec` | 15060 (31844 lúc chưa phát) | stack 32 KB, minimp3 dùng khoảng 17 KB |
| `audio_out` | 3232 | |
| `album` | 5468 | task tạm, đo lúc đang mở |
| `esp_timer` | 3872 | task hệ thống |
| `ipc0` / `ipc1` | 324 / 208 | task hệ thống ESP-IDF, nhỏ nhưng ổn định; không thuộc code của dự án |

Các task tạm `disc_cover` và `slideshow` chưa được đo (chỉ tồn tại ngắn, không bắt được lúc đang sống). Chưa thấy task nào sát giới hạn; chưa có dấu hiệu tràn stack.

*C4. Chạy dài (soak) 30 phút có nhạc phát lặp, 464 thao tác (vào app 268, thoát nhanh 60, đổi theme 76, đứng yên 60), 30 mẫu mỗi phút ở Main: đạt cả 5 tiêu chí.*
- PSRAM trôi +56 B (2,1 B/phút), heap nội bộ -44 B (13,5 B/phút), khối PSRAM liền lớn nhất không đổi, số task không đổi (9).
- Âm thanh: 3517 cửa sổ kiểm tra, 0 lần dứt tiếng, tỉ lệ thấp nhất 0,99, phát lại 6 lần (hết bài tự phát lại), 0 lỗi `play`.
- Khoảng cách tối đa giữa hai vòng `loop()`: 343 ms (do thoát Album hoặc vào Pet); không gây dứt tiếng.
- Stack `loopTask` thấp nhất trong cả lần chạy: 5036 B.

**Phát hiện (đã ghi ở mục 11):** 3 lỗi nhỏ của `audio_service` (đã sửa); `isMounted()` không đáng tin; mỗi lần gọi SD khi không có thẻ chặn khoảng 300 ms; Main Screen hiện giờ giả `1/1 07:00` khi thiếu RTC; `PET_DECAY_FAST = 1`; Pet vào chậm và nặng PSRAM; `loop()` bị chặn tới khoảng 340 ms khi chuyển app.

**Giới hạn của phép đo:** không đo FPS và tải CPU; độ phân giải thời gian chuyển app khoảng 5 ms; Calendar chưa được đo; phần đo có nhạc dùng một bài MP3 (`/music/exile.mp3`); các phép đo "đầu-cuối" có độ lệch cố định chưa giải thích (mục 11).