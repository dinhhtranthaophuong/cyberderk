#pragma once
// TJpg_Decoder gia lap. "JPG" gia: 'F','J','P','G', u16 w, u16 h, u32 seed, roi byte thua.
// Anh goc: pixel(x,y) = f(seed,x,y) xac dinh. Giai ma o scale s cho khoi 16x16 (da thu nho),
// moi diem = diem goc (x*s, y*s) (lay mau, khong trung binh), dung nhu bo giai ma that ve hinh hoc.
#include <cstdint>
#include <cstddef>
typedef enum { JDR_OK = 0, JDR_INP, JDR_FMT1, JDR_MEM1 } JRESULT;
typedef bool (*TJpgCb)(int16_t, int16_t, uint16_t, uint16_t, uint16_t*);
uint16_t fakeJpgPixel(uint32_t seed, int x, int y, int W, int H);   // dinh nghia trong test
class TJpgDecoder {
 public:
  void setJpgScale(uint8_t s) { scale_ = s; }
  void setSwapBytes(bool) {}
  void setCallback(TJpgCb cb) { cb_ = cb; }
  JRESULT getJpgSize(uint16_t* w, uint16_t* h, const uint8_t* d, uint32_t n);
  JRESULT drawJpg(int32_t x, int32_t y, const uint8_t* d, uint32_t n);
 private:
  uint8_t scale_ = 1;
  TJpgCb cb_ = nullptr;
};
extern TJpgDecoder TJpgDec;
