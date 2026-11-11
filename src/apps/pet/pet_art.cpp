#include "apps/pet/pet_art.hpp"

namespace {

// ---------------------------------------------------------------------------
// Thân mèo: lưới 20x20, mỗi ký tự là một mã màu (kiểu tam thể gốc):
//   '.' trong suốt   'O' viền   'W' lông trắng   'R' lông cam   'K' lông đen
// Các skin khác tạo ra bằng cách đổi mã màu của từng ô (hàm recolor bên dưới),
// nên chỉ cần sửa dáng mèo ở đúng một chỗ này.
// ---------------------------------------------------------------------------
const char* const kBody[kPetGrid] = {
    "....................",  //  0
    "....O..........O....",  //  1
    "...OKO........ORO...",  //  2
    "..OKKKO......ORRRO..",  //  3
    "..OOOOOOOOOOOOOOOO..",  //  4
    ".OKKKKWWWWWWWWRRRRO.",  //  5
    ".OKKWWWWWWWWWWWWRRO.",  //  6
    ".OWWWWWWWWWWWWWWWWO.",  //  7
    ".OWWWWWWWWWWWWWWWWO.",  //  8
    "..OWWWWWWWWWWWWWWO..",  //  9
    "...OOOOOOOOOOOOOO...",  // 10
    "....OKKKWWWWRRRO....",  // 11
    "...OKKKKKWWWWRRROOOO",  // 12
    "...OKKKKWWWWWRRROKKO",  // 13
    "...OKKKWWWWWWRRROKKO",  // 14
    "...OKKWWWWWWWWRRORRO",  // 15
    "...OWWWWWWWWWWWWORRO",  // 16
    "...OWWWWWOOWWWWWORRO",  // 17
    "...OOOOOOOOOOOOOOOOO",  // 18
    "....................",  // 19
};

// Vằn của mèo cam ('S' là ô có vằn): trán, má, thân và đuôi khoanh.
const char* const kStripe[kPetGrid] = {
    "....................",  //  0
    "....................",  //  1
    "....................",  //  2
    "....................",  //  3
    "....................",  //  4
    ".......S.SS.S.......",  //  5
    ".........SS.........",  //  6
    ".........SS.........",  //  7
    "..SS............SS..",  //  8
    "....................",  //  9
    "....................",  // 10
    "....................",  // 11
    ".....S..S..S..S.....",  // 12
    ".....S..S..S..S.....",  // 13
    ".................SS.",  // 14
    "......S..S..S.......",  // 15
    "......S..S..S....SS.",  // 16
    "....................",  // 17
    "....................",  // 18
    "....................",  // 19
};

// Mặt: vùng 12 cột x 4 hàng, đặt tại hàng 6, cột 4 của lưới thân.
// 'E' mắt   'P' mũi   'M' miệng   'T' giọt nước mắt   '.' giữ nguyên thân
constexpr int kFaceRow0 = 6;
constexpr int kFaceCol0 = 4;
constexpr int kFaceRows = 4;
constexpr int kFaceCols = 12;

const char* const kFace[static_cast<int>(PetMood::COUNT)][kFaceRows] = {
    // NORMAL
    {".EE......EE.",
     ".EE......EE.",
     ".....PP.....",
     ".....MM....."},
    // HAPPY: mắt híp, miệng cười rộng
    {"............",
     ".EE......EE.",
     ".....PP.....",
     "....MMMM...."},
    // SLEEPING: mắt nhắm
    {"............",
     ".EE......EE.",
     ".....PP.....",
     ".....MM....."},
    // SAD: có giọt nước mắt
    {".EE......EE.",
     ".EE......EE.",
     ".T...PP.....",
     ".....MM....."},
};

// Vùng lông trắng giữ lại ở một số skin.
bool isMuzzle(int x, int y) { return y >= 8 && y <= 9 && x >= 7 && x <= 12; }
bool isChest(int x, int y)  { return y >= 11 && y <= 15 && x >= 8 && x <= 12; }

// Đổi mã màu của một ô thân (khác ô viền) theo skin.
char recolor(PetSkin skin, int x, int y, char c) {
    switch (skin) {
        case PetSkin::TUXEDO:
            // Đen toàn thân, chỉ giữ trắng ở mõm, ngực và hai chân dưới.
            if (c == 'R') return 'K';
            if (c == 'W' && !(isMuzzle(x, y) || isChest(x, y) || y >= 16)) return 'K';
            return c;
        case PetSkin::WHITE:
            // Trắng toàn thân, trong tai hồng, một dải xám nhạt ở đáy đầu và đáy thân.
            if (c == 'K' || c == 'R') return (y <= 3) ? 'N' : 'W';
            if (c == 'W' && (y == 9 || y == 17)) return 'G';
            return c;
        case PetSkin::ORANGE_TABBY:
            // Cam toàn thân (giữ mõm trắng) rồi chồng vằn lên.
            if (c == 'W' && isMuzzle(x, y)) return 'W';
            return (kStripe[y][x] == 'S') ? 'S' : 'R';
        case PetSkin::CALICO:
        default:
            return c;
    }
}

// Mèo nằm ngủ (dùng khi mood == SLEEPING): cùng lưới 20x20 và cùng mã màu với kBody,
// thêm 'M' mắt nhắm và 'P' mũi. Mèo nằm nghiêng, đầu bên trái, đuôi cuộn dưới thân.
const char* const kSleep[kPetGrid] = {
    "....................",  //  0
    "....................",  //  1
    "....................",  //  2
    "....................",  //  3
    "....................",  //  4
    "..O....O............",  //  5
    ".ORO..OKO...........",  //  6
    ".ORROOOKKOOOOO......",  //  7
    ".ORRWWWWWKKKKKOO....",  //  8
    "OWWWWWWWWKKKKWWWOO..",  //  9
    "OWWWWWWWWKKKKWWWWWO.",  // 10
    "OWMMWWMMWWWWWWWWWWO.",  // 11
    "OPWWWWWWWWWWWWRRRWO.",  // 12
    ".OWWWWWWWWWWWWRRRWO.",  // 13
    ".OWWWWWWWWWWWWRRRWO.",  // 14
    ".OOWWWWWWWWWWWWWWOO.",  // 15
    "..OOWWWOOOOOOOOOOOO.",  // 16
    "...OOOOO.ORRRRRRRRO.",  // 17
    ".........OOOOOOOOOO.",  // 18
    "....................",  // 19
};

// Đổi mã màu của kSleep theo skin (giữ nguyên viền 'O', mắt 'M', mũi 'P').
char recolorSleep(PetSkin skin, int x, int y, char c) {
    if (c == 'O' || c == 'M' || c == 'P') return c;
    switch (skin) {
        case PetSkin::TUXEDO:
            if (c == 'R') return 'K';
            if (c == 'W' && !(y >= 14 || (x <= 6 && y >= 11))) return 'K';
            return c;
        case PetSkin::WHITE:
            if (c == 'K' || c == 'R') return (y <= 8) ? 'N' : 'W';
            if (c == 'W' && y == 15) return 'G';
            return c;
        case PetSkin::ORANGE_TABBY:
            if (c == 'W' && x <= 6 && y >= 11) return 'W';
            return (x >= 9 && x % 3 == 0 && y >= 9 && y <= 14) ? 'S' : 'R';
        case PetSkin::CALICO:
        default:
            return c;
    }
}

// Màu placeholder (giao diện chính thức chốt sau, ARCHITECTURE.md mục 11).
uint32_t bodyRgb(PetSkin skin, char c) {
    switch (c) {
        case 'O': return 0x1A1210;  // viền
        case 'W': return 0xF4F0E8;  // trắng ấm
        case 'R': return 0xE8892B;  // cam
        case 'K': return 0x3A3A40;  // đen (hơi sáng hơn viền để còn thấy ranh giới)
        case 'S': return 0xB5601A;  // vằn cam đậm
        case 'G': return 0xD8D2C8;  // xám nhạt (bóng của mèo trắng)
        case 'N': return 0xF2B8C0;  // hồng (trong tai mèo trắng)
        case 'E': return (skin == PetSkin::TUXEDO) ? 0xF0D040 : 0x101010;  // mắt vàng trên lông đen
        case 'M': return 0x101010;  // miệng
        case 'P': return 0xF28FA0;  // mũi hồng
        case 'T': return 0x5BA8F0;  // nước mắt
        default:  return 0xFF00FF;  // ký tự lạ: hồng chói để dễ phát hiện lỗi
    }
}

// ---------------------------------------------------------------------------
// Icon 10x10. Mỗi icon có bảng màu riêng (tối đa 6 mã).
// ---------------------------------------------------------------------------
struct IconColor { char code; uint32_t rgb; };
struct IconDef {
    const char* rows[kPetIconGrid];
    IconColor   colors[6];
};

const IconDef kIcons[static_cast<int>(PetIcon::COUNT)] = {
    // EAT: con cá
    {{"..........",
      "..........",
      "...OOOO.OO",
      "..OBBBBOFO",
      ".OBEBBBFFO",
      ".OBBBBBFFO",
      "..OWWWWOFO",
      "...OOOO.OO",
      "..........",
      ".........."},
     {{'O', 0x14202A}, {'B', 0x8FC1E3}, {'F', 0x4F86B5}, {'W', 0xE6F3FA}, {'E', 0x0C1218}}},
    // PLAY: cuộn len
    {{"..........",
      "...OOOO...",
      "..OYYLYYO.",
      ".OYLYYYYYO",
      ".OYDDDYYYO",
      ".OYYYDDYYO",
      ".OYDYYYDYO",
      "..OYDDYYO.",
      "...OOOO.D.",
      "........DD"},
     {{'O', 0x3A1018}, {'Y', 0xE8506A}, {'L', 0xF58CA0}, {'D', 0xA82A44}}},
    // BATH: giọt nước
    {{"....OO....",
      "...OBBO...",
      "...OBBO...",
      "..OBLBBO..",
      "..OBLBBO..",
      ".OBLBBBBO.",
      ".OBBBBBDO.",
      ".OBBBBDDO.",
      "..OBBDDO..",
      "...OOOO..."},
     {{'O', 0x0F3A5E}, {'B', 0x4DB8F0}, {'L', 0xBDEBFF}, {'D', 0x2B7DC4}}},
    // SLEEP: chữ Z lớn và z nhỏ
    {{".....LLLL.",
      ".......L..",
      "......L...",
      ".....LLLL.",
      ".YYYYYY...",
      ".....YY...",
      "....YY....",
      "...YY.....",
      "..YY......",
      ".YYYYYY..."},
     {{'Y', 0xFFE680}, {'L', 0xFFF3B0}}},
};

// Màu hiển thị trên nút Skin (đậm hơn màu lông thật để nổi trên nền nút xám xanh).
constexpr uint32_t kSwatchOrange = 0xE8892B;
constexpr uint32_t kSwatchWhite  = 0xF4F0E8;
constexpr uint32_t kSwatchBlack  = 0x16161A;

}  // namespace

namespace {

// Hiệu ứng khi bấm nút, vẽ vào ô trống quanh mèo (không bao giờ đè lên mèo).
bool petEffectColor(int gx, int gy, const PetAnim& a, uint32_t* rgb) {
    switch (a.fx) {
        case PetFx::EAT: {   // trái tim nhỏ giữa hai tai
            static const char* const kHeart[4] = {".R.R.", "RRRRR", ".RRR.", "..R.."};
            const int x = gx - 7;
            if (x >= 0 && x < 5 && gy >= 0 && gy < 4 && kHeart[gy][x] == 'R') { *rgb = 0xE8506A; return true; }
            return false;
        }
        case PetFx::PLAY: {  // cuộn len 3x3 bên trái, điểm sáng đổi chỗ theo nhịp
            const int y = gy - 15;
            if (gx < 0 || gx > 2 || y < 0 || y > 2) return false;
            const bool hi = (a.fxPhase & 1u) ? (gx == 2 && y == 2) : (gx == 0 && y == 0);
            *rgb = hi ? 0xF58CA0 : ((gx == 1 && y == 1) ? 0xA82A44 : 0xE8506A);
            return true;
        }
        case PetFx::BATH: {  // ba bong bóng 2x2 nổi lên
            const int bx[3] = {0, 9, 18};
            const int by[3] = {16 - 2 * a.fxPhase, 3 - a.fxPhase % 3, 11 - 2 * a.fxPhase};
            for (int i = 0; i < 3; ++i) {
                if (gx >= bx[i] && gx < bx[i] + 2 && gy >= by[i] && gy < by[i] + 2) { *rgb = 0xBDEBFF; return true; }
            }
            return false;
        }
        default:
            return false;
    }
}

}  // namespace

bool petBodyColor(int gx, int gy, PetMood mood, PetSkin skin, uint32_t* rgb, const PetAnim& anim) {
    if (gx < 0 || gx >= kPetGrid || gy < 0 || gy >= kPetGrid || rgb == nullptr) return false;

            const bool sleeping = (mood == PetMood::SLEEPING);

    // Hoạt ảnh: đổi hàng nguồn `sy` hoặc ẩn ô (lưới dữ liệu gốc không bị sửa).
    int sy = gy;
    int sx = gx;
    if (sleeping) {
        if (anim.breathe && gy >= 4 && gy <= 12) sy = gy + 1;           // thân phồng lên 1 hàng
    } else {
        if (anim.tailUp && gx >= 17 && gy >= 12 && gy <= 14) {          // đuôi nghiêng: nửa trên đuôi dồn sang trái 1 ô
            if (gx == 19) return false;                                 // (đuôi bình thường nghiêng sang phải)
            if (gx == 18) sx = 19;                                      // viền ngoài dịch vào, ruột đuôi hẹp còn 1 ô
        }      
        if (anim.earTwitch && gx == 15 && gy == 1) return false;        // đỉnh tai phải cụp xuống
    }

    char c = sleeping ? kSleep[sy][sx] : kBody[sy][sx];                                  
    if (c == '.') return !sleeping && petEffectColor(gx, gy, anim, rgb);                                                             
    if (c != 'O') c = sleeping ? recolorSleep(skin, gx, sy, c) : recolor(skin, sx, sy, c);   

    int m = (mood < PetMood::COUNT) ? static_cast<int>(mood) : 0;
    if (anim.blink && (mood == PetMood::NORMAL || mood == PetMood::SAD)) {
        m = static_cast<int>(PetMood::SLEEPING);   // mặt mắt nhắm (giữ nguyên khối SLEEPING trong kFace)
    }
    const int fy = gy - kFaceRow0;
    const int fx = gx - kFaceCol0;
    if (!sleeping && fy >= 0 && fy < kFaceRows && fx >= 0 && fx < kFaceCols) {
                char f = kFace[m][fy][fx];
        if (anim.fx == PetFx::EAT && (anim.fxPhase & 1u) && fy == 3) {
            f = kFace[static_cast<int>(PetMood::NORMAL)][fy][fx];   // nhai: miệng đóng mở
        }
        if (f == 'T' && anim.tear != 0) f = '.';                     // giọt nước mắt đã rời mắt
        if (f != '.') c = f;
    }
    if (mood == PetMood::SAD && anim.tear == 1 && gx == 5 && gy == 9) c = 'T';   // giọt nước mắt trên má

    *rgb = bodyRgb(skin, c);
    return true;
}

bool petIconColor(PetIcon icon, int gx, int gy, uint32_t* rgb) {
    if (icon >= PetIcon::COUNT || rgb == nullptr) return false;
    if (gx < 0 || gx >= kPetIconGrid || gy < 0 || gy >= kPetIconGrid) return false;

    const IconDef& def = kIcons[static_cast<int>(icon)];
    const char c = def.rows[gy][gx];
    if (c == '.') return false;

    for (const IconColor& ic : def.colors) {
        if (ic.code == c) {
            *rgb = ic.rgb;
            return true;
        }
    }
    *rgb = 0xFF00FF;   // mã lạ: hồng chói để dễ phát hiện lỗi
    return true;
}

int petSkinBands(PetSkin skin) {
    return (skin == PetSkin::CALICO) ? 3 : 1;
}

uint32_t petSkinBandColor(PetSkin skin, int band) {
    switch (skin) {
        case PetSkin::CALICO: {
            static const uint32_t kBands[3] = {kSwatchOrange, kSwatchWhite, kSwatchBlack};
            return kBands[(band >= 0 && band < 3) ? band : 0];
        }
        case PetSkin::TUXEDO:       return kSwatchBlack;
        case PetSkin::WHITE:        return kSwatchWhite;
        case PetSkin::ORANGE_TABBY: return kSwatchOrange;
        default:                    return kSwatchWhite;
    }
}
