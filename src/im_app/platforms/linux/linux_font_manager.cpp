#include "im_app/font_manager.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

namespace ImApp {

// Run fontconfig CLI to find a font file.
// Uses fc-match with format string to get the file path.
static std::string fc_match(const std::string& pattern) {
    std::string cmd = "fc-match -f '%{file}' '" + pattern + "' 2>/dev/null";
    std::array<char, 1024> buffer{};
    std::string result;

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return "";
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) !=
           nullptr) {
        result += buffer.data();
    }

    int rc = pclose(pipe);
    if (rc != 0) {
        // fc-match failed or returned non-zero
        return "";
    }

    // Trim whitespace
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' ||
                               result.back() == ' ')) {
        result.pop_back();
    }

    // Validate the result is a real file path
    if (result.empty() || result[0] != '/') {
        return "";
    }

    if (!std::filesystem::exists(result)) {
        return "";
    }

    return result;
}

// Hardcoded fallback paths for DejaVu Sans Mono
static std::string find_font_hardcoded(const std::string& family_name,
                                       bool bold, bool italic) {
    if (family_name != "DejaVu Sans Mono") {
        return "";
    }

    // Build filename
    std::string filename = "DejaVuSansMono";
    if (bold && italic) {
        filename += "-BoldOblique";
    } else if (bold) {
        filename += "-Bold";
    } else if (italic) {
        filename += "-Oblique";
    }

    // Common font directories on Linux
    const std::string font_dirs[] = {
        "/usr/share/fonts/truetype/dejavu",
        "/usr/share/fonts/TTF",
        "/usr/share/fonts/truetype",
        "/usr/local/share/fonts/truetype/dejavu",
    };

    for (const auto& dir : font_dirs) {
        std::filesystem::path path =
            std::filesystem::path(dir) / (filename + ".ttf");
        if (std::filesystem::exists(path)) {
            return path.string();
        }
    }

    return "";
}

std::string FontManager::find_system_font(const std::string& family_name,
                                          bool bold, bool italic) {
    // Build fontconfig pattern
    std::string pattern = family_name;
    if (bold && italic) {
        pattern += ":Bold:Italic";
    } else if (bold) {
        pattern += ":Bold";
    } else if (italic) {
        pattern += ":Italic";
    }

    // 1. Try fontconfig CLI
    std::string result = fc_match(pattern);
    if (!result.empty()) {
        return result;
    }

    // 2. Try hardcoded paths
    result = find_font_hardcoded(family_name, bold, italic);
    if (!result.empty()) {
        return result;
    }

    return "";
}

} // namespace ImApp
