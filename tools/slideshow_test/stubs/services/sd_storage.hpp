#pragma once
#include <cstddef>
namespace SdStorage {
void* readFileToPsram(const char* path, size_t* outSize);
bool writeFile(const char* path, const void* data, size_t len);
}
