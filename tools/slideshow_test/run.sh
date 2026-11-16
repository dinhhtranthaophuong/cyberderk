#!/bin/sh
# Chay test slideshow_service tren may tinh: sh run.sh [tsan]
# Can g++ co ASan/UBSan. Dung ma that trong ../../services neu co, neu khong thi ./src.
cd "$(dirname "$0")" || exit 1
if [ -f ../../services/slideshow_service.cpp ]; then SRC=../..; else SRC=./src; fi
OUT=${OUT:-/tmp/slideshow_test_build}; mkdir -p "$OUT"
FILES="test/slideshow_test.cpp $SRC/services/image_service.cpp $SRC/services/image_resize.cpp"
INC="-Istubs -I$SRC"
WARN="-Wall -Wextra -Wno-unused-parameter"
run() { # ten, std, flags
  echo "--- $1"
  g++ -std=$2 -g -O1 -fno-omit-frame-pointer $WARN $INC $3 $FILES -o "$OUT/t_$1" -pthread || { echo "BIEN DICH LOI ($1)"; FAIL=1; return; }
  if [ "$4" = "tsan" ]; then setarch -R "$OUT/t_$1" --stress-ms 2000 || FAIL=1; else "$OUT/t_$1" || FAIL=1; fi
}
FAIL=0
if [ "$1" = "tsan" ]; then
  run tsan17 gnu++17 "-fsanitize=thread" tsan
else
  run asan11 gnu++11 "-fsanitize=address,undefined"
  run asan17 gnu++17 "-fsanitize=address,undefined"
  run asan20 gnu++20 "-fsanitize=address,undefined"
  run tsan17 gnu++17 "-fsanitize=thread" tsan
fi
[ $FAIL = 0 ] && echo "== TAT CA CAU HINH DAT ==" || echo "== CO CAU HINH LOI =="
exit $FAIL
