#!/usr/bin/env python3
"""Kiem thu dot bien: chen tung loi co y vao ma that, test phai bat duoc (that bai / crash / treo)."""
import os, shutil, subprocess, sys, concurrent.futures as cf
HERE = os.path.dirname(os.path.abspath(__file__))
OUTROOT = os.environ.get('OUT', os.path.join(HERE, 'out'))
SRC = os.path.abspath(os.environ.get('SRCDIR', os.path.join(HERE, 'src')))
M = [
 ('M01 album: khong dung task nen khi huy', 'apps/album/album_app.cpp', '    if (task_) {\n        stop_.store(true);', '    if (false) {\n        stop_.store(true);'),
 ('M02 album: khong giai phong thumbnail', 'apps/album/album_app.cpp', '        for (Thumb& t : thumbs_) ImageService::free(&t.img);\n', ''),
 ('M03 album: khong giai phong anh day du', 'apps/album/album_app.cpp', '        ImageService::free(&fullImg_);\n        thumbs_.clear();', '        thumbs_.clear();'),
 ('M04 album: khong xa ket qua con trong hang doi', 'apps/album/album_app.cpp', 'while (xQueueReceive(resQ_, &r, 0) == pdTRUE) ImageService::free(&r.img);', 'while (xQueueReceive(resQ_, &r, 0) == pdTRUE) {}'),
 ('M05 album: bo kiem tra token anh day du', 'apps/album/album_app.cpp', 'if (!viewerOpen_ || !viewImg_ || r.token != fullToken_.load()) {', 'if (!viewerOpen_ || !viewImg_) {'),
 ('M06 album: so sanh toast khong dau (millis tran)', 'apps/album/album_app.cpp', 'static_cast<int32_t>(nowMs - toastUntil_) >= 0', 'nowMs >= toastUntil_'),
 ('M07 album: UP o hang dau quay vong sai', 'apps/album/album_app.cpp', 'next = lastRowStart + (sel_ % kCols);', 'next = lastRowStart;'),
 ('M08 album: gioi han anh 255', 'apps/album/album_app.cpp', 'constexpr int kMaxPhotos = 256;', 'constexpr int kMaxPhotos = 255;'),
 ('M09 album: OPTION trong viewer lay o dang chon', 'apps/album/album_app.cpp', 'setAsWidgetPhoto(viewerOpen_ ? viewIndex_ : sel_);', 'setAsWidgetPhoto(sel_);'),
 ('M10 album: dong viewer khong dua luoi theo anh', 'apps/album/album_app.cpp', '    sel_ = viewIndex_;   // lưới đi theo ảnh vừa xem\n', ''),
 ('M11 album: khong giam inFlight_', 'apps/album/album_app.cpp', '        if (inFlight_ > 0) --inFlight_;', ''),
 ('M12 album: toast khong an', 'apps/album/album_app.cpp', 'toastUntil_ = nowMs + kToastMs;', 'toastUntil_ = nowMs + 0x7fffffff;'),
 ('M13 main: UP/DOWN khong quay vong', 'apps/main_screen/main_screen.cpp', 'row = (row + GRID_ROWS - 1) % GRID_ROWS;', 'row = row > 0 ? row - 1 : 0;'),
 ('M14 main: widget trai LEFT vao cot dau', 'apps/main_screen/main_screen.cpp', 'self->focusItem(row * GRID_COLS + GRID_COLS - 1);  // quay vong: cot cuoi', 'self->focusItem(row * GRID_COLS);'),
 ('M15 main: khong nho focus widget trai', 'apps/main_screen/main_screen.cpp', '      lastFocusIdx = kLeftFocus;\n      break;', '      break;'),
 ('M16 main: sai thang trong dong ho', 'apps/main_screen/main_screen.cpp', 'kMonths[(now.month + 11) % 12]', 'kMonths[now.month % 12]'),
 ('M17 main: mau nut ghi cung', 'apps/main_screen/main_screen.cpp', 'lv_obj_set_style_bg_color(btn, lv_color_hex(th.panel), 0);', 'lv_obj_set_style_bg_color(btn, lv_color_hex(0x123456), 0);'),
 ('M18 main: ENTER o trong van chuyen app', 'apps/main_screen/main_screen.cpp', 'if (target != AppId::COUNT) AppManager::goTo(target);', 'AppManager::goTo(target);'),
 ('M19 pet: bo co destroying_ trong callback', 'apps/pet/pet_app.cpp', 'if (self == nullptr || self->destroying_ || self->root_ == nullptr) return;', 'if (self == nullptr || self->root_ == nullptr) return;'),
 ('M20 pet: khong huy sprite', 'apps/pet/pet_app.cpp', '    sprite_.destroy();\n', ''),
 ('M21 pet: khong huy icon', 'apps/pet/pet_app.cpp', '    icons_.destroy();\n', ''),
 ('M22 pet: trai/phai khong quay vong', 'apps/pet/pet_app.cpp', 'const int nCol = (col + dCol + kCols) % kCols;', 'const int nCol = (col + dCol < 0 || col + dCol >= kCols) ? col : col + dCol;'),
 ('M23 pet: an +20 thay vi +30', 'apps/pet/pet_state.cpp', 'add(PetStat::HUNGER, +30);', 'add(PetStat::HUNGER, +20);'),
 ('M24 pet: dt khong chiu tran so', 'apps/pet/pet_state.cpp', 'const uint32_t dt = nowMs - lastMs_;', 'const uint32_t dt = nowMs >= lastMs_ ? nowMs - lastMs_ : 0;'),
 ('M25 pet: Sleep khi nang luong day', 'apps/pet/pet_state.cpp', 'if (val_[e] >= kMax) return false;', ''),
 ('M26 manager: reload khong lam gi', 'core/app_manager.cpp', '} else if (reload && currentId != AppId::COUNT) {', '} else if (false) {'),
 ('M27 manager: delete app truoc khi huy man hinh', 'core/app_manager.cpp', '  lv_obj_del(oldScreen);\n  delete oldApp;', '  delete oldApp;\n  lv_obj_del(oldScreen);'),
 ('M28 manager: khong don group khi doi app', 'core/app_manager.cpp', '  lv_group_remove_all_objs(InputService::defaultGroup());\n', ''),
 ('M29 manager: HOME bi bo qua', 'core/app_manager.cpp', 'if (key == KEY_HOME) {\n      AppManager::goHome();', 'if (key == KEY_HOME) {\n      ;'),
 ('M30 manager: goTo cho phep id ngoai khoang', 'core/app_manager.cpp', 'if (v >= kNone) return;', ''),
]
def run(i):
    name, f, old, new = M[i]
    d = os.path.join(OUTROOT, 'mut%02d' % (i + 1))
    shutil.rmtree(d, ignore_errors=True); os.makedirs(d)
    s = os.path.join(d, 'src'); shutil.copytree(SRC, s)
    p = os.path.join(s, f); t = open(p, encoding='utf-8').read()
    if t.count(old) != 1: return (i, 'LOI MUTANT: doan can thay xuat hien %d lan' % t.count(old), '')
    open(p, 'w', encoding='utf-8').write(t.replace(old, new))
    env = dict(os.environ, SRCDIR=s, OUT=d)
    b = subprocess.run([os.path.join(HERE, 'build.sh'), 'm', '-fsanitize=address,undefined'], env=env, capture_output=True, text=True, cwd=HERE)
    if not os.path.exists(os.path.join(d, 'm')): return (i, 'KHONG BIEN DICH', b.stderr[-300:])
    try:
        r = subprocess.run([os.path.join(d, 'm')], capture_output=True, text=True, timeout=int(os.environ.get('MUT_TIMEOUT', '300')))
        out = r.stdout + r.stderr
        if r.returncode == 0: return (i, 'SONG SOT', '')
        first = [l for l in out.splitlines() if 'LOI [' in l or 'ERROR: AddressSanitizer' in l or 'runtime error' in l]
        return (i, 'BAT DUOC', (first[0] if first else 'thoat %d' % r.returncode)[:150])
    except subprocess.TimeoutExpired:
        return (i, 'BAT DUOC', 'treo / qua thoi gian')
if __name__ == '__main__':
    idx = [int(a) - 1 for a in sys.argv[1:]] or range(len(M))
    ok = 0
    with cf.ThreadPoolExecutor(max_workers=2) as ex:
        for i, st, why in ex.map(run, idx):
            print('%-52s %-10s %s' % (M[i][0], st, why), flush=True)
            ok += st == 'BAT DUOC'
    print('== %d / %d bi bat ==' % (ok, len(list(idx))))
