#include "im_app/font_manager.h"

#include "imgui.h"
#include "misc/freetype/imgui_freetype.h"

#include <spdlog/spdlog.h>

namespace ImApp {

FontSet FontManager::s_loaded_fonts;

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

    // Find font files for each variant
    std::string regular_path = find_system_font(family, false, false);
    std::string bold_path = find_system_font(family, true, false);
    std::string italic_path = find_system_font(family, false, true);
    std::string bold_italic_path = find_system_font(family, true, true);

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

const FontSet& FontManager::get_loaded_fonts() { return s_loaded_fonts; }

} // namespace ImApp
