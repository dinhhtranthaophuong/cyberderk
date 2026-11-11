# Cyberdeck ESP32-S3

Thiết bị **Cyberdeck cá nhân** nhỏ gọn chạy trên vi điều khiển **ESP32-S3**, gồm màn hình TFT màu, bàn phím cơ **3x3** để điều hướng và thẻ nhớ microSD chứa ảnh, nhạc. Giao diện viết bằng **LVGL**, firmware phát triển bằng **VS Code + PlatformIO**.

## Tính năng

Giao diện gồm một màn hình chính và các ứng dụng:

| Ứng dụng | Mô tả | Trạng thái |
|---|---|---|
| **Main Screen** | Nửa trái: khung ảnh (chọn từ Album) kèm đồng hồ, ngày tháng. Nửa phải: lưới icon 2x3 gồm các app người dùng chọn và ô Settings | Xong |
| **Pet** | Thú ảo (mèo pixel): cho ăn, chơi, tắm, ngủ, đổi màu lông; có chu kỳ ngày/đêm, hoạt ảnh chớp mắt, vẫy đuôi | Xong |
| **Music Player** | Phát MP3 từ thẻ SD qua I2S ra loa; đĩa quay có ảnh, playlist, tua, chỉnh âm lượng | Xong |
| **Visualizer** | Hiển thị phổ/sóng âm thời gian thực bằng FFT 256 điểm, 3 kiểu hiệu ứng | Xong |
| **Album** | Lưới thumbnail ảnh JPG từ thẻ SD, xem toàn màn hình, đặt ảnh làm ảnh widget Main Screen | Xong |
| **Settings** | Chọn theme giao diện, chọn app hiển thị trên Main Screen (lưu vào flash NVS) | Xong |
| **Calendar** | Lịch tháng dạng lưới, đánh dấu sự kiện | Chưa viết |

Ngoài ra có **3 theme** (`pink` mặc định, `night`, `mint`), mỗi theme có bộ icon pixel art riêng (64x64).

## Phần cứng

| Hạng mục | Linh kiện |
|---|---|
| MCU | ESP32-S3 DevKitC-1 **N16R8** (16 MB flash, 8 MB PSRAM octal) |
| Màn hình | ST7789 2.8" 240x320 (xoay ngang 320x240), không cảm ứng. Dự phòng: ST7735S 1.8" |
| Lưu trữ | Module microSD (SPI, bus riêng), thẻ FAT32 |
| Âm thanh | MAX98357A (I2S) + loa 4 Ω 3 W, mono |
| Bàn phím | 9 công tắc cơ ma trận 3x3, không cần diode |
| RTC | DS3231 (I2C), đã có phần cứng, chưa tích hợp phần mềm |

### Sơ đồ chân

Nguồn sự thật duy nhất là `src/config/pins.hpp`.

| Thiết bị | Tín hiệu | GPIO |
|---|---|---|
| TFT (SPI2/FSPI) | SCLK / MOSI / CS | 12 / 11 / 10 |
| | DC / RST / BL | 9 / 8 / 7 |
| microSD (SPI3/HSPI) | SCK / MOSI / MISO / CS | 15 / 16 / 17 / 18 |
| I2S MAX98357A | BCLK / LRC / DIN | 4 / 5 / 6 |
| Keypad hàng (output) | R1 / R2 / R3 | 38 / 39 / 40 |
| Keypad cột (input pull-up) | C1 / C2 / C3 | 41 / 42 / 21 |
| I2C (DS3231) | SDA / SCL | 1 / 2 |

Lưu ý khi đấu dây:
- Chân **SD của MAX98357A phải nối 3V3**, để hở thì amp tắt và loa im.
- Chân LED (đèn nền) của màn ST7789 nối thẳng 3V3.
- Các chân không được dùng trên N16R8: GPIO 26-37, 0/3/45/46, 19/20, 43/44.
- Sơ đồ nối dây nằm trong thư mục `docs/` (bàn phím, màn ST7789, module SD).

## Kiến trúc phần mềm

- **Framework:** Arduino core cho ESP32 (PlatformIO), `espressif32@6.5.0`
- **Đồ họa:** LVGL 8.3.11 + TFT_eSPI 2.5.43, giải mã JPG bằng TJpg_Decoder, giải mã MP3 bằng minimp3
- **Đa luồng (FreeRTOS):** Core 1 chạy giao diện LVGL và xử lý phím; Core 0 chạy đọc SD, giải mã ảnh/âm thanh và tác vụ nền
- **Mẫu thiết kế:** lập trình hướng đối tượng theo module, điều phối bằng máy trạng thái hữu hạn (`AppManager`), mỗi app là một `IApp`
- **Bộ nhớ:** mọi buffer lớn (ảnh, audio, heap LVGL) cấp phát trong PSRAM

> Phiên bản thư viện được **ghim cố ý**: TFT_eSPI lỗi `tft.init()` trên ESP32-S3 với Arduino core mới (Bodmer/TFT_eSPI #3346). Không nâng cấp khi chưa kiểm tra lại.

### Cấu trúc thư mục

```
platformio.ini
docs/           sơ đồ nối dây
tools/          sketch/test phần cứng và test trên máy tính (sd, i2s, fft, image, visualizer)
src/
  main.cpp
  lv_conf.h
  config/       pins, display, keymap
  core/         AppManager, AppRegistry, Theme, LVGL port, PSRAM
  assets/icons/ bộ icon pixel art theo theme
  services/     input, keypad, settings_store, sd_storage,
                image, audio, slideshow, time
  apps/         main_screen, settings, album, music,
                visualizer, calendar, pet
  lib/minimp3/
```

Nguyên tắc chính: app **chỉ gọi service**, không tự đụng SPI/SD/I2S/I2C; không dùng `delay()`; màu giao diện chỉ lấy từ `Themes::current()`; mỗi module tách riêng `.hpp` và `.cpp`.

Thêm app mới chỉ cần 3 việc: thêm giá trị vào `AppId`, khai báo factory trong `app_factories.hpp`, thêm một dòng vào bảng `kInfos` trong `app_registry.cpp`.

## Điều khiển

```
 ┌───┬───┬───┐
 │ 1 │ 2 │ 3 │    1 = Back      2 = Lên     3 = Option
 ├───┼───┼───┤    4 = Trái      5 = Enter   6 = Phải
 │ 4 │ 5 │ 6 │    7 = Prev      8 = Xuống   9 = Next
 ├───┼───┼───┤
 │ 7 │ 8 │ 9 │    Giữ phím 1 (>= 800 ms) = Home (về Main Screen)
 └───┴───┴───┘
```

Ngoài bàn phím thật, có thể gõ `1`..`9` (và `h` để giả lập giữ phím 1) trong Serial Monitor để thử khi chưa đấu phím.

## Chuẩn bị thẻ SD

Format thẻ **FAT32** bằng *SD Card Formatter* (format nhanh của Windows có thể gây lỗi `no valid FAT volume`). Cấu trúc thư mục:

```
/images/   ảnh JPG baseline (.jpg / .jpeg), không progressive
/music/    nhạc MP3 (khuyến nghị 128 kbps, tên không dấu, không khoảng trắng)
/data/     dữ liệu ứng dụng (slideshow.txt do firmware tự tạo)
```

Hiện chỉ hỗ trợ **MP3**, chưa hỗ trợ WAV/AAC/M4A.

## Build và nạp firmware

Yêu cầu: VS Code + extension PlatformIO.

```bash
# Màn ST7789 2.8" (mặc định)
pio run -e cyberdeck_st7789 -t upload

# Màn ST7735S 1.8" (dự phòng)
pio run -e cyberdeck_st7735 -t upload

# Xem log (qua cổng USB native)
pio device monitor
```

Một số lệnh hữu ích:
- `pio run -t erase`: xóa toàn bộ flash, gồm cài đặt theme/app đã lưu
- `-DKEYPAD_DEBUG=1` trong `build_flags`: in mã phím thật ra serial
- Env test phần cứng riêng: `sd_test` (thẻ SD), `i2s_test` (bíp 440 Hz)

Nếu màn hình sai màu hoặc nhiễu khi dùng module khác, xem mục 3.1 trong `ARCHITECTURE.md` (cách dò `invertDisplay`, `TFT_RGB_ORDER`, driver, tần số SPI).

## Tình trạng dự án

- Đã chạy trên board: core + hạ tầng, Main Screen, Settings, Album, Slideshow widget, Music Player, Visualizer, Pet
- Chưa làm: Calendar, tích hợp DS3231 (`TimeService`), lưu trạng thái Pet vào SD
- Việc còn lại: đo FPS Visualizer, kiểm tra rò rỉ bộ nhớ qua nhiều lần vào/ra app, tối ưu và kiểm thử tổng, nguồn pin và mạch sạc

## Tài liệu

- [`concept.md`](concept.md): ý tưởng và bố cục giao diện từng màn hình
- [`instruction.md`](instruction.md): bối cảnh và quy tắc lập trình của dự án
- [`ARCHITECTURE.md`](ARCHITECTURE.md): hợp đồng kiến trúc, API các service, quy ước phím, nhật ký kiểm thử

## Giấy phép

Chưa chọn giấy phép.