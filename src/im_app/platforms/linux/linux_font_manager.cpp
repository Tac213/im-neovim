#include "im_app/font_manager.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace ImApp {

// ---------------------------------------------------------------------------
// WSL Detection
// ---------------------------------------------------------------------------

/// Returns true if running under Windows Subsystem for Linux.
static bool is_wsl() {
    static bool checked = false;
    static bool result = false;
    if (checked) {
        return result;
    }
    checked = true;

    // Method 1: /proc/version mentions "microsoft" or "WSL"
    std::ifstream version_file("/proc/version");
    if (version_file.is_open()) {
        std::string line;
        std::getline(version_file, line);
        if (line.find("microsoft") != std::string::npos ||
            line.find("WSL") != std::string::npos) {
            result = true;
            return result;
        }
    }

    // Method 2: WSL interop device node exists
    result = std::filesystem::exists("/proc/sys/fs/binfmt_misc/WSLInterop");

    return result;
}

// ---------------------------------------------------------------------------
// Fontconfig CLI
// ---------------------------------------------------------------------------

/// Run fontconfig CLI to find a font file.
/// Uses fc-match with format string to get the file path.
static std::string fc_match(const std::string& pattern) {
    std::string cmd = "fc-match -f '%{file}' '" + pattern + "' 2>/dev/null";
    std::array<char, 1024> buffer{};
    std::string result;

    FILE* pipe = popen(cmd.c_str(), "r"); // NOLINT(cert-env33-c)
    if (!pipe) {
        return "";
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) !=
           nullptr) {
        result += buffer.data();
    }

    int rc = pclose(pipe);
    if (rc != 0) {
        return "";
    }

    // Trim trailing whitespace
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

// ---------------------------------------------------------------------------
// Font filename construction
// ---------------------------------------------------------------------------

/// Describes how a font family's filename is constructed for each style.
struct FontNaming {
    const char* family_name;
    const char* file_stem;
    const char* suffix_regular;     // e.g. "" or "-Regular"
    const char* suffix_bold;        // e.g. "-Bold" or "b"
    const char* suffix_italic;      // e.g. "-Italic" or "i"
    const char* suffix_bold_italic; // e.g. "-BoldItalic" or "z"
};

/// Known font families and their filename conventions.
/// Ordered roughly by quality / preference.
static const FontNaming g_known_fonts[] = {
    // Linux defaults
    {"DejaVu Sans Mono", "DejaVuSansMono", "", "-Bold", "-Oblique",
     "-BoldOblique"},
    {"Liberation Mono", "LiberationMono", "-Regular", "-Bold", "-Italic",
     "-BoldItalic"},
    {"Noto Sans Mono", "NotoSansMono", "-Regular", "-Bold", "-Italic",
     "-BoldItalic"},
    {"Ubuntu Mono", "UbuntuMono", "-Regular", "-Bold", "-Italic",
     "-BoldItalic"},
    {"Cousine", "Cousine", "-Regular", "-Bold", "-Italic", "-BoldItalic"},

    // Common developer fonts
    {"Source Code Pro", "SourceCodePro", "-Regular", "-Bold", "-Italic",
     "-BoldItalic"},
    {"Fira Code", "FiraCode", "-Regular", "-Bold", "-Italic", "-BoldItalic"},
    {"Fira Mono", "FiraMono", "-Regular", "-Bold", "-Italic", "-BoldItalic"},
    {"JetBrains Mono", "JetBrainsMono", "-Regular", "-Bold", "-Italic",
     "-BoldItalic"},
    {"Hack", "Hack", "-Regular", "-Bold", "-Italic", "-BoldItalic"},
    {"Inconsolata", "Inconsolata", "-Regular", "-Bold", "-Italic",
     "-BoldItalic"},
    {"Roboto Mono", "RobotoMono", "-Regular", "-Bold", "-Italic",
     "-BoldItalic"},

    // Windows fonts (accessible under WSL via /mnt/c/Windows/Fonts/)
    {"Cascadia Mono", "CascadiaMono", "", "-Bold", "-Italic", "-BoldItalic"},
    {"Consolas", "consola", "", "b", "i", "z"},
    {"Courier New", "cour", "", "bd", "i", "bi"},
};

static const int g_known_fonts_count =
    sizeof(g_known_fonts) / sizeof(g_known_fonts[0]);

/// Look up the font-naming convention for a given family. Returns nullptr if
/// unknown.
static const FontNaming* lookup_font_naming(const std::string& family_name) {
    for (int i = 0; i < g_known_fonts_count; ++i) {
        if (family_name == g_known_fonts[i].family_name) {
            return &g_known_fonts[i];
        }
    }
    return nullptr;
}

/// Build the expected .ttf filename for a family + style combination.
static std::string build_font_filename(const std::string& family_name,
                                       bool bold, bool italic) {
    const FontNaming* naming = lookup_font_naming(family_name);
    if (!naming) {
        return "";
    }

    const char* suffix = naming->suffix_regular;
    if (bold && italic) {
        suffix = naming->suffix_bold_italic;
    } else if (bold) {
        suffix = naming->suffix_bold;
    } else if (italic) {
        suffix = naming->suffix_italic;
    }

    return std::string(naming->file_stem) + suffix + ".ttf";
}

// ---------------------------------------------------------------------------
// Directory search
// ---------------------------------------------------------------------------

/// Search for a specific filename across a set of directories.
static std::string search_dirs(const std::vector<std::string>& dirs,
                               const std::string& filename) {
    for (const auto& dir : dirs) {
        std::filesystem::path path = std::filesystem::path(dir) / filename;
        if (std::filesystem::exists(path)) {
            return path.string();
        }
    }
    return "";
}

/// Build the ordered list of directories to search for fonts.
/// @param include_wsl  If true, also include Windows font directories
///                     accessible under WSL.
static std::vector<std::string> get_font_directories(bool include_wsl) {
    std::vector<std::string> dirs = {
        // Common distro-specific directories
        "/usr/share/fonts/truetype/dejavu",
        "/usr/share/fonts/truetype/liberation",
        "/usr/share/fonts/truetype/ubuntu",
        "/usr/share/fonts/truetype/noto",
        "/usr/share/fonts/truetype/google-noto",
        "/usr/share/fonts/truetype/croscore",
        "/usr/share/fonts/truetype/firacode",
        "/usr/share/fonts/truetype/jetbrains-mono",
        "/usr/share/fonts/truetype/hack",
        "/usr/share/fonts/truetype/inconsolata",
        "/usr/share/fonts/truetype/roboto",
        "/usr/share/fonts/truetype/source-code-pro",

        // Generic directories (searched in order of specificity)
        "/usr/share/fonts/TTF",
        "/usr/share/fonts/truetype",
        "/usr/share/fonts/opentype",
        "/usr/share/fonts",

        "/usr/local/share/fonts/truetype/dejavu",
        "/usr/local/share/fonts/truetype",
        "/usr/local/share/fonts",
    };

    // User-installed fonts
    const char* home = getenv("HOME");
    if (home) {
        dirs.push_back(std::string(home) + "/.local/share/fonts");
        dirs.push_back(std::string(home) + "/.fonts");
    }

    // WSL: Windows fonts accessible via /mnt/c
    if (include_wsl) {
        dirs.push_back("/mnt/c/Windows/Fonts");
    }

    return dirs;
}

// ---------------------------------------------------------------------------
// Hardcoded font search
// ---------------------------------------------------------------------------

/// Search known directories for a font file matching family + style.
static std::string find_font_hardcoded(const std::string& family_name,
                                       bool bold, bool italic,
                                       bool include_wsl) {
    std::string filename = build_font_filename(family_name, bold, italic);
    if (filename.empty()) {
        return "";
    }

    return search_dirs(get_font_directories(include_wsl), filename);
}

// ---------------------------------------------------------------------------
// Font family fallback chain
// ---------------------------------------------------------------------------

/// Ordered list of monospace font families to try when the primary family
/// isn't available.  Order puts the best / most-likely-available fonts first.
static std::vector<std::string> get_fallback_families(bool wsl_env) {
    if (wsl_env) {
        // On WSL, Windows fonts (via /mnt/c) are often more complete than
        // whatever the distro ships.  Put them right after the Linux default.
        return {
            "DejaVu Sans Mono",
            "Cascadia Mono",
            "Consolas",
            "Courier New",
            "Liberation Mono",
            "Noto Sans Mono",
            "Ubuntu Mono",
            "Cousine",
            "Source Code Pro",
            "Fira Code",
            "Fira Mono",
            "JetBrains Mono",
            "Hack",
            "Inconsolata",
            "Roboto Mono",
        };
    }

    return {
        "DejaVu Sans Mono", "Liberation Mono",
        "Noto Sans Mono",   "Ubuntu Mono",
        "Cousine",          "Source Code Pro",
        "Fira Code",        "Fira Mono",
        "JetBrains Mono",   "Hack",
        "Inconsolata",      "Roboto Mono",
    };
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::string FontManager::find_system_font(const std::string& family_name,
                                          bool bold, bool italic) {
    bool wsl = is_wsl();
    bool is_regular = !bold && !italic;

    // Only walk the family fallback chain for the regular (non-styled)
    // lookup.  For bold / italic / bold-italic we stick to the originally
    // requested family so that all four variants come from the *same*
    // typeface — mixing families would look terrible.
    std::vector<std::string> families_to_try;
    if (is_regular) {
        families_to_try = get_fallback_families(wsl);
        // Make sure the caller's explicit choice is tried first
        if (families_to_try.empty() || families_to_try[0] != family_name) {
            families_to_try.insert(families_to_try.begin(), family_name);
        }
    } else {
        families_to_try = {family_name};
    }

    for (const auto& family : families_to_try) {
        // 1. Try fontconfig CLI
        std::string fc_pattern = family;
        if (bold && italic) {
            fc_pattern += ":Bold:Italic";
        } else if (bold) {
            fc_pattern += ":Bold";
        } else if (italic) {
            fc_pattern += ":Italic";
        }

        std::string result = fc_match(fc_pattern);
        if (!result.empty()) {
            return result;
        }

        // 2. Try hardcoded paths (including WSL Windows fonts when
        //    appropriate)
        result = find_font_hardcoded(family, bold, italic, wsl);
        if (!result.empty()) {
            return result;
        }
    }

    return "";
}

} // namespace ImApp
