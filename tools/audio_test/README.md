# tools/audio_test: test may tinh cho services/audio_service

Bien dich CHINH `src/services/audio_service.cpp` (va minimp3 that) voi stub phan cung:
FreeRTOS (pthread, co kiem tra thu tu khoa gCtlMtx -> gRingMtx -> SD), I2S (ghi lai moi mau ra loa),
PSRAM (dem byte, chen loi het bo nho), SD (thu muc that, chen loi doc/cham/thao the). Chay duoi ASan + UBSan.

Moi mau PCM ra I2S duoc so tung mau voi ket qua giai ma doc lap bang minimp3 (ngoai AudioService).

## Chay
```
cd tools/audio_test
sh run.sh            # src o ../../src
TSAN=1 sh run.sh     # them ThreadSanitizer (chi de tham khao, xem ghi chu)
```
Can g++ (>= 5). `data/*.mp3` da tao san; tao lai bang `sh gen_mp3.sh data` (can ffmpeg + libmp3lame).
Test cung tu tao cac file hong (cat cut, hong giua, rac, ID3 lon, ID3v1...) trong thu muc tam.

## Noi dung
- begin(): cau hinh I2S/pin/task (stack, uu tien), goi lai, het PSRAM o tung lan cap phat, I2S loi
- Toan ven PCM 5 file (44.1k stereo, 22.05k mono, 48k VBR, 8k mono MPEG-2.5, khong tag): tung mau, set_clk, finished() dung 1 lan, thoi luong uoc
- Am luong (0,1,30,50,99,100,255), pause/resume/stop, doi bai khi dang phat
- Tua: +/-, qua dau/cuoi bai, luc PAUSED, luc STOPPED, 300 lan lien tiep; dinh vi mau dang phat so voi positionMs()
- readPcmTap: cua so lien tuc, truoc am luong, 3 luong doc dong thoi
- Ring (white-box): quay vong va tran so 32 bit cua gHead/gTail, ring day
- File hong/bien: cat cut, hong giua, rac, rong, chi co ID3, ID3 khai bao qua lon, ID3 100 KB, footer, ID3v1, duong dan 300 ky tu, khong ton tai
- Loi SD: mat the giua bai, thao the, doc cham, rut/cam lai
- 4 luong bam ngau nhien 4 giay (khong treo, khong sai thu tu khoa), sau do van phat dung

## Ghi chu
- Thoi gian duoc mo phong nhanh 5 lan (timeScale). Khong dat > 8: duoi ASan decode khong theo kip, cac phep do thoi gian sai.
- "PHAT HIEN ..." la loi that cua audio_service da biet (xem bao cao), khong lam test hong. "FAIL" la test hong.
- ThreadSanitizer bao khoang 70 canh tranh o cac co volatile (gState, gFinished, gDecodeDone...): khong dong bo nhung doc/ghi 8/32 bit
  can hang tren Xtensa nen vo hai; xem bao cao cho truong hop co hai (finished() mat su kien).
- Test khong do duoc: am thanh that, do tre DMA that, stack thuc cua task (can do tren board bang uxTaskGetStackHighWaterMark).
