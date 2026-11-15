#!/bin/sh
# Test may tinh cho services/audio_service.cpp (g++ + ASan + UBSan, phan cung bang stub).
# Dat thu muc nay thanh tools/audio_test/ roi chay:   sh run.sh [duong_dan_src]   (mac dinh ../../src)
# Chay 4 cau hinh (gnu++11/gnu++17 x minimp3 SIMD/NO_SIMD, NO_SIMD giong ESP32) x 5 kich ban.
# Tuy chon: TSAN=1 sh run.sh  -> them 1 lan chay ThreadSanitizer (can  setarch -R neu kernel moi bao "unexpected memory mapping").
# KHONG dat timeScale lon hon 8 (decode duoi ASan khong theo kip, phep do thoi gian sai).
SRC=${1:-../../src}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/audio_test_build}
mkdir -p "$OUT"
FLAGS="-g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -Wall -Wextra -Wno-unused-function -pthread"
INC="-I$HERE/stubs -I$SRC -I$SRC/lib/minimp3 -I$SRC/../lib/minimp3"
bad=0
for STD in gnu++11 gnu++17; do
  for SIMD in "" "-DMINIMP3_NO_SIMD"; do
    TAG="$STD $SIMD"
    echo "################ $TAG"
    g++ -std=$STD $SIMD $FLAGS $INC -o "$OUT/audio_test" "$HERE/test/audio_test.cpp" "$HERE/stubs/rtos_stub.cpp" "$HERE/stubs/sd_stub.cpp" || { echo "BIEN DICH LOI ($TAG)"; exit 1; }
    for SC in pre_begin begin_psram_fail begin_i2s_fail seek_extreme main; do
      "$OUT/audio_test" "$HERE/data" $SC > "$OUT/$SC.log" 2>&1
      rc=$?
      grep -E '^\[|PHAT HIEN|  FAIL|TREO|AddressSanitizer' "$OUT/$SC.log"
      ub=$(grep -c 'runtime error' "$OUT/$SC.log")
      # seek_extreme co chu y goi seek(INT_MAX): UBSan bao tran so da biet (phat hien F-seek-tran-so)
      if [ "$SC" = "seek_extreme" ]; then [ "$ub" -gt 0 ] && echo "  UBSan: $ub loi (da biet: seek(INT_MAX) tran so int)"; else [ "$ub" -gt 0 ] && { echo "  UBSan: $ub loi"; bad=1; }; fi
      [ $rc -ne 0 ] && bad=1
    done
  done
done
if [ -n "$TSAN" ]; then
  echo "################ ThreadSanitizer"
  g++ -std=gnu++11 -g -O1 -fsanitize=thread -pthread -Wno-unused-function $INC -o "$OUT/audio_test_tsan" "$HERE/test/audio_test.cpp" "$HERE/stubs/rtos_stub.cpp" "$HERE/stubs/sd_stub.cpp" && \
  setarch "$(uname -m)" -R "$OUT/audio_test_tsan" "$HERE/data" main > "$OUT/tsan.log" 2>&1; grep -c 'WARNING: ThreadSanitizer' "$OUT/tsan.log"
fi
[ $bad -eq 0 ] && echo "=== TAT CA DAT ===" || echo "=== CO LOI (xem log trong $OUT) ==="
exit $bad
