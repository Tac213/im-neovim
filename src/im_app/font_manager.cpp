#include "im_app/font_manager.h"

#include "imgui.h"
#include "misc/freetype/imgui_freetype.h"

#include <spdlog/spdlog.h>

namespace ImApp {

FontSet FontManager::s_loaded_fonts;
ImFont* FontManager::s_wide_font = nullptr;

static ImFont* load_font_file(ImFontAtlas* atlas, const std::string& file_path,
                              float size_px, ImFontConfig& config) {
    if (file_path.empty()) {
        return nullptr;
    }

    ImFont* font =
        atlas->AddFontFromFileTTF(file_path.c_str(), size_px, &config);
    if (!font) {
        spdlog::warn("FontManager: failed to load font from '{}'", file_path);
    } else {
        spdlog::info("FontManager: loaded font from '{}'", file_path);
    }
    return font;
}

std::string FontManager::get_default_font_family() {
#if defined(IM_APP_WIN32)
    return "Cascadia Mono";
#elif defined(IM_APP_LINUX)
    return "DejaVu Sans Mono";
#elif defined(IM_APP_DARWIN)
    return "Menlo";
#else
    return "";
#endif
}

FontSet FontManager::load_default_font(ImFontAtlas* atlas, float size_px) {
    s_loaded_fonts = FontSet{};

    const std::string family = get_default_font_family();
    if (family.empty()) {
        spdlog::warn("FontManager: no default font family for this platform");
        return s_loaded_fonts;
    }

    spdlog::info("FontManager: loading default font family '{}' at {}px",
                 family, size_px);

    // Common ImFontConfig for FreeType rendering
    ImFontConfig config;
    config.FontLoaderFlags = ImGuiFreeTypeBuilderFlags_LightHinting;

    // Find font files for each variant (allow fallback for the default font)
    std::string regular_path = find_system_font(family, false, false, true);
    std::string bold_path = find_system_font(family, true, false, true);
    std::string italic_path = find_system_font(family, false, true, true);
    std::string bold_italic_path = find_system_font(family, true, true, true);

    // If no variants found at all, try the base name without style
    if (regular_path.empty() && bold_path.empty() && italic_path.empty() &&
        bold_italic_path.empty()) {
        spdlog::warn(
            "FontManager: no font files found for family '{}' on this system",
            family);
        return s_loaded_fonts;
    }

    // Load regular
    if (!regular_path.empty()) {
        s_loaded_fonts.regular =
            load_font_file(atlas, regular_path, size_px, config);
    }

    // Load bold — merge with regular config to share glyph ranges
    if (!bold_path.empty()) {
        ImFontConfig bold_config = config;
        bold_config.MergeMode = true;
        bold_config.GlyphOffset.y = 0.0f;
        s_loaded_fonts.bold =
            load_font_file(atlas, bold_path, size_px, bold_config);
    } else {
        // No separate bold file; ImGui can synthesize bold from regular
        s_loaded_fonts.bold = s_loaded_fonts.regular;
    }

    // Load italic
    if (!italic_path.empty()) {
        ImFontConfig italic_config = config;
        italic_config.MergeMode = true;
        italic_config.GlyphOffset.y = 0.0f;
        s_loaded_fonts.italic =
            load_font_file(atlas, italic_path, size_px, italic_config);
    } else {
        s_loaded_fonts.italic = s_loaded_fonts.regular;
    }

    // Load bold-italic
    if (!bold_italic_path.empty()) {
        ImFontConfig bi_config = config;
        bi_config.MergeMode = true;
        bi_config.GlyphOffset.y = 0.0f;
        s_loaded_fonts.bold_italic =
            load_font_file(atlas, bold_italic_path, size_px, bi_config);
    } else {
        // Fall back to bold or italic, whichever exists
        if (s_loaded_fonts.bold &&
            s_loaded_fonts.bold != s_loaded_fonts.regular) {
            s_loaded_fonts.bold_italic = s_loaded_fonts.bold;
        } else if (s_loaded_fonts.italic &&
                   s_loaded_fonts.italic != s_loaded_fonts.regular) {
            s_loaded_fonts.bold_italic = s_loaded_fonts.italic;
        } else {
            s_loaded_fonts.bold_italic = s_loaded_fonts.regular;
        }
    }

    if (!s_loaded_fonts.regular) {
        spdlog::error(
            "FontManager: failed to load regular font for family '{}'", family);
    }

    return s_loaded_fonts;
}

FontSet FontManager::load_font(const std::string& family, float size_px,
                               bool want_bold, bool want_italic) {
    // Clear the existing atlas so we can load a fresh font set.
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    atlas->Clear();

    s_loaded_fonts = FontSet{};
    s_wide_font = nullptr;

    if (family.empty()) {
        spdlog::warn("FontManager::load_font: empty family name");
        return s_loaded_fonts;
    }

    spdlog::info("FontManager: loading font family '{}' at {}px (bold={}, "
                 "italic={})",
                 family, size_px, want_bold, want_italic);

    ImFontConfig config;
    config.FontLoaderFlags = ImGuiFreeTypeBuilderFlags_LightHinting;

    // Find font files for each variant (exact match -- no fallback).
    // The guifont comma-separated list handles fallback at a higher level.
    std::string regular_path = find_system_font(family, false, false, false);
    std::string bold_path =
        want_bold ? find_system_font(family, true, false, false) : "";
    std::string italic_path =
        want_italic ? find_system_font(family, false, true, false) : "";
    std::string bold_italic_path =
        (want_bold && want_italic) ? find_system_font(family, true, true, false)
                                   : "";

    // If no variants found at all, return with nullptr regular.
    if (regular_path.empty() && bold_path.empty() && italic_path.empty() &&
        bold_italic_path.empty()) {
        spdlog::warn("FontManager: no font files found for family '{}'",
                     family);
        return s_loaded_fonts;
    }

    // Load regular
    if (!regular_path.empty()) {
        s_loaded_fonts.regular =
            load_font_file(atlas, regular_path, size_px, config);
    }

    // Load bold — merge mode to share glyph ranges
    if (!bold_path.empty() && s_loaded_fonts.regular) {
        ImFontConfig bold_config = config;
        bold_config.MergeMode = true;
        bold_config.GlyphOffset.y = 0.0f;
        s_loaded_fonts.bold =
            load_font_file(atlas, bold_path, size_px, bold_config);
    } else {
        s_loaded_fonts.bold = s_loaded_fonts.regular;
    }

    // Load italic
    if (!italic_path.empty() && s_loaded_fonts.regular) {
        ImFontConfig italic_config = config;
        italic_config.MergeMode = true;
        italic_config.GlyphOffset.y = 0.0f;
        s_loaded_fonts.italic =
            load_font_file(atlas, italic_path, size_px, italic_config);
    } else {
        s_loaded_fonts.italic = s_loaded_fonts.regular;
    }

    // Load bold-italic
    if (!bold_italic_path.empty() && s_loaded_fonts.regular) {
        ImFontConfig bi_config = config;
        bi_config.MergeMode = true;
        bi_config.GlyphOffset.y = 0.0f;
        s_loaded_fonts.bold_italic =
            load_font_file(atlas, bold_italic_path, size_px, bi_config);
    } else {
        if (s_loaded_fonts.bold &&
            s_loaded_fonts.bold != s_loaded_fonts.regular) {
            s_loaded_fonts.bold_italic = s_loaded_fonts.bold;
        } else if (s_loaded_fonts.italic &&
                   s_loaded_fonts.italic != s_loaded_fonts.regular) {
            s_loaded_fonts.bold_italic = s_loaded_fonts.italic;
        } else {
            s_loaded_fonts.bold_italic = s_loaded_fonts.regular;
        }
    }

    if (!s_loaded_fonts.regular) {
        spdlog::error("FontManager: failed to load regular font for family "
                      "'{}' at {}px",
                      family, size_px);
    }

    return s_loaded_fonts;
}

// Build CJK glyph ranges covering East Asian double-width characters
// within the Basic Multilingual Plane (BMP, U+0000–U+FFFF).
// Supplementary-plane CJK (Extension B–F, U+20000+) requires
// ImFontGlyphRangesBuilder with actual text — not included here.
static const ImWchar* get_cjk_glyph_ranges() {
    static const ImWchar s_cjk_ranges[] = {
        0x0020, 0x00FF, // Basic Latin + Latin-1 Supplement
        0x1100, 0x115F, // Hangul Jamo
        0x2000, 0x206F, // General Punctuation
        0x2329, 0x232A, // Misc Technical (wide angle brackets)
        0x2E80, 0x2EFF, // CJK Radicals Supplement
        0x2F00, 0x2FDF, // Kangxi Radicals
        0x2FF0, 0x2FFF, // Ideographic Description Characters
        0x3000, 0x303F, // CJK Symbols and Punctuation
        0x3040, 0x309F, // Hiragana
        0x30A0, 0x30FF, // Katakana
        0x3100, 0x312F, // Bopomofo
        0x3130, 0x318F, // Hangul Compatibility Jamo
        0x3190, 0x31FF, // Kanbun + Katakana Phonetic Extensions
        0x3200, 0x32FF, // Enclosed CJK Letters and Months
        0x3300, 0x33FF, // CJK Compatibility
        0x3400, 0x4DBF, // CJK Unified Ideographs Extension A
        0x4E00, 0x9FFF, // CJK Unified Ideographs
        0xA000, 0xA4CF, // Yi Syllables
        0xAC00, 0xD7AF, // Hangul Syllables
        0xF900, 0xFAFF, // CJK Compatibility Ideographs
        0xFE10, 0xFE1F, // Vertical Forms
        0xFE30, 0xFE4F, // CJK Compatibility Forms
        0xFE50, 0xFE6F, // Small Form Variants
        0xFF01, 0xFF60, // Fullwidth ASCII variants
        0xFFE0, 0xFFE6, // Fullwidth Signs
        0,
    };
    return s_cjk_ranges;
}

ImFont* FontManager::load_wide_font(const std::string& family, float size_px) {
    if (family.empty()) {
        return nullptr;
    }

    // Must have a regular font loaded first to share the atlas.
    if (!s_loaded_fonts.regular) {
        spdlog::warn("FontManager::load_wide_font: no regular font loaded, "
                     "cannot add wide font to empty atlas");
        return nullptr;
    }

    std::string wide_path = find_system_font(family, false, false, false);
    if (wide_path.empty()) {
        spdlog::warn("FontManager: wide font family '{}' not found", family);
        return nullptr;
    }

    ImFontAtlas* atlas = ImGui::GetIO().Fonts;

    ImFontConfig config;
    config.FontLoaderFlags = ImGuiFreeTypeBuilderFlags_LightHinting;
    config.MergeMode = false; // Separate font — caller switches explicitly

    // Restrict glyph ranges to CJK to keep the atlas compact.
    // Without a range restriction, the wide font would try to load all
    // glyphs including ASCII, bloating the atlas.
    config.GlyphRanges = get_cjk_glyph_ranges();

    s_wide_font =
        atlas->AddFontFromFileTTF(wide_path.c_str(), size_px, &config);
    if (!s_wide_font) {
        spdlog::error("FontManager: failed to load wide font from '{}'",
                      wide_path);
    } else {
        spdlog::info("FontManager: loaded wide font '{}' at {}px", wide_path,
                     size_px);
    }

    return s_wide_font;
}

bool FontManager::load_font_with_wide(const std::string& family, float size_px,
                                      bool bold, bool italic,
                                      const std::string& wide_family,
                                      float wide_size_px) {
    FontSet result = load_font(family, size_px, bold, italic);
    if (!result.regular) {
        return false;
    }

    if (!wide_family.empty()) {
        float wide_sz = wide_size_px > 0.0f ? wide_size_px : size_px;
        load_wide_font(wide_family, wide_sz);
        // Wide font failure is non-fatal — we just don't have CJK support.
    }

    return true;
}

const FontSet& FontManager::get_loaded_fonts() { return s_loaded_fonts; }

ImFont* FontManager::get_wide_font() { return s_wide_font; }

} // namespace ImApp
