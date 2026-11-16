#!/usr/bin/env python3
"""Kiem thu dot bien: chen tung loi co y vao ban sao cua ma nguon, bo test phai THAT BAI.
Dung: python3 mutants.py   (can g++ co ASan/UBSan)."""
import os, shutil, subprocess, sys, tempfile
here = os.path.dirname(os.path.abspath(__file__))
root = os.path.join(here, '..', '..')
SRC = root if os.path.exists(os.path.join(root, 'services', 'slideshow_service.cpp')) else os.path.join(here, 'src')
S, I, R = 'services/slideshow_service.cpp', 'services/image_service.cpp', 'services/image_resize.cpp'
# (ten, file, cu, moi)
M = [
 ("bo kiem tra gen o runJob (ket qua cu ghi de)", S, "if (job->gen != gGen) {", "if (false) {"),
 ("job cu bi bo nhung khong giai phong anh", S, "if (ok) ImageService::free(&img);\n    return;", "return;"),
 ("setPhoto khong tang gen", S, "bool setPhoto(const char* path) {", "bool setPhoto(const char* path) {\n  struct G { ~G() {} } g_;"),  # thay doi that o dong duoi
 ("clearPhoto khong tang gen", S, "void clearPhoto() {\n  std::lock_guard<std::mutex> lk(gMutex);\n  ++gGen;", "void clearPhoto() {\n  std::lock_guard<std::mutex> lk(gMutex);"),
 ("doi kich thuoc khong tang gen", S, "      ++gGen;\n      dropCacheLocked();\n      startJobLocked(targetW, targetH);", "      dropCacheLocked();\n      startJobLocked(targetW, targetH);"),
 ("setPhoto khong xoa cache cu", S, "  ++gGen;\n  dropCacheLocked();\n  gPath.assign(path, len);", "  ++gGen;\n  gPath.assign(path, len);"),
 ("khong cat '\\r'", S, "s[n - 1] == '\\n' || s[n - 1] == '\\r' || s[n - 1] == ' '", "s[n - 1] == '\\n' || s[n - 1] == ' '"),
 ("khong cat khoang trang", S, "|| s[n - 1] == ' ')) --n;", ")) --n;"),
 ("khong gioi han 255 khi doc file", S, "size_t n = size > kMaxPathLen ? kMaxPathLen : size;", "size_t n = size;"),
 ("bien 255 -> 254", S, "len > kMaxPathLen) return false;", "len >= kMaxPathLen) return false;"),
 ("nhan duong dan tuong doi", S, "if (!path || path[0] != '/') return false;", "if (!path) return false;"),
 ("nhan nullptr", S, "if (!path || path[0] != '/') return false;", "if (path[0] != '/') return false;"),
 ("ro ri Job khi tao task loi", S, "    delete job;\n    gState = State::FAILED;", "    gState = State::FAILED;"),
 ("task khong tu xoa", S, "  vTaskDelete(nullptr);\n}", "}"),
 ("giu gMutex luc giai ma", S, "void runJob(Job* raw) {\n  std::unique_ptr<Job> job(raw);\n  Image img;", "void runJob(Job* raw) {\n  std::unique_ptr<Job> job(raw);\n  std::lock_guard<std::mutex> hold(gMutex);\n  Image img;"),
 ("giai ma loi khong dat FAILED", S, "    gState = State::FAILED;\n    Serial.printf(\"[Slideshow] giai ma loi", "    gState = State::IDLE;\n    Serial.printf(\"[Slideshow] giai ma loi"),
 ("khong dat READY", S, "  gState = State::READY;\n  Serial.printf", "  gState = State::LOADING;\n  Serial.printf"),
 ("cover: chon canh nho (letterbox)", S, "const float s = sx > sy ? sx : sy;", "const float s = sx > sy ? sy : sx;"),
 ("cover: khong can giua x", S, "const float x0 = (src.w - w / s) * 0.5f;", "const float x0 = 0.0f;"),
 ("cover: khong can giua y", S, "const float y0 = (src.h - h / s) * 0.5f;", "const float y0 = 0.0f;"),
 ("cover: hoan doi sx/sy", S, "const float sx = static_cast<float>(w) / src.w;\n  const float sy = static_cast<float>(h) / src.h;", "const float sx = static_cast<float>(h) / src.w;\n  const float sy = static_cast<float>(w) / src.h;"),
 ("cover: bo chan fx > maxX (doc tran)", S, "      if (fx > maxX) fx = maxX;\n", ""),
 ("cover: bo chan fy > maxY (doc tran)", S, "    if (fy > maxY) fy = maxY;\n", ""),
 ("cover: lat truc x", S, "drow[dx] = bilinear565(row0[ix]", "drow[w - 1 - dx] = bilinear565(row0[ix]"),
 ("bilinear: sai trong so", S, "const uint32_t w11 = fx * fy;", "const uint32_t w11 = fx * fy / 2;"),
 ("bilinear: khong xoay kenh G", S, "const uint32_t g = (((p00 >> 5) & 63) * w00", "const uint32_t g = (((p00 >> 6) & 63) * w00"),
 ("doi kich thuoc chi so chieu rong", S, "if (gW == targetW && gH == targetH) return;", "if (gW == targetW) return;"),
 ("doi kich thuoc: khong giai phong cache", S, "      ++gGen;\n      dropCacheLocked();\n      startJobLocked", "      ++gGen;\n      startJobLocked"),
 ("settled() coi LOADING la xong", S, "gState != State::UNLOADED && gState != State::IDLE && gState != State::LOADING;", "gState != State::UNLOADED && gState != State::IDLE;"),
 ("image() luon tra con tro", S, "return gState == State::READY ? &gDsc : nullptr;", "return &gDsc;"),
 ("setPhoto luon tra true", S, "  return saved;\n}", "  return true;\n}"),
 ("clearPhoto khong ghi file", S, '  SdStorage::writeFile(kPathFile, "", 0);', "  ;"),
 ("setPhoto khong ghi file", S, "const bool saved = SdStorage::writeFile(kPathFile, path, len);", "const bool saved = true;"),
 ("clearPhoto khong xoa cache", S, "  ++gGen;\n  dropCacheLocked();\n  gPath.clear();", "  ++gGen;\n  gPath.clear();"),
 ("request khong thu lai khi FAILED", S, "    case State::IDLE:\n    case State::FAILED:", "    case State::IDLE:"),
 ("request giai ma lai khi READY cung kich thuoc", S, "if (gW == targetW && gH == targetH) return;", "if (false) return;"),
 ("khong tao task khi IDLE (chi FAILED)", S, "    case State::IDLE:\n    case State::FAILED:", "    case State::FAILED:"),
 ("loadPath doc lai moi lan", S, "if (gState != State::UNLOADED) return;", "if (gState == State::READY || gState == State::LOADING) return;"),
 ("loadPath: rong khong thanh NO_PHOTO", S, "gState = gPath.empty() ? State::NO_PHOTO : State::IDLE;", "gState = State::IDLE;"),
 ("loadPath: ro ri bo dem file", S, "    Psram::release(buf);\n  }\n  gState", "  }\n  gState"),
 ("khong dung fallback anh goc", S, "    *out = src;  // het bo nho", "    ImageService::free(&src); return false;  // het bo nho"),
 ("fallback ro ri anh goc khi cover that bai thanh cong", S, "    ImageService::free(&src);\n    *out = dst;", "    *out = dst;"),
 ("stack task 8192 -> 4096", S, "constexpr uint32_t kTaskStack = 8192;", "constexpr uint32_t kTaskStack = 4096;"),
 ("task chay Core 1", S, "constexpr BaseType_t kTaskCore = 0;", "constexpr BaseType_t kTaskCore = 1;"),
 ("uu tien task = 2 (bang LVGL)", S, "constexpr UBaseType_t kTaskPrio = 1;", "constexpr UBaseType_t kTaskPrio = 2;"),
 ("data_size sai", S, "static_cast<uint32_t>(img.w) * static_cast<uint32_t>(img.h) * 2u;", "static_cast<uint32_t>(img.w) * static_cast<uint32_t>(img.h);"),
 ("header.h gan sai", S, "gDsc.header.h = static_cast<uint32_t>(img.h);", "gDsc.header.h = static_cast<uint32_t>(img.w);"),
 ("cf sai", S, "gDsc.header.cf = LV_IMG_CF_TRUE_COLOR;", "gDsc.header.cf = 0;"),
 ("ImageService: khong khoa giai ma", I, "DecodeLock() : ok_(xSemaphoreTake(mutex(), portMAX_DELAY) == pdTRUE) {}", "DecodeLock() : ok_(true) {}"),
 ("ImageService: nhan ~DecodeLock khong nha", I, "if (ok_) xSemaphoreGive(mutex());", ";"),
 ("ImageService: ro ri file", I, "    Psram::release(file);\n    if (!ok) *out = Image{};", "    if (!ok) *out = Image{};"),
 ("ImageService: ro ri khi loi giai ma", I, "    if (r != JDR_OK) {\n        Psram::release(decoded);\n        return false;", "    if (r != JDR_OK) {\n        return false;"),
 ("ImageService: ro ri decoded khi thu nho", I, "        ImageResize::boxDownscale(decoded, dw, dh, scaled, ow, oh);\n        Psram::release(decoded);", "        ImageResize::boxDownscale(decoded, dw, dh, scaled, ow, oh);"),
 ("ImageService: bo memset (pixel rac)", I, "    std::memset(decoded, 0, static_cast<size_t>(dw) * dh * sizeof(uint16_t));", ""),
 ("ImageService: khong chan kich thuoc vo ly", I, "|| jw > kMaxSourceSide || jh > kMaxSourceSide", ""),
 ("ImageService: onBlock khong cat phan du x", I, "if (x + copyW > g_ctx.w) copyW = g_ctx.w - x;", ""),
 ("ImageService: onBlock khong cat phan du y", I, "if (yy >= g_ctx.h) break;", ""),
 ("ImageService: giu g_ctx sau giai ma", I, "    g_ctx.dst = nullptr;\n    TJpgDec.setJpgScale(1);\n    if (r", "    TJpgDec.setJpgScale(1);\n    if (r"),
 ("resize: fitInside tran int", R, "const int64_t lhs = static_cast<int64_t>(srcW) * maxH;", "const int64_t lhs = static_cast<int>(srcW * maxH);"),
 ("resize: chooseJpegScale cho scale 8 sai", R, "static const int kScales[] = {8, 4, 2};", "static const int kScales[] = {8, 4, 2, 1};"),
 ("resize: scale chon qua lon", R, "if (srcW / s >= fit.w && srcH / s >= fit.h) return s;", "if (srcW / s >= fit.w || srcH / s >= fit.h) return s;"),
 ("resize: box filter khong lam tron", R, "(r + n / 2) / n", "r / n"),
 ("resize: fitInside cho phong to", R, "if (srcW <= maxW && srcH <= maxH) return {srcW, srcH};", ""),
]
# Dot bien "setPhoto khong tang gen": sua rieng (dong ++gGen trong setPhoto)
M[2] = ("setPhoto khong tang gen", S, "  ++gGen;\n  dropCacheLocked();\n  gPath.assign(path, len);", "  dropCacheLocked();\n  gPath.assign(path, len);")

def build_run(srcdir, tmp):
    exe = os.path.join(tmp, 'mut')
    cmd = ['g++', '-std=gnu++17', '-g', '-O1', '-fno-omit-frame-pointer', '-Wno-unused-parameter',
           '-fsanitize=address,undefined', '-I' + os.path.join(here, 'stubs'), '-I' + srcdir,
           os.path.join(here, 'test', 'slideshow_test.cpp'), os.path.join(srcdir, I), os.path.join(srcdir, R),
           '-o', exe, '-pthread']
    b = subprocess.run(cmd, capture_output=True, text=True)
    if b.returncode: return 'build', b.stderr[-300:]
    try:
        r = subprocess.run([exe, '--stress-ms', '500'], capture_output=True, text=True, timeout=180)
    except subprocess.TimeoutExpired:
        return 'timeout', ''
    return ('pass' if r.returncode == 0 else 'fail'), (r.stdout + r.stderr)

killed = survived = 0; surv = []
only = sys.argv[1:]
for i, (name, f, old, new) in enumerate(M):
    if only and str(i) not in only: continue
    tmp = tempfile.mkdtemp()
    sd = os.path.join(tmp, 'src'); os.makedirs(os.path.join(sd, 'services'))
    for x in os.listdir(os.path.join(SRC, 'services')):
        if x.endswith(('.cpp', '.hpp')): shutil.copy(os.path.join(SRC, 'services', x), os.path.join(sd, 'services', x))
    p = os.path.join(sd, f); t = open(p, encoding='utf-8').read()
    if t.count(old) != 1:
        print('M%02d BO QUA (mau khong khop %d lan): %s' % (i, t.count(old), name)); shutil.rmtree(tmp); continue
    open(p, 'w', encoding='utf-8').write(t.replace(old, new))
    st, out = build_run(sd, tmp)
    if st in ('fail', 'timeout', 'build'):
        killed += 1; print('M%02d  DA BAT (%s)  %s' % (i, st, name))
    else:
        survived += 1; surv.append((i, name)); print('M%02d  *** SONG SOT ***  %s' % (i, name))
    shutil.rmtree(tmp)
print('\nTong: %d bi bat, %d song sot' % (killed, survived))
for i, n in surv: print('  song sot M%02d: %s' % (i, n))
sys.exit(1 if survived else 0)
