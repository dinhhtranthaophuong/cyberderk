#pragma once
// Khai bao giong header that (services/sd_storage.hpp). Cai dat gia lap o stubs/stubs.cpp.
#include <cstddef>
#include <string>
#include <vector>
namespace SdStorage {
bool begin();
bool isMounted();
std::vector<std::string> listFiles(const char* dir, const char* exts);
bool exists(const char* path);
void* readFileToPsram(const char* path, size_t* outSize);
bool writeFile(const char* path, const void* data, size_t len);
}
