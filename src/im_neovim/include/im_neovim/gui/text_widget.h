#pragma once

#include "imgui.h"
#include <cstdint>
#include <string>

namespace ImNeovim {

// ScreenCell - a common cell type that works for both Terminal and NvimWidget
struct ScreenCell {
    // Character storage (UTF-32)
    uint32_t chars[4];
    int width;

    // Colors
    ImVec4 fg;
    ImVec4 bg;

    // Attributes
    bool bold : 1;
    bool italic : 1;
    bool underline : 1;
    bool undercurl : 1;
    bool reverse : 1;

    ScreenCell();
    void clear();
};

class TextWidget {
  public:
    TextWidget();
    virtual ~TextWidget();

    // Public interface
    virtual void render() = 0;
    const std::string& window_title() const { return m_window_title; }
    void set_window_title(const std::string& title) { m_window_title = title; }
    bool is_visible() const { return m_is_visible; }
    void set_visible(bool visible) { m_is_visible = visible; }
    bool is_embedded() const { return m_is_embedded; }
    void set_embedded(bool embedded) { m_is_embedded = embedded; }

  protected:
    // Common window management
    bool setup_window();
    void check_font_size_changed();

    // Common rendering helpers
    static void render_cell(ImDrawList* draw_list, const ScreenCell& cell,
                            const ImVec2& char_pos, float char_width,
                            float line_height);
    static void render_cursor(ImDrawList* draw_list, const ImVec2& cursor_pos,
                              const ScreenCell& cursor_cell, float char_width,
                              float line_height, float alpha);

    // UTF-8 utilities
    static size_t utf8_decode(const char* c, uint32_t* u, size_t clen);
    static size_t utf8_encode(uint32_t u, char* c);

    // Common state
    std::string m_window_title;
    bool m_is_visible{true};
    bool m_is_embedded{false};

    // Embedded window state
    ImVec2 m_embedded_window_pos{100.0f, 100.0f};
    ImVec2 m_embedded_window_size{800.0f, 400.0f};
    bool m_embedded_window_collapsed{false};

    float m_last_font_size = 0;

    // Default color map (16 ANSI colors)
    ImVec4 m_default_color_map[16] = {
        // Standard colors
        ImVec4(0.0f, 0.0f, 0.0f, 1.0f), // Black
        ImVec4(0.8f, 0.2f, 0.2f, 1.0f), // Rich Red
        ImVec4(0.2f, 0.8f, 0.2f, 1.0f), // Vibrant Green
        ImVec4(0.9f, 0.9f, 0.3f, 1.0f), // Sunny Yellow
        ImVec4(0.2f, 0.5f, 1.0f, 1.0f), // Sky Blue
        ImVec4(0.8f, 0.3f, 0.8f, 1.0f), // Electric Purple
        ImVec4(0.3f, 0.8f, 0.8f, 1.0f), // Aqua Cyan
        ImVec4(0.9f, 0.9f, 0.9f, 1.0f), // Off-White

        // Bright colors
        ImVec4(0.5f, 0.5f, 0.5f, 1.0f), // Medium Gray
        ImVec4(1.0f, 0.4f, 0.4f, 1.0f), // Coral Red
        ImVec4(0.4f, 1.0f, 0.4f, 1.0f), // Lime Green
        ImVec4(1.0f, 1.0f, 0.6f, 1.0f), // Lemon Yellow
        ImVec4(0.4f, 0.6f, 1.0f, 1.0f), // Bright Sky Blue
        ImVec4(1.0f, 0.5f, 1.0f, 1.0f), // Pink Purple
        ImVec4(0.5f, 1.0f, 1.0f, 1.0f), // Ice Blue
        ImVec4(1.0f, 1.0f, 1.0f, 1.0f)  // Pure White
    };

    static constexpr size_t g_utf_size = 4;
    static constexpr unsigned char g_utfmask[5] = {0xC0, 0x80, 0xE0, 0xF0,
                                                   0xF8};
    static constexpr uint32_t g_utfmin[5] = {0, 0, 0x80, 0x800, 0x10000};
    static constexpr uint32_t g_utfmax[5] = {0x10FFFF, 0x7F, 0x7FF, 0xFFFF,
                                             0x10FFFF};
    static const uint32_t g_utf_invalid = 0xFFFD;
};

} // namespace ImNeovim
