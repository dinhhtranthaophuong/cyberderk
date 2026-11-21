#!/bin/bash
# build.sh <ten> <san-flags> [extra g++ flags]; LVGL bien dich 1 lan vao $OUT/lvgl_<san>
set -e
cd "$(dirname "$0")"
NAME=$1; SAN=$2; shift 2
OUT=${OUT:-/tmp/app_test_build}; mkdir -p $OUT
LVGL=${LVGL_DIR:-./lvgl}
SRC=${SRCDIR:-src}
DEFS="-DLV_CONF_INCLUDE_SIMPLE -DSCREEN_W=${SW:-320} -DSCREEN_H=${SH:-240}"
INC="-Istubs -I$SRC -I$LVGL -I."
LIBD=$OUT/lvgl_$(echo "$SAN" | tr -c 'a-z' '_')
if [ ! -f $LIBD/done ]; then
  mkdir -p $LIBD
  ls $(find $LVGL/src -name '*.c') | xargs -P $(nproc) -I{} sh -c 'o='$LIBD'/$(echo {} | md5sum | cut -c1-12).o; gcc -std=gnu11 -O1 -g -w '"$SAN"' '"$DEFS $INC"' -c {} -o $o'
  touch $LIBD/done
fi
SRCS="$SRC/core/app_manager.cpp $SRC/core/app_registry.cpp $SRC/core/theme.cpp $SRC/services/input_service.cpp \
 $SRC/services/image_service.cpp $SRC/services/image_resize.cpp $SRC/services/slideshow_service.cpp \
 $SRC/apps/main_screen/main_screen.cpp $SRC/apps/main_screen/slideshow_widget.cpp $SRC/apps/album/album_app.cpp \
 $(ls $SRC/apps/pet/*.cpp) stubs/stubs.cpp"
g++ -std=${STD:-gnu++17} -g -O1 -c -x c++ $SRC/core/psram.cpp -Dheap_caps_malloc=psram_caps_malloc $SAN $DEFS $INC -o $OUT/psram_$NAME.o
g++ -std=${STD:-gnu++17} -g -O1 -fno-omit-frame-pointer -Wall -Wextra -Wno-unused-parameter $SAN $DEFS $INC "$@" \
  test/app_test.cpp $SRCS $LIBD/*.o $OUT/psram_$NAME.o -o $OUT/$NAME -pthread
