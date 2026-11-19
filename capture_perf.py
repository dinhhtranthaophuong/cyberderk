import serial
import time
import sys

port = 'COM4'
baud = 115200

ser = None
for i in range(20):
    try:
        ser = serial.Serial(port, baud, timeout=1)
        break
    except Exception as e:
        time.sleep(0.5)

if not ser:
    print('Failed to open COM4')
    sys.exit(1)

print('Connected to COM4. Capturing to perf.log...')
with open('perf.log', 'w', encoding='utf-8') as f:
    t0 = time.time()
    while time.time() - t0 < 660:  # Up to 11 minutes
        line = ser.readline()
        if line:
            text = line.decode('utf-8', errors='replace')
            f.write(text)
            f.flush()
            if text.startswith('PERF'):
                print(text, end='', flush=True)
            if 'PERF,XONG' in text:
                print('COMPLETED_PERF_TEST', flush=True)
                break

ser.close()
print('Done.')
