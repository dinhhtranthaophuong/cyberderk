#!/bin/sh
# Tao MP3 thu bang ffmpeg (libmp3lame). Chay: sh gen_mp3.sh data
# Tin hieu la chirp (tan so quet) nen moi cua so mau deu duy nhat, dung de dinh vi vi tri sau khi tua.
set -e
OUT=${1:-data}
mkdir -p "$OUT"
F=ffmpeg
Q="-hide_banner -loglevel error -y"
chirp() { echo "aevalsrc=0.5*sin(2*PI*t*($1+$2*t))|0.5*sin(2*PI*t*($3+$4*t)):s=$5:d=$6"; }
mono()  { echo "aevalsrc=0.5*sin(2*PI*t*($1+$2*t)):s=$3:d=$4"; }
# a: 44.1 kHz stereo CBR 128, ID3v2 + Xing/Info, 3 s
$F $Q -f lavfi -i "$(chirp 300 400 500 300 44100 3)" -c:a libmp3lame -b:a 128k -id3v2_version 3 -metadata title=thu "$OUT/a_44k_stereo.mp3"
# b: 22.05 kHz mono (MPEG-2), 3 s
$F $Q -f lavfi -i "$(mono 200 500 22050 3)" -c:a libmp3lame -b:a 32k -ac 1 "$OUT/b_22k_mono.mp3"
# c: 48 kHz stereo VBR, 3 s
$F $Q -f lavfi -i "$(chirp 250 600 700 200 48000 3)" -c:a libmp3lame -q:a 4 "$OUT/c_48k_vbr.mp3"
# d: 8 kHz mono (MPEG-2.5), 2 s
$F $Q -f lavfi -i "$(mono 150 300 8000 2)" -c:a libmp3lame -b:a 16k -ac 1 -ar 8000 "$OUT/d_8k_mono.mp3"
# f: 44.1 kHz CBR, khong ID3, khong Xing (uoc thoi luong theo bitrate), 3 s
$F $Q -f lavfi -i "$(chirp 350 350 450 250 44100 3)" -c:a libmp3lame -b:a 128k -write_xing 0 -id3v2_version 0 -write_id3v1 0 "$OUT/f_plain_44k.mp3"
# n: 20 s CBR 128 co Xing (tua, vong ring)
$F $Q -f lavfi -i "$(chirp 200 50 300 60 44100 20)" -c:a libmp3lame -b:a 128k "$OUT/n_long_44k.mp3"
ls -l "$OUT"
