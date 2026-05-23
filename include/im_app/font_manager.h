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

    /// Load a custom font family at the given size with bold/italic variants.
    /// Clears the existing font atlas and loads the requested family.
    /// If the requested family is not found, s_loaded_fonts.regular remains
    /// nullptr — the caller should revert to the previous font.
    /// Returns the loaded FontSet (check .regular for success).
    static FontSet load_font(const std::string& family, float size_px,
                             bool want_bold = false, bool want_italic = false);

    /// Load a CJK double-width font as a separate ImFont (non-merge mode).
    /// Uses East Asian glyph ranges (CJK Unified Ideographs, Fullwidth Forms,
    /// Hiragana, Katakana, Hangul).
    /// Call AFTER load_font() so both fonts share the same atlas.
    /// Returns the loaded wide font, or nullptr if not found.
    static ImFont* load_wide_font(const std::string& family, float size_px);

    /// Clear the atlas, load the regular font + optional wide font.
    /// This is the main entry point for dynamic font reload.
    /// @return true if the regular font loaded successfully.
    static bool load_font_with_wide(const std::string& family, float size_px,
                                    bool bold, bool italic,
                                    const std::string& wide_family = "",
                                    float wide_size_px = 0.0f);

    /// Platform-specific: find a system font file path by family name and
    /// style. Returns an empty string if no matching font is found.
    /// @param allow_fallback If true, may return a different family when
    ///   the requested one isn't found (used for the initial default font).
    ///   If false, returns the exact family or empty.
    static std::string find_system_font(const std::string& family_name,
                                        bool bold = false, bool italic = false,
                                        bool allow_fallback = false);

    /// Returns the default monospace font family name for the current platform.
    /// - Windows: "Cascadia Mono"
    /// - Linux:   "DejaVu Sans Mono"
    /// - macOS:   "Menlo"
    static std::string get_default_font_family();

    /// Access the most recently loaded font set.
    static const FontSet& get_loaded_fonts();

    /// Access the most recently loaded CJK wide font (nullptr if none).
    static ImFont* get_wide_font();

  private:
    static FontSet s_loaded_fonts;
    static ImFont* s_wide_font;
};

} // namespace ImApp
