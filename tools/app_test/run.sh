#!/bin/bash
# Chay test cap app (Main Screen, Album, Pet + AppManager) tren may tinh.
#   sh run.sh          ASan+UBSan (gnu++11/17/20) + TSan
#   sh run.sh quick    chi ASan+UBSan gnu++17 (~2 phut)
#   sh run.sh mutants  kiem thu dot bien (30 loi chen co y), ~40 phut
#   sh run.sh <ten>    chi mot nhom test, vd: leaks, album_nav, pet_actions, fuzz
# Bien moi truong: SRCDIR=../../src de test MA THAT cua du an (mac dinh: ban sao trong ./src),
#                  LVGL_DIR=<duong dan toi thu muc lvgl 8.3.x>, OUT=<thu muc build>.
cd "$(dirname "$0")" || exit 1
export OUT=${OUT:-/tmp/app_test_build}; mkdir -p "$OUT"
if [ -z "$LVGL_DIR" ]; then
  for d in ../../.pio/libdeps/*/lvgl ./lvgl; do [ -f "$d/lvgl.h" ] && LVGL_DIR=$d && break; done
fi
if [ -z "$LVGL_DIR" ]; then
  echo "Chua co LVGL: tai ban 8.3.11 ve ./lvgl (can git + mang)"
  git clone --depth 1 --branch v8.3.11 https://github.com/lvgl/lvgl.git lvgl || exit 1
  LVGL_DIR=./lvgl
fi
export LVGL_DIR
FAIL=0
build_run() { # ten std flags [tsan]
  echo "--- $1"
  STD=$2 ./build.sh "$1" "$3" 2>&1 | grep -E "error|undefined reference" && { FAIL=1; return; }
  if [ "$4" = tsan ]; then setarch "$(uname -m)" -R "$OUT/$1" || FAIL=1; else "$OUT/$1" ${ONLY:+"$ONLY"} || FAIL=1; fi
}
case "$1" in
  mutants) exec python3 mutants.py ;;
  quick) build_run asan17 gnu++17 "-fsanitize=address,undefined" ;;
  "")
    build_run asan11 gnu++11 "-fsanitize=address,undefined"
    build_run asan17 gnu++17 "-fsanitize=address,undefined"
    build_run asan20 gnu++20 "-fsanitize=address,undefined"
    build_run tsan17 gnu++17 "-fsanitize=thread" tsan ;;
  *) ONLY=$1; export ONLY; build_run asan17 gnu++17 "-fsanitize=address,undefined" ;;
esac
[ $FAIL = 0 ] && echo "== TAT CA CAU HINH DAT ==" || echo "== CO CAU HINH LOI =="
exit $FAIL
