#pragma once

// Luu cai dat nguoi dung vao bo nho flash cua ESP32 (NVS), khong can the SD:
//   - theme dang chon (khoa theme)
//   - danh sach app hien tren Main Screen (khoa app, theo thu tu trong bang dang ky)
// Du lieu hong hoac khoa la bi bo qua; khong co du lieu thi dung mac dinh.
namespace SettingsStore {

// Doc tu flash va ap vao Themes va AppRegistry. Goi mot lan, truoc AppManager::init().
void begin();

// Ghi theme hien tai va lua chon app hien tai xuong flash.
void save();

}  // namespace SettingsStore
