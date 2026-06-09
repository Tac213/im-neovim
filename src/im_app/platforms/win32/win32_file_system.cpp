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

std::filesystem::path FileSystem::home_directory() {
    // Primary: SHGetKnownFolderPath with FOLDERID_Profile.
    wchar_t* known_path = nullptr;
    if (::SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &known_path) ==
            S_OK &&
        known_path != nullptr && known_path[0] != L'\0') {
        std::filesystem::path ret{known_path};
        ::CoTaskMemFree(known_path);
        return ret;
    }

    // Fallback 1: USERPROFILE environment variable (safe version).
    wchar_t* profile = nullptr;
    if (_wdupenv_s(&profile, nullptr, L"USERPROFILE") == 0 &&
        profile != nullptr && profile[0] != L'\0') {
        std::filesystem::path ret{profile};
        ::free(profile);
        return ret;
    }

    // Fallback 2: HOMEDRIVE + HOMEPATH.
    wchar_t* drive = nullptr;
    wchar_t* hpath = nullptr;
    if (_wdupenv_s(&drive, nullptr, L"HOMEDRIVE") == 0 && drive != nullptr &&
        _wdupenv_s(&hpath, nullptr, L"HOMEPATH") == 0 && hpath != nullptr) {
        std::filesystem::path ret{std::wstring{drive} + hpath};
        ::free(drive);
        ::free(hpath);
        return ret;
    }
    ::free(drive);
    ::free(hpath);

    return std::filesystem::path{};
}
} // namespace ImApp
