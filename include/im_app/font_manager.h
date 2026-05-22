#pragma once

#include <string>

struct ImFont;
struct ImFontAtlas;

namespace ImApp {

/// Holds the four loaded font variants (regular, bold, italic, bold-italic).
struct FontSet {
    ImFont* regular = nullptr;
    ImFont* bold = nullptr;
    ImFont* italic = nullptr;
    ImFont* bold_italic = nullptr;
};

/// Cross-platform system font manager.
/// Discovers and loads the platform's default monospace font.
struct FontManager {
    /// Load the hardcoded default monospace font for the current platform
    /// with all four variants (regular, bold, italic, bold-italic).
    /// Returns a FontSet with the loaded fonts, or all-nullptr on failure.
    /// Caller should fall back to atlas->AddFontDefault() if regular is
    /// nullptr.
    static FontSet load_default_font(ImFontAtlas* atlas, float size_px = 14.0f);

    /// Platform-specific: find a system font file path by family name and
    /// style. Returns an empty string if no matching font is found.
    static std::string find_system_font(const std::string& family_name,
                                        bool bold = false, bool italic = false);

    /// Returns the default monospace font family name for the current platform.
    /// - Windows: "Cascadia Mono"
    /// - Linux:   "DejaVu Sans Mono"
    /// - macOS:   "Menlo"
    static std::string get_default_font_family();

    /// Access the most recently loaded font set.
    static const FontSet& get_loaded_fonts();

  private:
    static FontSet s_loaded_fonts;
};

} // namespace ImApp
