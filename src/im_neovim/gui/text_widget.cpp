#include "im_neovim/gui/text_widget.h"
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
    ImGui::SetNextWindowPos(m_embedded_window_pos, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(m_embedded_window_size, ImGuiCond_FirstUseEver);

    // Ensure we never pass an empty window title to ImGui
    const char* window_title_ptr = m_window_title.c_str();
    if (m_window_title.empty()) {
        window_title_ptr = "Window";
    }

    bool window_open = true;
    bool window_created = ImGui::Begin(window_title_ptr, &window_open,
                                       ImGuiWindowFlags_NoCollapse);
    if (window_created) {
        m_embedded_window_pos = ImGui::GetWindowPos();
        m_embedded_window_size = ImGui::GetWindowSize();
        m_embedded_window_collapsed = ImGui::IsWindowCollapsed();
        if (!window_open) {
            m_is_visible = false;
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

void TextWidget::render_cell(ImDrawList* draw_list, const ScreenCell& cell,
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
    if (cell.width > 0) {
        char text[g_utf_size] = {0};
        size_t len = 0;
        for (int i = 0; i < cell.width && i < 4; i++) {
            len += utf8_encode(cell.chars[i], &text[len]);
        }
        draw_list->AddText(char_pos, ImGui::ColorConvertFloat4ToU32(fg), text);
    }

    // Draw underline
    if (cell.underline) {
        draw_list->AddLine(
            ImVec2(char_pos.x, char_pos.y + line_height - 1),
            ImVec2(char_pos.x + char_width, char_pos.y + line_height - 1),
            ImGui::ColorConvertFloat4ToU32(fg));
    }

    // Draw undercurl (wavy underline)
    if (cell.undercurl) {
        // Simple undercurl implementation - draw a wavy line
        float y = char_pos.y + line_height - 1;
        float wave_amplitude = 2.0f;
        int segments = static_cast<int>(char_width / 4.0f);
        if (segments < 2) {
            segments = 2;
        }
        for (int i = 0; i < segments; i++) {
            float x1 = char_pos.x + (i * char_width) / segments;
            float x2 = char_pos.x + ((i + 1) * char_width) / segments;
            float y_offset = (i % 2 == 0) ? -wave_amplitude : wave_amplitude;
            draw_list->AddLine(ImVec2(x1, y + y_offset),
                               ImVec2(x2, y - y_offset),
                               ImGui::ColorConvertFloat4ToU32(fg));
        }
    }
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
