#!/usr/bin/env python3
"""Đặt giờ DS3231 trên board từ đồng hồ laptop, qua lệnh `s` của sketch rtc_test.

Chạy:
  python3 tools/rtc_test/sync_time.py /dev/ttyACM0        # Linux/WSL
  python  tools/rtc_test/sync_time.py COM5                # Windows

Cần pyserial. PlatformIO đã kèm sẵn pyserial trong python riêng của nó:
  ~/.platformio/penv/bin/python tools/rtc_test/sync_time.py /dev/ttyACM0
Hoặc: python3 -m pip install pyserial

Chỉ một chương trình được mở cổng serial một lúc: tắt `pio device monitor` trước khi chạy.
Script gửi lệnh đúng lúc chuyển giây của laptop nên sai số chỉ vài chục ms. Giờ gửi là GIỜ ĐỊA PHƯƠNG
của laptop (TimeService lưu giờ địa phương), hãy chắc laptop đang đúng múi giờ ICT.
"""
import math
import sys
import time
from datetime import datetime

try:
    import serial
except ImportError:
    sys.exit("Thieu pyserial. Chay: python3 -m pip install pyserial  (hoac dung python cua PlatformIO)")

ATTEMPTS = 3
REPLY_WAIT_S = 2.0


def open_port(name: str) -> "serial.Serial":
    ser = serial.Serial()
    ser.port = name
    ser.baudrate = 115200
    ser.timeout = 0.2
    ser.dtr = False   # tránh reset board khi mở cổng (với USB native của ESP32-S3)
    ser.rts = False
    ser.open()
    return ser


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    try:
        ser = open_port(sys.argv[1])
    except (serial.SerialException, OSError) as e:
        print(f"Khong mo duoc {sys.argv[1]}: {e}")
        return 1

    with ser:
        time.sleep(0.5)
        ser.reset_input_buffer()
        for attempt in range(1, ATTEMPTS + 1):
            now = time.time()
            target = math.floor(now) + 1            # đợi tới đầu giây kế tiếp rồi gửi giờ của giây đó
            time.sleep(max(0.0, target - now))
            line = datetime.fromtimestamp(target).strftime("s %Y-%m-%d %H:%M:%S")
            ser.write((line + "\n").encode())
            ser.flush()

            deadline = time.time() + REPLY_WAIT_S
            ok = False
            while time.time() < deadline:
                raw = ser.readline().decode(errors="replace").strip()
                if not raw:
                    continue
                if raw.startswith(("OK set", "LOI", "thanh ghi", "gio trong chip", "control", "=>")):
                    print(raw)
                if raw.startswith("OK set"):
                    ok = True
                elif raw.startswith("LOI"):
                    return 1
                elif ok and raw.startswith("=>"):
                    return 0
            if ok:
                return 0
            print(f"Chua thay phan hoi (lan {attempt}/{ATTEMPTS}), thu lai...")
    print("Khong nhan duoc phan hoi tu board. Kiem tra: da nap rtc_test chua, dung cong chua, "
          "da tat pio device monitor chua.")
    return 1


if __name__ == "__main__":
    sys.exit(main())