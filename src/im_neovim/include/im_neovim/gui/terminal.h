#pragma once

#include "im_app/pty.h"
#include "im_neovim/gui/text_widget.h"
#include "imgui.h"
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <vterm.h>

namespace ImNeovim {
using uchar = unsigned char;

static constexpr size_t g_utf_size = 4;

// Minimum usable terminal dimensions to prevent the shell from being
// launched into a tiny content area before the ImGui layout stabilizes.
static constexpr int g_min_term_cols = 10;
static constexpr int g_min_term_rows = 5;

class Terminal : public TextWidget {
  public:
    // Common type definitions
    using Rune = uint_least32_t;

    // Terminal modes
    enum Mode {
        ModeWrap = 1 << 0,
        ModeUtf8 = 1 << 1,
        ModeAltscreen = 1 << 2,
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

    struct TCursor {
        int x{0};
        int y{0};
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
    std::string im_window_name() const { return m_window_title; }
    bool is_visible() const { return m_is_visible; }
    void set_visible(bool visible) { m_is_visible = visible; }
    bool is_embedded() const { return m_is_embedded; }
    void set_embedded(bool embedded) { m_is_embedded = embedded; }
    void process_input(const std::string& input) const;
    bool selected_text(int x, int y);
    void paste_from_clipboard() const;

  private:
    void _start_shell();
    void _read_output();

    void _write_to_buffer(const char* data, size_t length);

    // Render helper functions
    void _check_font_size_changed();
    void _handle_terminal_resize();
    void _handle_scrollback(const ImGuiIO& io, int new_rows);
    void _handle_mouse_input(const ImGuiIO& io);
    void _handle_keyboard_input(const ImGuiIO& io) const;

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

    // Helper to convert VTermScreenCell to ScreenCell
    void _vterm_cell_to_screen_cell(VTermScreenCell& vterm_cell,
                                    ScreenCell& screen_cell);

    void _selection_start(int col, int row);
    void _selection_extend(int col, int row);
    void _selection_clear();
    void _get_selection(std::string& selected);
    void _copy_selection();

    // Terminal operations
    void _clear_region(int x1, int y1, int x2, int y2);
    void _move_to(int x, int y);
    void _set_mode(bool set, int mode);

    void _cursor_save();
    void _cursor_load();

    void _add_to_scrollback(int cols, const VTermScreenCell* cells);
    int _pop_from_scrollback(int cols, VTermScreenCell* cells);
    void _scrollback_clear();

    void _selection_normalize();

    void _ring_bell() const;

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
        uint32_t mode{ModeWrap | ModeUtf8}; // terminal mode flags
        std::vector<bool> dirty;            // dirtyness of lines
    } m_state;
    bool m_dark_mode = true;

    static constexpr float g_drag_threshold = 3.0f;
    Selection m_selection;

    // Thread and synchronization
    std::mutex m_buffer_mutex;
    std::thread m_read_thread;
    bool m_should_terminate{false};

    // PTY information
    std::shared_ptr<ImApp::PseudoTerminal> m_pty{nullptr};

    // libvterm related
    VTerm* m_vterm{nullptr};
    VTermScreen* m_vterm_screen{nullptr};

    TCursor m_saved_cursor; // For cursor save/restore

    std::vector<std::vector<VTermScreenCell>> m_sb_buffer;
    size_t m_max_scrollback_lines = 10000;
    int m_scroll_offset = 0;
};
} // namespace ImNeovim
