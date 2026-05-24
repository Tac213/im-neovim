#include "im_neovim/gui/text_widget.h"
#include "im_app/font_manager.h"
#include "im_neovim/logging.h"
#include <algorithm>


namespace ImNeovim {

ScreenCell::ScreenCell()
    : chars{0, 0, 0, 0}, width(0), bold(false), italic(false), underline(false),
      undercurl(false), reverse(false) {
    fg = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    bg = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
}

void ScreenCell::clear() {
    chars[0] = 0;
    chars[1] = 0;
    chars[2] = 0;
    chars[3] = 0;
    width = 0;
    fg = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    bg = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    bold = false;
    italic = false;
    underline = false;
    undercurl = false;
    reverse = false;
}

TextWidget::TextWidget() : m_window_title("TextWidget") {}

TextWidget::~TextWidget() {}

bool TextWidget::setup_window() {
    if (m_is_embedded) {
        return true;
    }

    // Set dock target if we have a dock ID
    if (m_dock_id != 0) {
        ImGui::SetNextWindowDockID(m_dock_id, ImGuiCond_FirstUseEver);
    } else {
        // Fallback to floating window with saved position/size
        ImGui::SetNextWindowPos(m_embedded_window_pos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(m_embedded_window_size,
                                 ImGuiCond_FirstUseEver);
    }

    // Ensure we never pass an empty window title to ImGui
    const char* window_title_ptr = m_window_title.c_str();
    if (m_window_title.empty()) {
        window_title_ptr = "Window";
    }

    // Reset persistent open flag if the window was re-shown (e.g., after close)
    if (m_is_visible && !m_window_open) {
        m_window_open = true;
    }

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoCollapse | get_additional_window_flags();
    bool window_created = ImGui::Begin(window_title_ptr, &m_window_open, flags);
    if (window_created) {
        m_embedded_window_pos = ImGui::GetWindowPos();
        m_embedded_window_size = ImGui::GetWindowSize();
        m_embedded_window_collapsed = ImGui::IsWindowCollapsed();
        if (!m_window_open) {
            // When UnsavedDocument is set, ImGui signals close by setting
            // *p_open=false but keeps the window in the dock.  Re-open so
            // the subclass can show a save dialog.
            if (flags & ImGuiWindowFlags_UnsavedDocument) {
                m_window_open = true;
                on_close_attempted();
            } else {
                m_is_visible = false;
            }
        }
    } else {
        m_embedded_window_collapsed = true;
    }
    return window_created;
}

void TextWidget::check_font_size_changed() {
    float current_font_size = ImGui::GetFontBaked()->Size;
    if (current_font_size != m_last_font_size) {
        m_last_font_size = current_font_size;
    }
}

bool TextWidget::is_wide_char(uint32_t codepoint) {
    // East Asian double-width character detection via Unicode block ranges.
    // Covers CJK Unified Ideographs, Fullwidth Forms, Hiragana, Katakana,
    // Hangul, and CJK symbols/punctuation.
    return (codepoint >= 0x1100 && codepoint <= 0x115F) || // Hangul Jamo
           (codepoint >= 0x2329 && codepoint <= 0x232A) || // Misc Technical
           (codepoint >= 0x2E80 && codepoint <= 0x2EFF) || // CJK Rad Suppl
           (codepoint >= 0x2F00 && codepoint <= 0x2FDF) || // Kangxi Radicals
           (codepoint >= 0x2FF0 && codepoint <= 0x2FFF) || // Ideographic Desc
           (codepoint >= 0x3000 && codepoint <= 0x303F) || // CJK Symbols/Punct
           (codepoint >= 0x3040 && codepoint <= 0x309F) || // Hiragana
           (codepoint >= 0x30A0 && codepoint <= 0x30FF) || // Katakana
           (codepoint >= 0x3100 && codepoint <= 0x312F) || // Bopomofo
           (codepoint >= 0x3130 && codepoint <= 0x318F) || // Hangul Compat Jamo
           (codepoint >= 0x3190 && codepoint <= 0x31FF) || // Kanbun + Ext
           (codepoint >= 0x3200 && codepoint <= 0x32FF) || // Encl CJK
           (codepoint >= 0x3300 && codepoint <= 0x33FF) || // CJK Compat
           (codepoint >= 0x3400 && codepoint <= 0x4DBF) || // CJK Ext A
           (codepoint >= 0x4E00 && codepoint <= 0x9FFF) || // CJK Unified
           (codepoint >= 0xA000 && codepoint <= 0xA4CF) || // Yi
           (codepoint >= 0xAC00 && codepoint <= 0xD7AF) || // Hangul Syllables
           (codepoint >= 0xF900 && codepoint <= 0xFAFF) || // CJK Compat Ideo
           (codepoint >= 0xFE10 && codepoint <= 0xFE1F) || // Vertical forms
           (codepoint >= 0xFE30 && codepoint <= 0xFE4F) || // CJK Compat Forms
           (codepoint >= 0xFE50 && codepoint <= 0xFE6F) || // Small Form Var
           (codepoint >= 0xFF01 && codepoint <= 0xFF60) || // Fullwidth ASCII
           (codepoint >= 0xFFE0 && codepoint <= 0xFFE6) || // Fullwidth Signs
           (codepoint >= 0x1F200 && codepoint <= 0x1F2FF) || // Encl Ideo Suppl
           (codepoint >= 0x1F300 && codepoint <= 0x1F5FF) || // Misc Symbols
           (codepoint >= 0x1F900 && codepoint <= 0x1F9FF) || // Suppl Symbols
           (codepoint >= 0x20000 && codepoint <= 0x2FFFF) || // CJK Ext B+
           (codepoint >= 0x30000 && codepoint <= 0x3FFFF);   // CJK Ext H+
}

bool TextWidget::render_cell(ImDrawList* draw_list, const ScreenCell& cell,
                             const ImVec2& char_pos, float char_width,
                             float line_height) {
    ImVec4 fg = cell.fg;
    ImVec4 bg = cell.bg;

    // Handle reverse video
    if (cell.reverse) {
        std::swap(fg, bg);
    }

    // Draw background
    if (bg.x != 0 || bg.y != 0 || bg.z != 0) {
        draw_list->AddRectFilled(
            char_pos, ImVec2(char_pos.x + char_width, char_pos.y + line_height),
            ImGui::ColorConvertFloat4ToU32(bg));
    }

    // Draw character
    bool rendered_wide = false;
    if (cell.width > 0) {
        // Determine if the first character is wide (CJK double-width)
        bool wide_char = is_wide_char(cell.chars[0]);
        ImFont* wide_font = ImApp::FontManager::get_wide_font();

        // Select appropriate font variant
        const auto& fonts = ImApp::FontManager::get_loaded_fonts();
        ImFont* target_font = fonts.regular;

        if (wide_char && wide_font) {
            // Use the separate CJK wide font for double-width characters
            target_font = wide_font;
        } else if (cell.bold && cell.italic && fonts.bold_italic &&
                   fonts.bold_italic != fonts.regular) {
            target_font = fonts.bold_italic;
        } else if (cell.bold && fonts.bold && fonts.bold != fonts.regular) {
            target_font = fonts.bold;
        } else if (cell.italic && fonts.italic &&
                   fonts.italic != fonts.regular) {
            target_font = fonts.italic;
        }

        if (target_font && target_font != ImGui::GetFont()) {
            ImGui::PushFont(target_font);
        }

        char text[g_utf_size] = {0};
        size_t len = 0;
        for (int i = 0; i < cell.width && i < 4; i++) {
            len += utf8_encode(cell.chars[i], &text[len]);
        }

        // Wide characters span 2 cell widths
        float render_width = wide_char ? char_width * 2.0f : char_width;
        draw_list->AddText(char_pos, ImGui::ColorConvertFloat4ToU32(fg), text);

        if (target_font && target_font != ImGui::GetFont()) {
            ImGui::PopFont();
        }

        rendered_wide = wide_char;
    }

    // Draw underline
    if (cell.underline) {
        float ul_width = rendered_wide ? char_width * 2.0f : char_width;
        draw_list->AddLine(
            ImVec2(char_pos.x, char_pos.y + line_height - 1),
            ImVec2(char_pos.x + ul_width, char_pos.y + line_height - 1),
            ImGui::ColorConvertFloat4ToU32(fg));
    }

    // Draw undercurl (wavy underline)
    if (cell.undercurl) {
        float uc_width = rendered_wide ? char_width * 2.0f : char_width;
        float y = char_pos.y + line_height - 1;
        float wave_amplitude = 2.0f;
        int segments = static_cast<int>(uc_width / 4.0f);
        if (segments < 2) {
            segments = 2;
        }
        for (int i = 0; i < segments; i++) {
            float x1 = char_pos.x + (i * uc_width) / segments;
            float x2 = char_pos.x + ((i + 1) * uc_width) / segments;
            float y_offset = (i % 2 == 0) ? -wave_amplitude : wave_amplitude;
            draw_list->AddLine(ImVec2(x1, y + y_offset),
                               ImVec2(x2, y - y_offset),
                               ImGui::ColorConvertFloat4ToU32(fg));
        }
    }

    return rendered_wide;
}

void TextWidget::render_cursor(ImDrawList* draw_list, const ImVec2& cursor_pos,
                               const ScreenCell& cursor_cell, float char_width,
                               float line_height, float alpha) {
    // Determine cursor color based on dark/light mode
    // For now, use a simple gray cursor with alpha
    ImVec4 cursor_color{0.7f, 0.7f, 0.7f, alpha};

    if (cursor_cell.chars[0] != '\0') {
        char text[g_utf_size] = {0};
        size_t len = 0;
        for (int i = 0; i < cursor_cell.width && i < 4; i++) {
            len += utf8_encode(cursor_cell.chars[i], &text[len]);
        }
        ImVec4 fg = cursor_cell.fg;
        ImVec4 bg = cursor_cell.bg;

        if (cursor_cell.reverse) {
            std::swap(fg, bg);
        }

        draw_list->AddRectFilled(
            cursor_pos,
            ImVec2(cursor_pos.x + char_width, cursor_pos.y + line_height),
            ImGui::ColorConvertFloat4ToU32(cursor_color));
        draw_list->AddText(cursor_pos, ImGui::ColorConvertFloat4ToU32(fg),
                           text);
    } else {
        // Just draw cursor
        draw_list->AddRectFilled(
            cursor_pos,
            ImVec2(cursor_pos.x + char_width, cursor_pos.y + line_height),
            ImGui::ColorConvertFloat4ToU32(cursor_color));
    }
}

#define BETWEEN(x, a, b) ((a) <= (x) && (x) <= (b))

size_t TextWidget::utf8_decode(const char* c, uint32_t* u, size_t clen) {
    *u = g_utf_invalid;
    size_t len = 0;
    uint32_t udecoded = 0;

    // Determine sequence length and initial byte decoding
    if ((static_cast<unsigned char>(c[0]) & 0x80) == 0) {
        // ASCII character
        *u = static_cast<unsigned char>(c[0]);
        return 1;
    } else if ((static_cast<unsigned char>(c[0]) & 0xE0) == 0xC0) {
        // 2-byte sequence
        len = 2;
        udecoded = static_cast<unsigned char>(c[0]) & 0x1F;
    } else if ((static_cast<unsigned char>(c[0]) & 0xF0) == 0xE0) {
        // 3-byte sequence
        len = 3;
        udecoded = static_cast<unsigned char>(c[0]) & 0x0F;
    } else if ((static_cast<unsigned char>(c[0]) & 0xF8) == 0xF0) {
        // 4-byte sequence
        len = 4;
        udecoded = static_cast<unsigned char>(c[0]) & 0x07;
    } else {
        LOG_ERROR("Invalid UTF-8 start byte: 0x{:x}",
                  static_cast<int>(static_cast<unsigned char>(c[0])));
        return 0;
    }

    // Validate sequence length
    if (clen < len) {
        LOG_ERROR("Incomplete UTF-8 sequence. Expected {} bytes, got {}", len,
                  clen);
        return 0;
    }

    // Process continuation bytes
    for (size_t i = 1; i < len; i++) {
        // Validate continuation byte
        if ((static_cast<unsigned char>(c[i]) & 0xC0) != 0x80) {
            LOG_ERROR("Invalid continuation byte at position {}: 0x{:x}", i,
                      static_cast<int>(static_cast<unsigned char>(c[i])));
            return 0;
        }

        // Shift and add continuation byte
        udecoded = (udecoded << 6) | (static_cast<unsigned char>(c[i]) & 0x3F);
    }

    // Additional validation for decoded Unicode point
    auto between = [](uint32_t x, uint32_t a, uint32_t b) {
        return (a) <= (x) && (x) <= (b);
    };
    if (!BETWEEN(udecoded, g_utfmin[len], g_utfmax[len]) || // NOLINT
        BETWEEN(udecoded, 0xD800, 0xDFFF) || udecoded > 0x10FFFF) {
        LOG_ERROR("Invalid Unicode code point : U+{:x}", udecoded);
        *u = g_utf_invalid;
        return 0;
    }

    *u = udecoded;
    return len;
}

#undef BETWEEN

size_t TextWidget::utf8_encode(uint32_t u, char* c) {
    size_t len = 0;

    if (u < 0x80) {
        c[0] = static_cast<char>(u);
        len = 1;
    } else if (u < 0x800) {
        c[0] = static_cast<char>(0xC0 | (u >> 6));
        c[1] = static_cast<char>(0x80 | (u & 0x3F));
        len = 2;
    } else if (u < 0x10000) {
        c[0] = static_cast<char>(0xE0 | (u >> 12));
        c[1] = static_cast<char>(0x80 | ((u >> 6) & 0x3F));
        c[2] = static_cast<char>(0x80 | (u & 0x3F));
        len = 3;
    } else {
        c[0] = static_cast<char>(0xF0 | (u >> 18));
        c[1] = static_cast<char>(0x80 | ((u >> 12) & 0x3F));
        c[2] = static_cast<char>(0x80 | ((u >> 6) & 0x3F));
        c[3] = static_cast<char>(0x80 | (u & 0x3F));
        len = 4;
    }

    return len;
}

} // namespace ImNeovim
