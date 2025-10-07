#pragma once

#include <filesystem>

namespace ImApp {
struct FileSystem {
    static std::filesystem::path executable_path();
    static std::filesystem::path local_app_data_path();
};
} // namespace ImApp