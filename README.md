# Cyberdeck: Chat 0, bước 4 (bàn phím 3x3 thật)

Bước 1 đến 3 đã chạy trên board. Bước 4 thêm bàn phím ma trận 3x3 thật, dùng ngắt để đánh thức và quét khi có phím, nên lúc rảnh gần như không tốn CPU. Serial Monitor vẫn dùng được song song.

## Đấu dây

Xem `docs/keypad_wiring.png` (hoặc `.svg`):

```
Hàng:  R1 = GPIO 38   R2 = GPIO 39   R3 = GPIO 40
Cột:   C1 = GPIO 41   C2 = GPIO 42   C3 = GPIO 21
Nút:   1 2 3 / 4 5 6 / 7 8 9  (nút nằm ở giao điểm hàng-cột)
```

- Mỗi nút chỉ có 2 đầu: một đầu vào dây hàng, một đầu vào dây cột. Không cần GND, 3V3, điện trở ngoài, diode.
- Nút bấm 4 chân: dùng 2 chân chéo nhau.
- Phím 1 nhấn nhanh = ESC (gửi khi thả), giữ 0,8 s = HOME. Phím 3 = OPTION. Nhấn 2 phím cùng lúc bị bỏ qua.

## Cách thử bàn phím

Nạp code như bên dưới, rồi bấm từng nút theo bảng "Cách thử" (kết quả giống gõ phím tương ứng trên Serial). Nếu một nút không ăn hoặc ra nhầm phím, thêm `-DKEYPAD_DEBUG=1` vào `build_flags` để serial in `[Keypad] key 0x....` mỗi lần có phím, rồi báo mình kèm số nút bạn bấm và mã in ra. Sai thường gặp: đấu nhầm hai chân cùng cạnh của nút 4 chân (nút luôn nối hoặc không bao giờ nối).

File mới: `services/keypad_matrix.hpp/.cpp`, `docs/keypad_wiring.*`. File sửa: `config/keymap.hpp`, `services/input_service.hpp/.cpp`. Còn lại không đổi (`platformio.ini` giữ nguyên của bạn).

---

Phần dưới là hướng dẫn chung của bước 3 (cách chép code, bảng thử, xử lý lỗi), vẫn đúng cho bước này.

## Cách nhập code

1. **Đóng Serial Monitor** nếu đang mở (Ctrl+C trong terminal monitor).
2. **Giải nén** file zip ra một thư mục tạm. Bên trong có thư mục `cyberdeck/`.
3. Trong **project của bạn**, **xóa thư mục `src/` cũ**, rồi chép thư mục `src/` mới từ zip vào. Làm vậy sạch hơn chép đè, vì có file mới và file đã đổi, không để sót file cũ.
4. **Giữ nguyên `platformio.ini` của bạn.** Lần này file đó không đổi so với bước 2, nên nếu bạn đã chỉnh gì (tab màu, `TFT_RGB_ORDER`...) thì vẫn còn nguyên. Chỉ thay `README.md` nếu muốn.
5. Nạp code rồi mở monitor (đổi COM4 cho đúng cổng của bạn):

```
pio run -t upload
pio device monitor --port COM4
```

Nếu build báo lỗi lạ, thử `pio run -t clean` rồi nạp lại.

### File thay đổi so với bước 2

| Loại | File |
|---|---|
| **Mới** | `core/app_base.hpp`, `core/app_factories.hpp`, `core/app_manager.hpp/.cpp`, `core/app_registry.hpp/.cpp`, `apps/main_screen/main_screen.hpp/.cpp` |
| **Sửa** | `main.cpp`, `core/lvgl_port.hpp/.cpp` (task LVGL riêng), `services/input_service.hpp/.cpp` (không còn `poll()`), `config/display.hpp` (lưới 2 cột x 3 hàng), `lv_conf.h` (heap LVGL trong PSRAM) |
| **Không đổi** | `platformio.ini`, `config/pins.hpp`, `config/keymap.hpp`, `core/psram.*` |

## Cách thử

Gõ trực tiếp vào cửa sổ monitor, không cần Enter:

```
Phím 4/6: focus đi vòng widget trái → cột 1 → cột 2 → widget trái. Từ widget trái, phím 6 vào cột đầu của lưới ở hàng đã nhớ. Phím 4 vào cột cuối.
Phím 2/8: chỉ đổi hàng trong lưới, quay vòng như trước.
Phím 5 trên widget trái: gọi AppManager::goTo(AppId::APP_ALBUM). Widget có viền trắng khi được focus, không glow.
Phím 7/9: LVGL tự dời focus theo thứ tự tạo, widget trái đứng đầu nên đi từ trái sang phải.
Phím 1 (ESC): Main là màn gốc nên không làm gì. KEY_OPTION không xử lý nên trả false.
Đồng hồ: dùng time() của hệ thống, kiểm tra mỗi giây và chỉ sửa label khi phút đổi. Màn 160x128 hiện hai dòng (Oct 27 rồi 23:23), màn lớn hiện một dòng Oct 27, 23:23.
Vòng đời: onDestroy() đặt mọi con trỏ về nullptr, callback kiểm tra trước khi dùng. Widget trái được thêm vào group thủ công, vì container lv_obj không tự vào group.
 1 = BACK(ESC)   2 = UP      3 = OPTION
 4 = LEFT        5 = ENTER   6 = RIGHT
 7 = PREV        8 = DOWN    9 = NEXT
 h = HOME (giả lập giữ phím 1)
```

| Gõ | Kỳ vọng |
|---|---|
| `6` / `4` / `8` / `2` | focus di chuyển theo lưới 2 cột x 3 hàng, quay vòng ở mép |
| `7` / `9` | focus lùi / tiến tuần tự |
| `5` trên nút Music, Pet, Cal, Viz, Album | vào app: màn hình hiện `<tên>` và `(chua cai dat)`, serial in một dòng `[AppManager] -> ...` |
| `1` trong app | về Main Screen, focus nằm đúng nút vừa mở |
| `h` trong app | về Main Screen từ bất kỳ app nào |
| `3` | không có phản ứng (app stub bỏ qua) |
| `5` ở ô cuối cùng (ô trống, không nhãn) | không làm gì |

Thứ tự các nút: hàng 1 Music, Pet; hàng 2 Cal, Viz; hàng 3 Album, (trống).

### Thử rò rỉ bộ nhớ

Vào rồi thoát app khoảng 20 lần (`5`, `1`, `5`, `1`...). Quan sát số `psram` trong dòng `[AppManager] -> ...` và dòng heartbeat mỗi 5 giây. Số này phải **ổn định**, không giảm dần.

## Kết quả mong đợi trên serial

```
=== Cyberdeck chat 0 / buoc 3: AppManager ===
Flash: 16384 KB, PSRAM free: ~8190 KB
[AppManager] -> Main (.. ms) | heap .. KB, psram .. KB
San sang. Go trong Serial Monitor: ...
```

## Đã kiểm tra những gì

Bước 4 (bàn phím) chưa chạy trên board. Đã chạy trên máy tính (bản test bước 3): biên dịch LVGL 8.3.11 thật cùng code dự án, thay phần cứng bằng stub, bật AddressSanitizer, rồi mô phỏng gõ phím. 38 kiểm tra đạt: điều hướng quay vòng, mở từng app, ESC/HOME/OPTION, nhớ vị trí focus, ô trống, và 300 lần vào/ra app không tăng bộ nhớ. Lần chạy đầu bắt được một lỗi dùng bộ nhớ đã giải phóng khi chuyển app, đã sửa trong bản này.

Phần chỉ board mới thử được: task LVGL chạy riêng trên Core 1 và heap LVGL nằm trong PSRAM (cả hai đều mới ở bước này).

## Nếu có lỗi

| Hiện tượng | Cách xử lý |
|---|---|
| Board reset liên tục, log dừng sau `Flash: ...` | Chụp log gửi mình. Nghi ngờ task LVGL hoặc heap PSRAM |
| Màn hình hiện rồi đứng hình | Chụp log gửi mình, kèm vừa gõ phím gì |
| `LvglPort::init() THAT BAI` | Báo mình, kèm dòng PSRAM free |
| Gõ phím không phản ứng | Kiểm tra cửa sổ monitor đang focus; thử `5` |
| Số `psram` giảm dần khi vào/ra app | Báo mình, kèm vài dòng log |
| Lỗi build | Dán nguyên văn lỗi |

## Báo lại cho mình

Dán log từ dòng `=== Cyberdeck ...` kèm vài dòng `[AppManager]`, rồi cho biết từng dòng trong bảng "Cách thử" có đúng không.

## Cấu trúc hiện tại

```
platformio.ini            // ghim espressif32@6.5.0, TFT_eSPI@2.5.43, lvgl@8.3.11
src/
  main.cpp                // setup + loop (loop chỉ gọi AppManager::tick)
  lv_conf.h               // LVGL: RGB565, heap trong PSRAM, tick = millis()
  config/                 // pins, display, keymap
  core/
    psram.*               // cấp phát PSRAM
    lvgl_port.*           // TFT <-> LVGL, task LVGL (Core 1), LvglLock
    app_base.hpp          // IApp + AppId
    app_factories.hpp     // createXxxApp() kiểu weak
    app_manager.*         // chuyển app, hàng đợi phím custom
    app_registry.*        // tên, icon, thứ tự ô trong lưới launcher
  services/
    input_service.*       // phím từ serial -> lv_indev / AppManager::postKey
  apps/
    main_screen/          // launcher tạm
```

Muốn thêm một app thật, app đó chỉ cần định nghĩa hàm `IApp* createXxxApp()` (xem `core/app_factories.hpp`). `AppManager` tự dùng hàm đó thay cho màn hình thay thế.
