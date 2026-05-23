#include "im_app/font_manager.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <filesystem>
#include <string>

namespace ImApp {

// Registry-based font discovery:
// Fonts are registered at HKLM\SOFTWARE\Microsoft\Windows
// NT\CurrentVersion\Fonts Value names are like "Cascadia Mono (TrueType)", data
// is the filename like "CascadiaMono.ttf"
static std::string find_font_in_registry(const std::string& family_name,
                                         bool bold, bool italic) {
    // Build the registry value name we're looking for
    std::string value_name = family_name;
    if (bold && italic) {
        value_name += " Bold Italic";
    } else if (bold) {
        value_name += " Bold";
    } else if (italic) {
        value_name += " Italic";
    }
    // Registry value names typically end with " (TrueType)" or " (OpenType)"
    std::string value_name_tt = value_name + " (TrueType)";
    std::string value_name_ot = value_name + " (OpenType)";

    HKEY hkey;
    if (::RegOpenKeyExA(
            HKEY_LOCAL_MACHINE,
            "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", 0,
            KEY_READ, &hkey) != ERROR_SUCCESS) {
        return "";
    }

    std::string result;
    DWORD index = 0;
    char value_buf[256];
    DWORD value_size = sizeof(value_buf);
    BYTE data_buf[256];
    DWORD data_size = sizeof(data_buf);
    DWORD type;

    while (::RegEnumValueA(hkey, index, value_buf, &value_size, nullptr, &type,
                           data_buf, &data_size) == ERROR_SUCCESS) {
        std::string current_value(value_buf, value_size);

        // Strip trailing null if present
        if (!current_value.empty() && current_value.back() == '\0') {
            current_value.pop_back();
        }

        if (current_value == value_name_tt || current_value == value_name_ot ||
            current_value == value_name) {
            std::string filename(reinterpret_cast<char*>(data_buf), data_size);
            // Strip trailing nulls
            while (!filename.empty() && filename.back() == '\0') {
                filename.pop_back();
            }

            // Construct full path
            std::filesystem::path font_dir =
                std::filesystem::path("C:\\Windows\\Fonts");
            std::filesystem::path full_path = font_dir / filename;

            if (std::filesystem::exists(full_path)) {
                result = full_path.string();
                break;
            }
        }

        index++;
        value_size = sizeof(value_buf);
        data_size = sizeof(data_buf);
    }

    ::RegCloseKey(hkey);
    return result;
}

// Hardcoded fallback paths for Cascadia Mono
static std::string find_font_hardcoded(const std::string& family_name,
                                       bool bold, bool italic) {
    // Only support Cascadia Mono (the designated Windows default)
    if (family_name != "Cascadia Mono") {
        // Also try "Cascadia Code" as a fallback family name
    }

    // Build filename
    std::string filename = "CascadiaMono";
    if (bold && italic) {
        filename += "-BoldItalic";
    } else if (bold) {
        filename += "-Bold";
    } else if (italic) {
        filename += "-Italic";
    }

    // Try .ttf first
    std::filesystem::path font_dir = "C:\\Windows\\Fonts";
    std::filesystem::path ttf_path = font_dir / (filename + ".ttf");

    if (std::filesystem::exists(ttf_path)) {
        return ttf_path.string();
    }

    // Try without dash (some installs use "CascadiaMonoBold.ttf" etc.)
    // Actually, the Windows font folder may have the files directly or in
    // subdirectories. Check common patterns:
    // - C:\Windows\Fonts\CascadiaMono.ttf
    // - C:\Windows\Fonts\CascadiaCode.ttf  (Cascadia Code also includes Mono
    // weight)
    if (family_name == "Cascadia Mono") {
        // Fall back to Cascadia Code if Mono not found
        std::string code_filename = "CascadiaCode";
        if (bold && italic) {
            code_filename += "-BoldItalic";
        } else if (bold) {
            code_filename += "-Bold";
        } else if (italic) {
            code_filename += "-Italic";
        }
        std::filesystem::path code_path = font_dir / (code_filename + ".ttf");
        if (std::filesystem::exists(code_path)) {
            return code_path.string();
        }
    }

    return "";
}

std::string FontManager::find_system_font(const std::string& family_name,
                                          bool bold, bool italic,
                                          bool /*allow_fallback*/) {
    // 1. Try registry
    std::string result = find_font_in_registry(family_name, bold, italic);
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
