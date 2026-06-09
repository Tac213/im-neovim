#pragma once

#include <filesystem>
#include <string>

namespace ImApp {

/// Converts a std::filesystem::path to a UTF-8 std::string.
///
/// On Windows, path::string() uses the ANSI code page (CP_ACP) which cannot
/// represent Unicode characters such as 中文, causing
/// ERROR_NO_UNICODE_TRANSLATION (system error 1113).
///
/// This function always produces proper UTF-8 on all platforms by routing
/// through path::u8string() and converting char8_t to char.
inline std::string path_to_string(const std::filesystem::path& p) {
    auto u8p = p.u8string();
    return {u8p.begin(), u8p.end()};
}

struct FileSystem {
    static std::filesystem::path executable_path();
    static std::filesystem::path local_app_data_path();
    static std::filesystem::path home_directory();
};

} // namespace ImApp