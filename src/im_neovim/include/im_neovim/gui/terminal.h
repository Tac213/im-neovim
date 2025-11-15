#pragma once

#include "im_app/pty.h"
#include "imgui.h"
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <vterm.h>

namespace ImNeovim {
using uchar = unsigned char;

static constexpr size_t g_utf_size = 4;

class Terminal {
  public:
    // Common type definitions
    using Rune = uint_least32_t;

    // Glyph attributes (matching st's glyph_attribute)
    enum Attribute {
        AttrNull = 0,
        AttrBold = 1 << 0,
        AttrFaint = 1 << 1,
        AttrItalic = 1 << 2,
        AttrUnderline = 1 << 3,
        AttrBlink = 1 << 4,
        AttrReverse = 1 << 5,
        AttrInvisible = 1 << 6,
        AttrStruck = 1 << 7,
        AttrWrap = 1 << 8,
        AttrWide = 1 << 9,
        AttrWdummy = 1 << 10,
        AttrBoldFaint = AttrBold | AttrFaint,
    };

    // Terminal modes
    enum Mode {
        ModeWrap = 1 << 0,
        ModeInsert = 1 << 1,
        ModeAltscreen = 1 << 2,
        ModeCrlf = 1 << 3,
        ModeEcho = 1 << 4,
        ModePrint = 1 << 5,
        ModeUtf8 = 1 << 6,
        ModeSixel = 1 << 7,
        ModeBracketpaste = 1 << 8,
        ModeAppcursor = 1 << 9,
        ModeMousebtn = 1 << 10,
        ModeMousesgr = 1 << 11,
        ModeMouseX10 = 1 << 12,
        ModeMousemany = 1 << 13,
        ModeSmoothscroll = 1 << 14,
        ModeVisualbell = 1 << 15
    };

    struct STREscape {
        char type;                     // ESC type
        std::string buf;               // Raw string buffer
        size_t len{0};                 // Raw string length
        size_t siz{256};               // Buffer size
        std::vector<std::string> args; // Arguments
    };

    // Selection modes (matching st's selection_mode)
    enum SelectionMode {
        SelectionIdle = 0,
        SelectionEmpty = 1,
        SelectionReady = 2,
        SelectionSelecting = 3
    };

    // Selection types
    enum SelectionType { SelectionRegular = 1, SelectionRectangular = 2 };

    // Cursor states
    enum CursorState {
        CursorDefault = 0,
        CursorWrapnext = 1,
        CursorOrigin = 2
    };

    // Escape states
    enum EscapeState {
        EscStart = 1,
        EscCsi = 2,
        EscStr = 4,
        EscAltcharset = 8,
        EscStrEnd = 16,
        EscUtF8 = 64,
        EscTest = 32,
        EscAppcursor = 128
    };

    // Color modes
    enum ColorMode { ColorBasic = 0, Color256 = 1, ColorTrue = 2 };

    struct Glyph {
        Rune u{' '};                        // character code
        uint16_t mode{0};                   // attribute flags
        ImVec4 fg{1.0f, 1.0f, 1.0f, 1.0f};  // foreground color
        ImVec4 bg{0.0f, 0.0f, 0.0f, 1.0f};  // background color
        ColorMode color_mode{ColorBasic};   // Color mode
        uint32_t true_color_fg{0xFFFFFFFF}; // True color foreground
        uint32_t true_color_bg{0xFF000000}; // True color background
    };

    struct TCursor {
        int x{0};
        int y{0};
        Glyph attr;
        uint16_t attrs{0}; // For ATTR_* flags
        uint8_t state{0};
        ImVec4 fg{1.0f, 1.0f, 1.0f, 1.0f};
        ImVec4 bg{0.0f, 0.0f, 0.0f, 1.0f};
        ColorMode color_mode{ColorBasic};
        uint32_t true_color_fg{0xFFFFFFFF};
        uint32_t true_color_bg{0xFF000000};
    };

    struct Selection {
        SelectionMode mode{SelectionIdle};
        SelectionType type{SelectionRegular};
        int snap{0};
        struct {
            int x, y;
        } nb, ne, ob, oe; // normalized begin/end, original begin/end
        int alt{0};
    };

    Terminal();
    ~Terminal();

    void render();
    void resize(int cols, int rows);
    const std::string& window_title() const { return m_window_title; }
    void set_window_title(const std::string& title) { m_window_title = title; }
    bool is_visible() const { return m_is_visible; }
    void set_visible(bool visible) { m_is_visible = visible; }
    bool is_embedded() const { return m_is_embedded; }
    void set_embedded(bool embedded) { m_is_embedded = embedded; }
    void process_input(const std::string& input) const;
    bool selected_text(int x, int y);
    void paste_from_clipboard() const;

  private:
    enum Charset {
        CharsetGraphiC0,
        CharsetUk,
        CharsetUsa,
        CharsetMulti,
        CharsetGer,
        CharsetFin
    };
    void _start_shell();
    void _read_output();

    void _write_to_buffer(const char* data, size_t length);
    void _write_char(Rune u);
    void _write_glyph(const Glyph& g, int x, int y);

    // Utility functions
    void _reset();

    // Render helper functions
    void _check_font_size_changed();
    bool _setup_window();
    void _handle_terminal_resize();
    void _handle_scrollback(const ImGuiIO& io, int new_rows);
    void _handle_mouse_input(const ImGuiIO& io);
    void _handle_keyboard_input(const ImGuiIO& io) const;
    void _handle_special_keys(const ImGuiIO& io) const;
    void _handle_control_combos(const ImGuiIO& io) const;
    void _handle_regular_text_input(const ImGuiIO& io) const;

    // RenderBuffer helper functions
    void _render_buffer();
    void _render_alt_screen(ImDrawList* draw_list, const ImVec2& pos,
                            float char_width, float line_height);
    void _render_main_screen(ImDrawList* draw_list, const ImVec2& pos,
                             float char_width, float line_height);

    // Shared rendering helpers
    void _render_selection_highlight(ImDrawList* draw_list, const ImVec2& pos,
                                     float char_width, float line_height,
                                     int start_y, int end_y,
                                     int screen_offset = 0);
    void _render_cursor(ImDrawList* draw_list, const ImVec2& cursor_pos,
                        VTermScreenCell& cursor_cell, float char_width,
                        float line_height, float alpha);
    void _render_vterm_cell(ImDrawList* draw_list, VTermScreenCell& cell,
                            const ImVec2& char_pos, float char_width,
                            float line_height);
    void _handle_vterm_cell_colors(VTermScreenCell& cell, ImVec4& fg,
                                   ImVec4& bg);
    static void _render_glyph(ImDrawList* draw_list, const Glyph& glyph,
                              const ImVec2& char_pos, float char_width,
                              float line_height);
    static void _handle_glyph_colors(const Glyph& glyph, ImVec4& fg,
                                     ImVec4& bg);

    void _selection_start(int col, int row);
    void _selection_extend(int col, int row);
    void _selection_clear();
    void _get_selection(std::string& selected);
    void _copy_selection();

    // Terminal operations
    void _clear_region(int x1, int y1, int x2, int y2);
    void _scroll_up(int orig, int n);
    void _scroll_down(int orig, int n);
    void _move_to(int x, int y);
    void _set_mode(bool set, int mode);

    void _tnewline(int first_col);
    void _tstrsequence(uchar c);
    void _tmoveto(int x, int y);
    void _tmoveato(int x, int y); // Absolute move with origin mode
    void _tputtab(int n);
    void _tsetmode(int priv, int set, const std::vector<int>& args);

    void _cursor_save();
    void _cursor_load();

    void _add_to_scrollback(const std::vector<Glyph>& line);
    void _add_to_scrollback(int cols, const VTermScreenCell* cells);
    int _pop_from_scrollback(int cols, VTermScreenCell* cells);
    void _scrollback_clear();

    struct CSIEscape {
        char buf[256];         // Raw string
        size_t len;            // Raw string length
        char priv;             // Private mode
        std::vector<int> args; // Arguments
        char mode[2];          // Final character(s)
    };

    static void _parse_csi_param(CSIEscape& csi);
    void _handle_csi(const CSIEscape& csi);

    // Sequence handling
    void _handle_sgr(const std::vector<int>& args);
    void _handle_control_code(uchar c);

    int _eschandle(uchar ascii);
    void _handle_dcs() const; // DSC - Device Control String
    void _selection_normalize();
    void _strparse();
    void _handle_string_sequence();
    // OSC - Operating System Command
    void _handle_osc_color(const std::vector<std::string>& args) const;
    void _handle_osc_selection(const std::vector<std::string>& args);

    void _ring_bell();

    // UTF-8 handling
    static size_t _utf8_decode(const char* c, Rune* u, size_t clen);
    static size_t _utf8_encode(Rune u, char* c);

    static constexpr const uchar g_utfmask[5] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8};
    static constexpr const Rune g_utfmin[5] = {0, 0, 0x80, 0x800, 0x10000};
    static constexpr const Rune g_utfmax[5] = {0x10FFFF, 0x7F, 0x7FF, 0xFFFF,
                                               0x10FFFF};
    static const Rune g_utf_invalid = 0xFFFD; // Unicode replacement character

    // vterm callback
    VTermScreenCallbacks m_vterm_screen_callbacks;
    static int _vterm_settermprop(VTermProp prop, VTermValue* val, void* data);
    static int _vterm_damage(VTermRect rect, void* data);
    static int _vterm_moverect(VTermRect dest, VTermRect src, void* data);
    static int _vterm_movecursor(VTermPos new_pos, VTermPos old_pos,
                                 int visible, void* data);
    static int _vterm_bell(void* data);
    static int _vterm_sb_pushline(int cols, const VTermScreenCell* cells,
                                  void* data);
    static int _vterm_sb_popline(int cols, VTermScreenCell* cells, void* data);
    static int _vterm_sb_clear(void* data);
    static void _vterm_output(const char* s, size_t len, void* data);

    // Terminal state
    struct TermState {
        TCursor c;                          // Current cursor
        int row{0};                         // number of rows
        int col{0};                         // number of columns
        int top{0};                         // scroll region top
        int bot{0};                         // scroll region bottom
        int icharset{0};                    // Selected charset for sequence
        uint32_t mode{ModeWrap | ModeUtf8}; // terminal mode flags
        int esc{0};                         // escape state flags
        char trantbl[4]{};                  // charset table translation
        int charset{0};                     // current charset
        Rune lastc{0}; // last printed char outside of sequence
        std::vector<std::vector<Glyph>> lines;     // screen
        std::vector<std::vector<Glyph>> alt_lines; // alternate screen
        std::vector<bool> dirty;                   // dirtyness of lines
        std::vector<bool> tabs;                    // Tab stops
    } m_state;
    bool m_dark_mode = true;

    static constexpr float g_drag_threshold = 3.0f;
    Selection m_selection;

    std::string m_window_title;
    bool m_is_visible{true};
    bool m_is_embedded{false};

    // Embedded terminal window state
    ImVec2 m_embedded_window_pos{100.0f, 100.0f};
    ImVec2 m_embedded_window_size{800.0f, 400.0f};
    bool m_embedded_window_collapsed{false};

    // Thread and synchronization
    std::mutex m_buffer_mutex;
    std::thread m_read_thread;
    bool m_should_terminate{false};

    // PTY information
    std::shared_ptr<ImApp::PseudoTerminal> m_pty{nullptr};

    // libvterm related
    VTerm* m_vterm{nullptr};
    VTermScreen* m_vterm_screen{nullptr};

    float m_last_font_size = 0;

    TCursor m_saved_cursor; // For cursor save/restore

    std::vector<std::vector<Glyph>> m_scrollback_buffer;
    std::vector<std::vector<VTermScreenCell>> m_sb_buffer;
    size_t m_max_scrollback_lines = 10000;
    int m_scroll_offset = 0;

    CSIEscape m_csiescseq;
    STREscape m_strescseq;

    ImVec4 m_default_color_map[16] = {
        // Standard colors
        ImVec4(0.0f, 0.0f, 0.0f, 1.0f), // Black
        ImVec4(0.8f, 0.2f, 0.2f, 1.0f), // Rich Red
        ImVec4(0.2f, 0.8f, 0.2f, 1.0f), // Vibrant Green
        ImVec4(0.9f, 0.9f, 0.3f, 1.0f), // Sunny Yellow
        ImVec4(0.2f, 0.5f, 1.0f, 1.0f), // Sky Blue (brighter blue)
        ImVec4(0.8f, 0.3f, 0.8f, 1.0f), // Electric Purple
        ImVec4(0.3f, 0.8f, 0.8f, 1.0f), // Aqua Cyan
        ImVec4(0.9f, 0.9f, 0.9f, 1.0f), // Off-White

        // Bright colors (pastel-like but still vibrant)
        ImVec4(0.5f, 0.5f, 0.5f, 1.0f), // Medium Gray
        ImVec4(1.0f, 0.4f, 0.4f, 1.0f), // Coral Red
        ImVec4(0.4f, 1.0f, 0.4f, 1.0f), // Lime Green
        ImVec4(1.0f, 1.0f, 0.6f, 1.0f), // Lemon Yellow
        ImVec4(0.4f, 0.6f, 1.0f, 1.0f), // Bright Sky Blue
        ImVec4(1.0f, 0.5f, 1.0f, 1.0f), // Pink Purple
        ImVec4(0.5f, 1.0f, 1.0f, 1.0f), // Ice Blue
        ImVec4(1.0f, 1.0f, 1.0f, 1.0f)  // Pure White
    };

    const std::unordered_map<Rune, Rune> m_box_drawing_chars = {
        // Basic box drawing - map proper Unicode box chars to themselves
        {0x2500, L'─'}, // HORIZONTAL LINE
        {0x2502, L'│'}, // VERTICAL LINE
        {0x250C, L'┌'}, // DOWN AND RIGHT
        {0x2510, L'┐'}, // DOWN AND LEFT
        {0x2514, L'└'}, // UP AND RIGHT
        {0x2518, L'┘'}, // UP AND LEFT
        {0x251C, L'├'}, // VERTICAL AND RIGHT
        {0x2524, L'┤'}, // VERTICAL AND LEFT
        {0x252C, L'┬'}, // DOWN AND HORIZONTAL
        {0x2534, L'┴'}, // UP AND HORIZONTAL
        {0x253C, L'┼'}, // VERTICAL AND HORIZONTAL

        // Double-line variants
        {0x2550, L'═'}, // DOUBLE HORIZONTAL
        {0x2551, L'║'}, // DOUBLE VERTICAL
        {0x2554, L'╔'}, // DOUBLE DOWN AND RIGHT
        {0x2557, L'╗'}, // DOUBLE DOWN AND LEFT
        {0x255A, L'╚'}, // DOUBLE UP AND RIGHT
        {0x255D, L'╝'}, // DOUBLE UP AND LEFT

        // Rounded corners (often used in btop)
        {0x256D, L'╭'}, // ROUNDED DOWN AND RIGHT
        {0x256E, L'╮'}, // ROUNDED DOWN AND LEFT
        {0x256F, L'╯'}, // ROUNDED UP AND LEFT
        {0x2570, L'╰'}, // ROUNDED UP AND RIGHT

        // Block elements
        {0x2588, L'█'}, // FULL BLOCK
        {0x2591, L'░'}, // LIGHT SHADE
        {0x2592, L'▒'}, // MEDIUM SHADE
        {0x2593, L'▓'}, // DARK SHADE
        {0x2584, L'▄'}, // LOWER HALF BLOCK
        {0x2580, L'▀'}, // UPPER HALF BLOCK
        {0x2581, L'▁'}, // Lower one eighth block
        {0x2582, L'▂'}, // Lower one quarter block
        {0x2583, L'▃'}, // Lower three eighths block
        {0x2584, L'▄'}, // Lower half block
        {0x2585, L'▅'}, // Lower five eighths block
        {0x2586, L'▆'}, // Lower three quarters block
        {0x2587, L'▇'}, // Lower seven eighths block
        {0x2588, L'█'}, // Full block
        {0x2591, L'░'}, // Light shade
        {0x2592, L'▒'}, // Medium shade
        {0x2593, L'▓'}, // Dark shade

        {0x2500, L'─'}, // Existing mappings...

        // Add Braille patterns (0x28c0 is appearing a lot in the output)
        {0x28c0, L'⣀'}, // BRAILLE PATTERN DOTS-78
        {0x28c1, L'⣁'}, // BRAILLE PATTERN DOTS-1-78
        {0x28c2, L'⣂'}, // BRAILLE PATTERN DOTS-2-78
        {0x28c3, L'⣃'}, // BRAILLE PATTERN DOTS-12-78

        // Block elements
        {0x2588, L'█'}, // FULL BLOCK (this one appears in your output)
        {0x2589, L'▉'}, // LEFT SEVEN EIGHTHS BLOCK
        {0x258A, L'▊'}, // LEFT THREE QUARTERS BLOCK
        {0x258B, L'▋'}, // LEFT FIVE EIGHTHS BLOCK
        {0x258C, L'▌'}, // LEFT HALF BLOCK
        {0x258D, L'▍'}, // LEFT THREE EIGHTHS BLOCK
        {0x258E, L'▎'}, // LEFT ONE QUARTER BLOCK
        {0x258F, L'▏'}, // LEFT ONE EIGHTH BLOCK
        {0x2589, L'▉'},
        {0x258A, L'▊'},
        {0x258B, L'▋'},
        {0x258C, L'▌'},
        {0x258D, L'▍'},
        {0x258E, L'▎'},
        {0x258F, L'▏'},
        {0x2840, 0x2840}, // ⡀
        {0x2880, 0x2880}, // ⢀
        {0x28c0, 0x28c0}, // ⣀
    };
};
} // namespace ImNeovim
