#pragma once
// Nguon su that duy nhat cho GPIO (xem ARCHITECTURE.md muc 1.2).
// TFT_eSPI doc chan tu build_flags trong platformio.ini, nen neu doi chan TFT
// phai doi CA HAI cho. static_assert o cuoi file se bao loi neu hai ben lech nhau.
//
// Chan CAM tren ESP32-S3 N16R8: GPIO 26-37 (flash/PSRAM), 0/3/45/46 (strapping),
// 19/20 (USB), 43/44 (UART0).

namespace Pins {

// TFT (FSPI / SPI2)
constexpr int TftSclk = 12;
constexpr int TftMosi = 11;
constexpr int TftCs   = 10;
constexpr int TftDc   = 9;
constexpr int TftRst  = 8;
constexpr int TftBl   = 7;   // tuy chon: neu LED cua man noi thang 3V3 thi chan nay bo trong

// microSD (HSPI / SPI3)
constexpr int SdSck  = 15;
constexpr int SdMosi = 16;
constexpr int SdMiso = 17;
constexpr int SdCs   = 18;

// I2S MAX98357A - chua dung o buoc 1
constexpr int I2sBclk = 4;
constexpr int I2sLrc  = 5;
constexpr int I2sDin  = 6;

// Keypad 3x3: hang la output, cot la input pull-up - chua dung o buoc 1
constexpr int KeyRow[3] = {38, 39, 40};
constexpr int KeyCol[3] = {41, 42, 21};

// I2C (DS3231) - chua dung o buoc 1
constexpr int I2cSda = 1;
constexpr int I2cScl = 2;

}  // namespace Pins

// Kiem tra build_flags khop voi bang chan o tren.
#if defined(TFT_SCLK) && defined(TFT_MOSI) && defined(TFT_CS) && defined(TFT_DC) && defined(TFT_RST)
static_assert(TFT_SCLK == Pins::TftSclk, "TFT_SCLK trong platformio.ini lech voi pins.hpp");
static_assert(TFT_MOSI == Pins::TftMosi, "TFT_MOSI trong platformio.ini lech voi pins.hpp");
static_assert(TFT_CS == Pins::TftCs, "TFT_CS trong platformio.ini lech voi pins.hpp");
static_assert(TFT_DC == Pins::TftDc, "TFT_DC trong platformio.ini lech voi pins.hpp");
static_assert(TFT_RST == Pins::TftRst, "TFT_RST trong platformio.ini lech voi pins.hpp");
#endif
