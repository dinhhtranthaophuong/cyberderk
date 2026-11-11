#pragma once

// The microSD tren bus HSPI rieng (khong dung chung voi TFT). Nhieu task dung chung the
// (audio doc nhac, ImageService doc anh) nen moi ham ben duoi tu khoa bang mutex de quy.
// Khoa chi giu trong tung lan doc/ghi (toi da 8 KB), khong giu luc giai ma.
//
// Duong dan tuyet doi tu goc the: "/images/1.jpeg", "/music/a.mp3".
#include <FS.h>

#include <cstddef>
#include <string>
#include <vector>

namespace SdStorage {

// Mount the (25 MHz, that bai thi thu 10 roi 4 MHz). Goi lai khi da mount thi tra true ngay.
// Goi truoc khi co task nao khac dung SD, khong can giu LvglLock.
bool begin();
bool isMounted();

// Liet ke file (bo qua thu muc va file an bat dau bang '.') theo duoi, khong phan biet hoa/thuong.
// `exts` la danh sach duoi cach nhau dau ';', vi du ".jpg;.jpeg". Tra duong dan day du,
// da sap xep (khong phan biet hoa/thuong). Rong neu thu muc khong ton tai hoac chua mount.
std::vector<std::string> listFiles(const char* dir, const char* exts);

bool exists(const char* path);

// Doc ca file vao PSRAM. Nguoi goi giai phong bang Psram::release(). Tra nullptr neu loi
// (khong co file, file rong, het PSRAM, doc thieu). *outSize la so byte (0 neu loi).
void* readFileToPsram(const char* path, size_t* outSize);

// Ghi de file (tao moi neu chua co). KHONG tu tao thu muc cha. Tra true neu ghi du.
bool writeFile(const char* path, const void* data, size_t len);

// Doc theo dong (audio). Moi lan open/read/seek tu khoa/nha khoa.
class Reader {
 public:
  Reader() = default;
  ~Reader() { close(); }
  Reader(const Reader&) = delete;
  Reader& operator=(const Reader&) = delete;

  bool open(const char* path);
  size_t read(void* dst, size_t n);  // tra so byte doc duoc (0 = het file hoac loi)
  bool seek(size_t pos);
  size_t size() const { return size_; }
  bool isOpen() const { return open_; }
  void close();

 private:
  File file_;
  size_t size_ = 0;
  bool open_ = false;
};

}  // namespace SdStorage
