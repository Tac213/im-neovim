#include "im_app/file_system.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Shlobj_core.h>
#include <Windows.h>

namespace ImApp {
std::filesystem::path FileSystem::executable_path() {
    wchar_t buf[MAX_PATH] = {0};
    DWORD bufsize = 0;
    ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    auto ret = std::filesystem::path(buf);
    return ret;
}

std::filesystem::path FileSystem::local_app_data_path() {
    LPWSTR buf = nullptr;
    if (::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr,
                               &buf) == S_OK) {
        auto ret = std::filesystem::path(buf);
        if (buf) {
            ::CoTaskMemFree(buf);
        }
        return ret;
    }
    return std::filesystem::path();
}
} // namespace ImApp
