#include "im_neovim/gui/terminal.h"
#include "im_neovim/logging.h"
#include <fmt/ranges.h>
#include <type_traits>

namespace ImNeovim {
#define BETWEEN(x, a, b) ((a) <= (x) && (x) <= (b))
#define MODBIT(x, set, bit) ((set) ? ((x) |= (bit)) : ((x) &= ~(bit)))
#define ISCONTROLC0(c) (BETWEEN(c, 0, 0x1f) || (c) == 0x7f)
#define ISCONTROLC1(c) (BETWEEN(c, 0x80, 0x9f))
#define ISCONTROL(c) (ISCONTROLC0(c) || ISCONTROLC1(c))

Terminal::Terminal() : m_window_title("Terminal"), m_dark_mode(true) {
    m_pty = ImApp::PseudoTerminal::create();

    // Initialize with safe default size
    m_state.row = 24;
    m_state.col = 80;
    m_state.bot = m_state.row - 1;
    m_selection.mode = SelectionIdle;
    m_selection.type = SelectionRegular;
    m_selection.snap = 0;
    m_selection.ob.x = -1;
    m_selection.ob.y = -1;
    m_selection.oe.x = -1;
    m_selection.oe.y = -1;
    m_selection.nb.x = -1;
    m_selection.nb.y = -1;
    m_selection.ne.x = -1;
    m_selection.ne.y = -1;
    m_selection.alt = 0;
    // Initialize screen buffer
    m_state.lines.resize(m_state.row, std::vector<Glyph>(m_state.col));
    m_state.alt_lines.resize(m_state.row, std::vector<Glyph>(m_state.col));
    m_state.dirty.resize(m_state.row, true);

    // Initialize tab stops (every 8 columns)
    m_state.tabs.resize(m_state.col, false);
    for (int i = 8; i < m_state.col; i += 8) {
        m_state.tabs[i] = true;
    }

    // Create VTerm instance
    m_vterm = vterm_new(m_state.row, m_state.col);
    vterm_set_utf8(m_vterm, 1);
    // Get screen and set up callbacks BEFORE enabling
    m_vterm_screen = vterm_obtain_screen(m_vterm);
    vterm_screen_enable_altscreen(m_vterm_screen, 1);
    vterm_screen_enable_reflow(m_vterm_screen, true);

    m_vterm_screen_callbacks.damage = _vterm_damage;
    m_vterm_screen_callbacks.moverect = _vterm_moverect;
    m_vterm_screen_callbacks.movecursor = _vterm_movecursor;
    m_vterm_screen_callbacks.settermprop = _vterm_settermprop;
    m_vterm_screen_callbacks.bell = _vterm_bell;
    m_vterm_screen_callbacks.sb_pushline = _vterm_sb_pushline;
    m_vterm_screen_callbacks.sb_popline = _vterm_sb_popline;
    m_vterm_screen_callbacks.sb_clear = _vterm_sb_clear;
    vterm_screen_set_callbacks(m_vterm_screen, &m_vterm_screen_callbacks, this);

    vterm_screen_set_damage_merge(m_vterm_screen, VTERM_DAMAGE_SCROLL);
    vterm_screen_reset(m_vterm_screen, 1);
    vterm_output_set_callback(m_vterm, _vterm_output, this);
}

Terminal::~Terminal() {
    m_should_terminate = true;
    /* We need to write somethting into the `m_pty`,
     * otherwise the `m_read_thread` can't be joined.
     */
    process_input("exit\r");
    if (m_read_thread.joinable()) {
        m_read_thread.join();
    }
    if (m_vterm) {
        vterm_free(m_vterm);
    }
    m_vterm = nullptr;
    m_vterm_screen = nullptr;
    if (m_pty->is_valid()) {
        m_pty->terminate();
    }
    m_pty.reset();
}

void Terminal::render() {
    if (!m_is_visible) {
        return;
    }
    if (!m_pty->is_valid()) {
        _start_shell();
    }

    _check_font_size_changed();
    bool window_created = _setup_window();

    // Only render terminal content if window is open and not collapsed
    if (window_created && (m_is_embedded || !m_embedded_window_collapsed)) {
        ImGuiIO& io = ImGui::GetIO();
        _handle_terminal_resize();
        _render_buffer();
        _handle_scrollback(io, m_state.row);
        _handle_mouse_input(io);
        _handle_keyboard_input(io);
    }

    // Only call End() if Begin() was actually called and succeeded
    if (window_created && !m_is_embedded) {
        ImGui::End();
    }
}

void Terminal::resize(int cols, int rows) {
    std::lock_guard<std::mutex> lock(m_buffer_mutex);
    // Get actual content area size
    ImVec2 content_size = ImGui::GetContentRegionAvail();
    float char_width = ImGui::GetFontBaked()->GetCharAdvance('M');
    float line_height = ImGui::GetTextLineHeight();

    // Calculate new dimensions based on actual font metrics
    int new_cols = std::max(1, static_cast<int>(content_size.x / char_width));
    int new_rows = std::max(1, static_cast<int>(content_size.y / line_height));

    // Only resize if dimensions actually changed
    if (new_cols == m_state.col && new_rows == m_state.row) {
        return;
    }

    // Create new buffers
    std::vector<std::vector<Glyph>> new_lines(rows);
    std::vector<std::vector<Glyph>> new_alt_lines(rows);
    std::vector<bool> new_dirty(rows, true);
    std::vector<bool> new_tabs(cols, false);

    // Initialize the new lines
    for (int i = 0; i < rows; i++) {
        new_lines[i].resize(cols);
        new_alt_lines[i].resize(cols);
        // Initialize with default glyphs if needed
        for (int j = 0; j < cols; j++) {
            new_lines[i][j].u = ' ';
            new_lines[i][j].mode = m_state.c.attrs;
            new_lines[i][j].fg = m_state.c.fg;
            new_lines[i][j].bg = m_state.c.bg;
        }
    }

    // Copy existing content
    int min_rows = std::min(rows, m_state.row);
    int min_cols = std::min(cols, m_state.col);

    for (int y = 0; y < min_rows; y++) {
        for (int x = 0; x < min_cols; x++) {
            if (y < m_state.lines.size() && x < m_state.lines[y].size()) {
                new_lines[y][x] = m_state.lines[y][x];
            }
        }
    }

    // Set new tab stops
    for (int i = 8; i < cols; i += 8) {
        new_tabs[i] = true;
    }

    // Update terminal state
    m_state.row = rows;
    m_state.col = cols;
    m_state.top = 0;
    m_state.bot = rows - 1;

    // Then clamp to ensure validity
    m_state.top = std::clamp(m_state.top, 0, rows - 1);
    m_state.bot = std::clamp(m_state.bot, m_state.top, rows - 1);
    if (m_state.bot < m_state.top) {
        m_state.bot = m_state.top;
    }

    // Swap in new buffers
    m_state.lines = std::move(new_lines);
    m_state.alt_lines = std::move(new_alt_lines);
    m_state.dirty = std::move(new_dirty);
    m_state.tabs = std::move(new_tabs);

    // Ensure cursor stays within bounds
    m_state.c.x = std::min(m_state.c.x, cols - 1);
    m_state.c.y = std::min(m_state.c.y, rows - 1);

    // Update PTY size if valid
    if (m_pty->is_valid()) {
        m_pty->resize(m_state.row, m_state.col);
    }
    vterm_set_size(m_vterm, m_state.row, m_state.col);
    vterm_screen_flush_damage(m_vterm_screen);

    LOG_DEBUG("Terminal resized to {}x{}", cols, rows);
}

void Terminal::process_input(const std::string& input) const {
    if (!m_pty->is_valid()) {
        return;
    }
    if (m_state.mode & ModeBracketpaste) {
        if (input.substr(0, 4) == "\033[200~") {
            m_pty->write(input.c_str(), input.length());
            return;
        }
        if (input.substr(0, 4) == "\033[201~") {
            m_pty->write(input.c_str(), input.length());
            return;
        }
    }
    if (m_state.mode & ModeAppcursor) {
        if (input == "\033[A") {
            m_pty->write("\033OA", 3); // Up
            return;
        }
        if (input == "\033[B") {
            m_pty->write("\033OB", 3); // Down
            return;
        }
        if (input == "\033[C") {
            m_pty->write("\033OC", 3); // Right
            return;
        }
        if (input == "\033[D") {
            m_pty->write("\033OD", 3); // Left
            return;
        }
    }

    if (input == "\r\n" || input == "\n") {
        m_pty->write("\r", 1);
        return;
    }

    if (input == "\b") {
        m_pty->write("\b \b", 3);
        return;
    }

    m_pty->write(input.c_str(), input.length());
}

bool Terminal::selected_text(int x, int y) {
    if (m_selection.mode == SelectionIdle || m_selection.ob.x == -1 ||
        m_selection.alt != (m_state.mode & ModeAltscreen)) {
        return false;
    }

    // Convert coordinates to absolute buffer positions
    int actual_y = m_sb_buffer.size() + y;
    int sel_start_y = m_sb_buffer.size() + m_selection.nb.y;
    int sel_end_y = m_sb_buffer.size() + m_selection.ne.y;

    // Ensure start is less than or equal to end
    if (sel_start_y > sel_end_y) {
        std::swap(sel_start_y, sel_end_y);
    }

    if (m_selection.type == SelectionRectangular) {
        return BETWEEN(actual_y, sel_start_y, sel_end_y) &&
               BETWEEN(x, m_selection.nb.x, m_selection.ne.x);
    }

    return BETWEEN(actual_y, sel_start_y, sel_end_y) &&
           (actual_y != sel_start_y || x >= m_selection.nb.x) &&
           (actual_y != sel_end_y || x <= m_selection.ne.x);
}

void Terminal::paste_from_clipboard() const {
    const char* text = ImGui::GetClipboardText();

    if (m_state.mode & ModeBracketpaste) {
        // Send paste start sequence
        m_pty->write("\033[200~", 6);
        // Send the actual text
        m_pty->write(text, strlen(text));
        // Send paste end sequence
        m_pty->write("\033[201~", 6);
    } else {
        m_pty->write(text, strlen(text));
    }
}

void Terminal::_start_shell() {
    // Open PTY master
    if (m_pty->launch(
            m_state.row,
            m_state.col)) { // Use initial rows/cols from Terminal state object
        m_read_thread = std::thread(&Terminal::_read_output, this);
    } else {
        LOG_CRITICAL("Faield to launch pty!");
    }
}

void Terminal::_read_output() {
    char buffer[4096];
    while (!m_should_terminate) {
        size_t bytes_read = 0;
        if (m_pty->is_valid()) {
            bytes_read = m_pty->read(buffer, sizeof(buffer) - 1);
        }
        if (bytes_read > 0) {
            std::lock_guard<std::mutex> lock(m_buffer_mutex);
            _write_to_buffer(buffer, bytes_read);
        } else if (bytes_read < 0 && errno != EINTR) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void Terminal::_write_to_buffer(const char* data, size_t length) {
    static char utf8buf[g_utf_size];
    static size_t utf8len = 0;

    vterm_input_write(m_vterm, data, length);
    vterm_screen_flush_damage(m_vterm_screen);
    // for (size_t i = 0; i < length; ++i) {
    //     unsigned char c = data[i];

    //     // Existing STR sequence handling
    //     if (m_state.esc & EscStr) {
    //         if (c == '\a' || c == 030 || c == 032 || c == 033 ||
    //             ISCONTROLC1(c)) {
    //             m_state.esc &= ~(EscStart | EscStr);
    //             m_state.esc |= EscStrEnd;
    //             _strparse();
    //             _handle_string_sequence();
    //             m_state.esc = 0;
    //             continue;
    //         }

    //         if (m_strescseq.len < 256) {
    //             m_strescseq.buf += c;
    //             m_strescseq.len++;
    //         }
    //         continue;
    //     }

    //     // Escape sequence start
    //     if (c == '\033') {
    //         m_state.esc = EscStart;
    //         m_csiescseq.len = 0;
    //         m_strescseq.buf.clear();
    //         m_strescseq.len = 0;
    //         utf8len = 0; // Reset UTF-8 buffer
    //         continue;
    //     }

    //     // Control character handling
    //     if (ISCONTROL(c)) {
    //         utf8len = 0; // Reset UTF-8 buffer
    //         _handle_control_code(c);
    //         continue;
    //     }

    //     // Ongoing escape sequence processing
    //     if (m_state.esc & EscStart) {
    //         if (m_state.esc & EscCsi) {
    //             if (m_csiescseq.len < sizeof(m_csiescseq.buf) - 1) {
    //                 m_csiescseq.buf[m_csiescseq.len++] = c;
    //                 if (BETWEEN(c, 0x40, 0x7E)) {
    //                     m_csiescseq.buf[m_csiescseq.len] = '\0';
    //                     m_csiescseq.mode[0] = c;
    //                     _parse_csi_param(m_csiescseq);
    //                     _handle_csi(m_csiescseq);
    //                     m_state.esc = 0;
    //                     m_csiescseq.len = 0;
    //                 }
    //             }
    //             continue;
    //         }

    //         if (_eschandle(c)) {
    //             m_state.esc = 0;
    //         }
    //         continue;
    //     }

    //     if (m_state.mode & ModeUtf8) {
    //         if (utf8len == 0) {
    //             if ((c & 0x80) == 0) {
    //                 _write_char(c);
    //             } else if ((c & 0xE0) == 0xC0 || // 2-byte start
    //                        (c & 0xF0) == 0xE0 || // 3-byte start
    //                        (c & 0xF8) == 0xF0) { // 4-byte start
    //                 utf8buf[utf8len++] = c;

    //                 // If it's a 3-byte sequence (box drawing characters),
    //                 // we want to immediately look for the next two bytes
    //                 if ((c & 0xF0) == 0xE0) {
    //                     // Look ahead for the next two bytes
    //                     if (i + 2 < length) {
    //                         utf8buf[utf8len++] = data[i + 1];
    //                         utf8buf[utf8len++] = data[i + 2];

    //                         Rune u;
    //                         size_t decoded = _utf8_decode(utf8buf, &u,
    //                         utf8len); if (decoded > 0) {
    //                             _write_char(u);
    //                         }

    //                         // Skip the next two bytes since we've processed
    //                         // them
    //                         i += 2;
    //                         utf8len = 0;
    //                     }
    //                 }
    //             } else {
    //                 // Unexpected start byte
    //                 utf8buf[utf8len++] = c;
    //                 utf8buf[utf8len] = '\0';
    //                 _write_char(0xFFFD);
    //             }
    //         } else {
    //             // This block is now less likely to be used due to the
    //             changes
    //             // above
    //             if ((c & 0xC0) == 0x80) {
    //                 utf8buf[utf8len++] = c;

    //                 size_t expected_len = ((utf8buf[0] & 0xE0) == 0xC0)   ? 2
    //                                       : ((utf8buf[0] & 0xF0) == 0xE0) ? 3
    //                                       : ((utf8buf[0] & 0xF8) == 0xF0) ? 4
    //                                                                       :
    //                                                                       0;

    //                 if (utf8len == expected_len) {
    //                     Rune u;
    //                     size_t decoded = _utf8_decode(utf8buf, &u, utf8len);
    //                     if (decoded > 0) {
    //                         _write_char(u);
    //                     }
    //                     utf8len = 0;
    //                 }
    //             } else {
    //                 LOG_ERROR("Invalid continuation byte: 0x{:x}",
    //                           static_cast<int>(c));
    //                 utf8len = 0;
    //             }
    //         }
    //     } else {
    //         _write_char(c);
    //     }
    // }
}

void Terminal::_write_char(Rune u) {
    auto it = m_box_drawing_chars.find(u);
    if (it != m_box_drawing_chars.end()) {
        u = it->second;
    } else {
        // Log unmapped characters
        if (u >= 0x2500 && u <= 0x257F) {
            // LOG_ERROR("Unmapped box drawing character: U+{:x}", u);
        }
    }

    if (m_state.c.x >= m_state.col) {
        // Set wrap flag on current line before moving to next
        if (m_state.c.y < m_state.row && m_state.c.x > 0) {
            m_state.lines[m_state.c.y][m_state.c.x - 1].mode |= AttrWrap;
        }

        m_state.c.x = 0;
        if (m_state.c.y == m_state.bot) {
            _scroll_up(m_state.top, 1);
        } else if (m_state.c.y < m_state.row - 1) {
            m_state.c.y++;
        }
    }

    Glyph g;
    g.u = u;
    g.mode = m_state.c.attrs;
    g.fg = m_state.c.fg;
    g.bg = m_state.c.bg;
    g.color_mode = m_state.c.color_mode;
    g.true_color_fg = m_state.c.true_color_fg;
    g.true_color_bg = m_state.c.true_color_bg;

    // Set wrap flag if at end of line
    if (m_state.c.x == m_state.col - 1) {
        g.mode |= AttrWrap;
    }

    _write_glyph(g, m_state.c.x, m_state.c.y);
    m_state.c.x++;
}

void Terminal::_write_glyph(const Glyph& g, int x, int y) {
    if (x >= m_state.col || y >= m_state.row || x < 0 || y < 0) {
        return;
    }

    Glyph& cell = m_state.lines[y][x];
    cell = g;

    // Ensure we properly handle attribute clearing
    if (!(g.mode &
          (AttrReverse | AttrBold | AttrItalic | AttrBlink | AttrUnderline))) {
        cell.mode &=
            ~(AttrReverse | AttrBold | AttrItalic | AttrBlink | AttrUnderline);
    }

    cell.mode = (cell.mode & ~(AttrReverse | AttrBold | AttrItalic | AttrBlink |
                               AttrUnderline)) |
                (g.mode & (AttrReverse | AttrBold | AttrItalic | AttrBlink |
                           AttrUnderline));

    cell.color_mode = g.color_mode;

    if (cell.mode & AttrReverse) {
        cell.fg = m_state.c.bg;
        cell.bg = m_state.c.fg;
        cell.true_color_fg = m_state.c.true_color_bg;
        cell.true_color_bg = m_state.c.true_color_fg;
    } else {
        cell.fg = m_state.c.fg;
        cell.bg = m_state.c.bg;
        cell.true_color_fg = m_state.c.true_color_fg;
        cell.true_color_bg = m_state.c.true_color_bg;
    }

    // Handle bold attribute affecting colors
    if (cell.mode & AttrBold && cell.color_mode == ColorBasic) {
        // Make the color brighter for bold text
        if (cell.true_color_fg < 0x8) { // If using standard colors
            cell.fg = m_default_color_map[cell.true_color_fg +
                                          8]; // Use bright version
        }
    }
    if (cell.mode & AttrWide && x + 1 < m_state.col) {
        Glyph& next_cell = m_state.lines[y][x + 1];
        next_cell.u = ' ';
        next_cell.mode = AttrWdummy;
        // Copy color/attrs from base cell
        next_cell.fg = cell.fg;
        next_cell.bg = cell.bg;
    }

    m_state.dirty[y] = true;
}

void Terminal::_reset() {
    // Reset terminal to initial state
    m_state.mode = ModeWrap | ModeUtf8;
    m_state.c = {}; // Reset cursor
    m_state.charset = 0;

    // Reset character set translation table
    for (char& i : m_state.trantbl) {
        i = CharsetUsa;
    }

    // Clear screen and reset color/attributes
    _clear_region(0, 0, m_state.col - 1, m_state.row - 1);
    m_state.c.fg = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    m_state.c.bg = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
}

void Terminal::_check_font_size_changed() {
    float current_font_size = ImGui::GetFontBaked()->Size;
    if (current_font_size != m_last_font_size) {
        m_last_font_size = current_font_size;
        resize(m_state.col, m_state.row);
    }
}

bool Terminal::_setup_window() {
    if (m_is_embedded) {
        return true;
    }
    ImGui::SetNextWindowPos(m_embedded_window_pos, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(m_embedded_window_size, ImGuiCond_FirstUseEver);

    bool window_open = true;
    bool window_created = ImGui::Begin(m_window_title.c_str(), &window_open,
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

void Terminal::_handle_terminal_resize() {
    ImVec2 content_size = ImGui::GetContentRegionAvail();
    float char_width = ImGui::GetFontBaked()->GetCharAdvance('M');
    float line_height = ImGui::GetTextLineHeight();

    int new_cols = std::max(1, static_cast<int>(content_size.x / char_width));
    int new_rows = std::max(1, static_cast<int>(content_size.y / line_height));

    if (new_cols != m_state.col || new_rows != m_state.row) {
        LOG_DEBUG("Resizing terminal.");

        resize(new_cols, new_rows);
    }
}

void Terminal::_handle_scrollback(const ImGuiIO& io, int new_rows) {
    if (ImGui::IsWindowFocused() && ImGui::IsWindowHovered() &&
        !(m_state.mode & ModeAltscreen)) {
        if (io.MouseWheel != 0.0f) {
            int max_scroll =
                std::max(0, static_cast<int>(m_sb_buffer.size() + m_state.row) -
                                new_rows);
            // Reverse the scroll direction by changing subtraction to addition
            m_scroll_offset += static_cast<int>(io.MouseWheel * 3);
            m_scroll_offset = std::clamp(m_scroll_offset, 0, max_scroll);
        }
    }
}

void Terminal::_handle_mouse_input(const ImGuiIO& io) {

    if (!ImGui::IsWindowFocused() || !ImGui::IsWindowHovered()) {
        return;
    }

    ImVec2 mouse_pos = ImGui::GetMousePos();
    ImVec2 content_pos = ImGui::GetCursorScreenPos();
    float char_width = ImGui::GetFontBaked()->GetCharAdvance('M');
    float line_height = ImGui::GetTextLineHeight();

    int cell_x = static_cast<int>((mouse_pos.x - content_pos.x) / char_width);
    int cell_y = static_cast<int>(
        (mouse_pos.y - content_pos.y + (line_height * 0.2)) / line_height);

    cell_x = std::clamp(cell_x, 0, m_state.col - 1);

    // Account for scrollback offset when not in alt screen
    if (!(m_state.mode & ModeAltscreen)) {
        ImVec2 content_size = ImGui::GetContentRegionAvail();
        int visible_rows =
            std::max(1, static_cast<int>(content_size.y / line_height));
        int total_lines = m_sb_buffer.size() + m_state.row;
        int max_scroll = std::max(0, total_lines - visible_rows);
        m_scroll_offset = std::clamp(m_scroll_offset, 0, max_scroll);
        int start_line =
            std::max(0, total_lines - visible_rows - m_scroll_offset);

        // Convert visible Y coordinate to actual buffer coordinate
        int actual_y = start_line + cell_y;

        // Convert to selection coordinate system (relative to scrollback
        // buffer)
        cell_y = actual_y - m_sb_buffer.size();

    } else {
        // In alt screen, clamp to current screen
        cell_y = std::clamp(cell_y, 0, m_state.row - 1);
    }

    static ImVec2 click_start_pos{0, 0};

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        click_start_pos = mouse_pos;
        _selection_start(cell_x, cell_y);
    } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        float drag_distance =
            sqrt(drag_delta.x * drag_delta.x + drag_delta.y * drag_delta.y);

        if (drag_distance > g_drag_threshold) {
            _selection_extend(cell_x, cell_y);
        }
    } else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        ImVec2 drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        float drag_distance =
            sqrt(drag_delta.x * drag_delta.x + drag_delta.y * drag_delta.y);

        if (drag_distance <= g_drag_threshold) {
            _selection_clear();
        }
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        paste_from_clipboard();
    }

    // Handle clipboard shortcuts
    if (io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false) ||
            ImGui::IsKeyPressed(ImGuiKey_C, false)) {
            _copy_selection();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_V, false)) {
            paste_from_clipboard();
        }
    }
}

void Terminal::_handle_keyboard_input(const ImGuiIO& io) const {
    if (!ImGui::IsWindowFocused()) {
        return;
    }
    VTermModifier mod = VTERM_MOD_NONE;
    if (io.KeyCtrl) {
        mod = static_cast<VTermModifier>(
            static_cast<std::underlying_type_t<VTermModifier>>(mod) |
            static_cast<std::underlying_type_t<VTermModifier>>(VTERM_MOD_CTRL));
    }
    if (io.KeyShift) {
        mod = static_cast<VTermModifier>(
            static_cast<std::underlying_type_t<VTermModifier>>(mod) |
            static_cast<std::underlying_type_t<VTermModifier>>(
                VTERM_MOD_SHIFT));
    }
    if (io.KeyAlt) {
        mod = static_cast<VTermModifier>(
            static_cast<std::underlying_type_t<VTermModifier>>(mod) |
            static_cast<std::underlying_type_t<VTermModifier>>(VTERM_MOD_ALT));
    }
    static const std::pair<ImGuiKey, VTermKey> s_key_map[] = {
#if !defined(_WIN32)
        {ImGuiKey_Enter, VTERM_KEY_ENTER},
        {ImGuiKey_Tab, VTERM_KEY_TAB},
        {ImGuiKey_Backspace, VTERM_KEY_BACKSPACE},
        {ImGuiKey_Escape, VTERM_KEY_ESCAPE},
#endif
        {ImGuiKey_UpArrow, VTERM_KEY_UP},
        {ImGuiKey_DownArrow, VTERM_KEY_DOWN},
        {ImGuiKey_LeftArrow, VTERM_KEY_LEFT},
        {ImGuiKey_RightArrow, VTERM_KEY_RIGHT},
        {ImGuiKey_Insert, VTERM_KEY_INS},
        {ImGuiKey_Delete, VTERM_KEY_DEL},
        {ImGuiKey_Home, VTERM_KEY_HOME},
        {ImGuiKey_End, VTERM_KEY_END},
        {ImGuiKey_PageUp, VTERM_KEY_PAGEUP},
        {ImGuiKey_PageDown, VTERM_KEY_PAGEDOWN},
        {ImGuiKey_F1, static_cast<VTermKey>(VTERM_KEY_FUNCTION(1))},
        {ImGuiKey_F2, static_cast<VTermKey>(VTERM_KEY_FUNCTION(2))},
        {ImGuiKey_F3, static_cast<VTermKey>(VTERM_KEY_FUNCTION(3))},
        {ImGuiKey_F4, static_cast<VTermKey>(VTERM_KEY_FUNCTION(4))},
        {ImGuiKey_F5, static_cast<VTermKey>(VTERM_KEY_FUNCTION(5))},
        {ImGuiKey_F6, static_cast<VTermKey>(VTERM_KEY_FUNCTION(6))},
        {ImGuiKey_F7, static_cast<VTermKey>(VTERM_KEY_FUNCTION(7))},
        {ImGuiKey_F8, static_cast<VTermKey>(VTERM_KEY_FUNCTION(8))},
        {ImGuiKey_F9, static_cast<VTermKey>(VTERM_KEY_FUNCTION(9))},
        {ImGuiKey_F10, static_cast<VTermKey>(VTERM_KEY_FUNCTION(10))},
        {ImGuiKey_F11, static_cast<VTermKey>(VTERM_KEY_FUNCTION(11))},
        {ImGuiKey_F12, static_cast<VTermKey>(VTERM_KEY_FUNCTION(12))},
        {ImGuiKey_F13, static_cast<VTermKey>(VTERM_KEY_FUNCTION(13))},
        {ImGuiKey_F14, static_cast<VTermKey>(VTERM_KEY_FUNCTION(14))},
        {ImGuiKey_F15, static_cast<VTermKey>(VTERM_KEY_FUNCTION(15))},
        {ImGuiKey_F16, static_cast<VTermKey>(VTERM_KEY_FUNCTION(16))},
        {ImGuiKey_F17, static_cast<VTermKey>(VTERM_KEY_FUNCTION(17))},
        {ImGuiKey_F18, static_cast<VTermKey>(VTERM_KEY_FUNCTION(18))},
        {ImGuiKey_F19, static_cast<VTermKey>(VTERM_KEY_FUNCTION(19))},
        {ImGuiKey_F20, static_cast<VTermKey>(VTERM_KEY_FUNCTION(20))},
        {ImGuiKey_F21, static_cast<VTermKey>(VTERM_KEY_FUNCTION(21))},
        {ImGuiKey_F22, static_cast<VTermKey>(VTERM_KEY_FUNCTION(22))},
        {ImGuiKey_F23, static_cast<VTermKey>(VTERM_KEY_FUNCTION(23))},
        {ImGuiKey_F24, static_cast<VTermKey>(VTERM_KEY_FUNCTION(24))}};
    for (const auto& [imgui_key, vterm_key] : s_key_map) {
        if (ImGui::IsKeyPressed(imgui_key)) {
            vterm_keyboard_key(m_vterm, vterm_key, mod);
        }
    }
    for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
        const auto& cc = io.InputQueueCharacters[i];
        char c = static_cast<char>(io.InputQueueCharacters[i]);
        if (c != 0) {
#if defined(_WIN32)
            if (c == '\r') {
                vterm_keyboard_key(m_vterm, VTERM_KEY_ENTER, mod);
            } else if (c == '\t') {
                vterm_keyboard_key(m_vterm, VTERM_KEY_TAB, mod);
            } else if (c == '\b') {
                vterm_keyboard_key(m_vterm, VTERM_KEY_BACKSPACE, mod);
            } else if (c == 27) {
                vterm_keyboard_key(m_vterm, VTERM_KEY_ESCAPE, mod);
            } else {
#endif
                vterm_keyboard_unichar(m_vterm, io.InputQueueCharacters[i],
                                       mod);
#if defined(_WIN32)
            }
#endif
        }
    }
}

void Terminal::_handle_special_keys(const ImGuiIO& io) const {
    if (ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        process_input("\r");
    } else if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
        process_input("\x7f");
    } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        process_input(m_state.mode & ModeAppcursor ? "\033OA" : "\033[A");
    } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        process_input(m_state.mode & ModeAppcursor ? "\033OB" : "\033[B");
    } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
        process_input(m_state.mode & ModeAppcursor ? "\033OC" : "\033[C");
    } else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
        if (io.KeyCtrl) {
            process_input("\033[1;5D");
        } else if (io.KeyShift) {
            process_input("\033[1;2D");
        } else if (m_state.mode & ModeAppcursor) {
            process_input("\033OD");
        } else {
            process_input("\033[D");
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
        process_input("\033[H");
    } else if (ImGui::IsKeyPressed(ImGuiKey_End)) {
        process_input("\033[F");
    } else if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        process_input("\033[3~");
    } else if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
        process_input("\033[5~");
    } else if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
        process_input("\033[6~");
    } else if (ImGui::IsKeyPressed(ImGuiKey_Tab)) {
        process_input("\t");
    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        process_input("\033");
    }
}
void Terminal::_handle_control_combos(const ImGuiIO& io) const {
    if (!io.KeyCtrl && !io.KeySuper) {
        return;
    }

    static const std::pair<ImGuiKey, char> s_control_keys[] = {
        {ImGuiKey_A, '\x01'}, {ImGuiKey_B, '\x02'}, {ImGuiKey_C, '\x03'},
        {ImGuiKey_D, '\x04'}, {ImGuiKey_E, '\x05'}, {ImGuiKey_F, '\x06'},
        {ImGuiKey_G, '\x07'}, {ImGuiKey_H, '\x08'}, {ImGuiKey_I, '\x09'},
        {ImGuiKey_J, '\x0A'}, {ImGuiKey_K, '\x0B'}, {ImGuiKey_L, '\x0C'},
        {ImGuiKey_M, '\x0D'}, {ImGuiKey_N, '\x0E'}, {ImGuiKey_O, '\x0F'},
        {ImGuiKey_P, '\x10'}, {ImGuiKey_Q, '\x11'}, {ImGuiKey_R, '\x12'},
        {ImGuiKey_S, '\x13'}, {ImGuiKey_T, '\x14'}, {ImGuiKey_U, '\x15'},
        {ImGuiKey_W, '\x17'}, {ImGuiKey_X, '\x18'}, {ImGuiKey_Y, '\x19'},
        {ImGuiKey_Z, '\x1A'}};

    for (const auto& [key, ctrl_char] : s_control_keys) {
        if (ImGui::IsKeyPressed(key)) {
            process_input(std::string(1, ctrl_char));
        }
    }
}
void Terminal::_handle_regular_text_input(const ImGuiIO& io) const {
    if (io.KeySuper || io.KeyCtrl || io.KeyAlt) {
        return;
    }

    for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
        char c = static_cast<char>(io.InputQueueCharacters[i]);
        if (c != 0) {
            process_input(std::string(1, c));
        }
    }
}

void Terminal::_render_buffer() {
    std::lock_guard<std::mutex> lock(m_buffer_mutex);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float char_width = ImGui::GetFontBaked()->GetCharAdvance('M');
    float line_height = ImGui::GetTextLineHeight();

    if (m_state.mode & ModeAltscreen) {
        _render_alt_screen(draw_list, pos, char_width, line_height);
    } else {
        _render_main_screen(draw_list, pos, char_width, line_height);
    }
}

void Terminal::_render_alt_screen(ImDrawList* draw_list, const ImVec2& pos,
                                  float char_width, float line_height) {
    // Handle selection highlight
    if (m_selection.mode != SelectionIdle && m_selection.ob.x != -1) {
        _render_selection_highlight(draw_list, pos, char_width, line_height, 0,
                                    m_state.row);
    }

    // Draw alt screen characters
    for (int y = 0; y < m_state.row; y++) {
        if (!m_state.dirty[y]) {
            continue;
        }

        for (int x = 0; x < m_state.col; x++) {
            VTermScreenCell cell;
            VTermPos vterm_pos{
                .row = y,
                .col = x,
            };
            vterm_screen_get_cell(m_vterm_screen, vterm_pos, &cell);

            ImVec2 char_pos(pos.x + x * char_width, pos.y + y * line_height);
            _render_vterm_cell(draw_list, cell, char_pos, char_width,
                               line_height);
        }
    }

    // Draw cursor
    if (ImGui::IsWindowFocused()) {
        ImVec2 cursor_pos(pos.x + m_state.c.x * char_width,
                          pos.y + m_state.c.y * line_height);
        float alpha = (sin(ImGui::GetTime() * 3.14159f) * 0.3f) + 0.5f;
        VTermScreenCell cursor_cell;
        VTermPos vterm_pos{
            .row = m_state.c.y,
            .col = m_state.c.x,
        };
        vterm_screen_get_cell(m_vterm_screen, vterm_pos, &cursor_cell);
        _render_cursor(draw_list, cursor_pos, cursor_cell, char_width,
                       line_height, alpha);
    }
}

void Terminal::_render_main_screen(ImDrawList* draw_list, const ImVec2& pos,
                                   float char_width, float line_height) {
    ImVec2 content_size = ImGui::GetContentRegionAvail();
    int visible_rows =
        std::max(1, static_cast<int>(content_size.y / line_height));
    int total_lines = m_sb_buffer.size() + m_state.row;

    // Handle scrollback clamping
    int max_scroll = std::max(0, total_lines - visible_rows);
    m_scroll_offset = std::clamp(m_scroll_offset, 0, max_scroll);
    int start_line = std::max(0, total_lines - visible_rows - m_scroll_offset);

    // Handle selection highlighting
    if (m_selection.mode != SelectionIdle && m_selection.ob.x != -1) {
        _render_selection_highlight(draw_list, pos, char_width, line_height,
                                    start_line, start_line + visible_rows,
                                    m_sb_buffer.size());
    }

    // Draw content
    for (int vis_y = 0; vis_y < visible_rows; vis_y++) {
        int current_line = start_line + vis_y;

        bool use_sb_buffer = current_line < m_sb_buffer.size();
        int row_idx =
            use_sb_buffer ? current_line : current_line - m_sb_buffer.size();

        for (int x = 0; x < m_state.col; x++) {
            VTermScreenCell* cell = nullptr;
            VTermScreenCell vt_cell;
            if (use_sb_buffer) {
                cell = &m_sb_buffer[row_idx][x];
            } else {
                VTermPos vterm_pos{
                    .row = row_idx,
                    .col = x,
                };
                vterm_screen_get_cell(m_vterm_screen, vterm_pos, &vt_cell);
                cell = &vt_cell;
            }
            if (cell == nullptr) {
                continue;
            }
            ImVec2 char_pos(pos.x + x * char_width,
                            pos.y + vis_y * line_height);
            _render_vterm_cell(draw_list, *cell, char_pos, char_width,
                               line_height);
        }
    }

    // Draw cursor when not scrolled
    if (ImGui::IsWindowFocused() && m_scroll_offset == 0) {
        ImVec2 cursor_pos(pos.x + m_state.c.x * char_width,
                          pos.y + (visible_rows -
                                   (total_lines - m_sb_buffer.size()) +
                                   m_state.c.y) *
                                      line_height);
        float alpha = (sin(ImGui::GetTime() * 3.14159f) * 0.3f) + 0.5f;
        VTermScreenCell cursor_cell;
        VTermPos vterm_pos{
            .row = m_state.c.y,
            .col = m_state.c.x,
        };
        vterm_screen_get_cell(m_vterm_screen, vterm_pos, &cursor_cell);
        _render_cursor(draw_list, cursor_pos, cursor_cell, char_width,
                       line_height, alpha);
    }
}

void Terminal::_render_selection_highlight(ImDrawList* draw_list,
                                           const ImVec2& pos, float char_width,
                                           float line_height, int start_y,
                                           int end_y, int screen_offset) {
    for (int y = start_y; y < end_y; y++) {
        int screen_y = y - screen_offset;
        // Handle both current screen and scrollback lines
        if (screen_y >= 0 && screen_y < m_state.row) {
            // Current screen line
            for (int x = 0; x < m_state.col; x++) {
                // Convert visible coordinate to selection coordinate system
                int selection_y = screen_offset + screen_y - m_sb_buffer.size();
                if (selected_text(x, selection_y)) {
                    ImVec2 highlight_pos(pos.x + x * char_width,
                                         pos.y + (y - start_y) * line_height);
                    draw_list->AddRectFilled(
                        highlight_pos,
                        ImVec2(highlight_pos.x + char_width,
                               highlight_pos.y + line_height),
                        ImGui::ColorConvertFloat4ToU32(
                            ImVec4(1.0f, 0.1f, 0.7f, 0.3f)));
                }
            }
        } else if (screen_y < 0) {
            // Scrollback line - render it if it's visible
            int scrollback_index = -screen_y - 1;
            if (scrollback_index >= 0 &&
                scrollback_index < m_sb_buffer.size()) {
                for (int x = 0; x < m_state.col; x++) {
                    // Convert visible coordinate to selection coordinate system
                    int selection_y =
                        screen_offset + screen_y - m_sb_buffer.size();
                    if (selected_text(x, selection_y)) {
                        ImVec2 highlight_pos(pos.x + x * char_width,
                                             pos.y +
                                                 (y - start_y) * line_height);
                        draw_list->AddRectFilled(
                            highlight_pos,
                            ImVec2(highlight_pos.x + char_width,
                                   highlight_pos.y + line_height),
                            ImGui::ColorConvertFloat4ToU32(
                                ImVec4(1.0f, 0.1f, 0.7f, 0.3f)));
                    }
                }
            }
        }
    }
}

void Terminal::_render_glyph(ImDrawList* draw_list, const Glyph& glyph,
                             const ImVec2& char_pos, float char_width,
                             float line_height) {
    ImVec4 fg = glyph.fg;
    ImVec4 bg = glyph.bg;

    _handle_glyph_colors(glyph, fg, bg);

    // Draw background
    if (bg.x != 0 || bg.y != 0 || bg.z != 0 || (glyph.mode & AttrReverse)) {
        draw_list->AddRectFilled(
            char_pos, ImVec2(char_pos.x + char_width, char_pos.y + line_height),
            ImGui::ColorConvertFloat4ToU32(bg));
    }

    // Draw character
    if (glyph.u != ' ' && glyph.u != 0) {
        char text[g_utf_size] = {0};
        _utf8_encode(glyph.u, text);
        draw_list->AddText(char_pos, ImGui::ColorConvertFloat4ToU32(fg), text);
    }

    // Draw underline
    if (glyph.mode & AttrUnderline) {
        draw_list->AddLine(
            ImVec2(char_pos.x, char_pos.y + line_height - 1),
            ImVec2(char_pos.x + char_width, char_pos.y + line_height - 1),
            ImGui::ColorConvertFloat4ToU32(fg));
    }
}
void Terminal::_render_vterm_cell(ImDrawList* draw_list, VTermScreenCell& cell,
                                  const ImVec2& char_pos, float char_width,
                                  float line_height) {
    ImVec4 fg{m_dark_mode ? 1.0f : 0.0f, m_dark_mode ? 1.0f : 0.0f,
              m_dark_mode ? 1.0f : 0.0f, 1.0f};
    ImVec4 bg{m_dark_mode ? 0.0f : 1.0f, m_dark_mode ? 0.0f : 1.0f,
              m_dark_mode ? 0.0f : 1.0f, 1.0f};
    _handle_vterm_cell_colors(cell, fg, bg);

    // Draw background
    if (bg.x != 0 || bg.y != 0 || bg.z != 0 || (cell.attrs.reverse)) {
        draw_list->AddRectFilled(
            char_pos, ImVec2(char_pos.x + char_width, char_pos.y + line_height),
            ImGui::ColorConvertFloat4ToU32(bg));
    }

    // Draw character
    if (cell.width > 0) {
        char text[g_utf_size] = {0};
        size_t len = 0;
        for (char i = 0; i < cell.width; i++) {
            len += _utf8_encode(cell.chars[i], &text[len]);
        }
        draw_list->AddText(char_pos, ImGui::ColorConvertFloat4ToU32(fg), text);
    }

    // Draw underline
    if (cell.attrs.underline) {
        draw_list->AddLine(
            ImVec2(char_pos.x, char_pos.y + line_height - 1),
            ImVec2(char_pos.x + char_width, char_pos.y + line_height - 1),
            ImGui::ColorConvertFloat4ToU32(fg));
    }
}

void Terminal::_handle_vterm_cell_colors(VTermScreenCell& cell, ImVec4& fg,
                                         ImVec4& bg) {
    if (VTERM_COLOR_IS_DEFAULT_FG(&cell.fg)) {
        fg.x = m_dark_mode ? 1.0f : 0.0f;
        fg.y = m_dark_mode ? 1.0f : 0.0f;
        fg.z = m_dark_mode ? 1.0f : 0.0f;
        fg.w = 1.0f;
    }
    if (VTERM_COLOR_IS_INDEXED(&cell.fg)) {
        auto index = cell.fg.indexed.idx;
        if (index < 16) {
            fg = m_default_color_map[index];
        }
    }
    if (VTERM_COLOR_IS_RGB(&cell.fg)) {
        vterm_screen_convert_color_to_rgb(m_vterm_screen, &cell.fg);
        fg.x = static_cast<float>(cell.fg.rgb.red) / 256.0f;
        fg.y = static_cast<float>(cell.fg.rgb.green) / 256.0f;
        fg.z = static_cast<float>(cell.fg.rgb.blue) / 256.0f;
        fg.w = 1.0f;
    }
    if (VTERM_COLOR_IS_DEFAULT_BG(&cell.bg)) {
        bg.x = m_dark_mode ? 0.0f : 1.0f;
        bg.y = m_dark_mode ? 0.0f : 1.0f;
        bg.z = m_dark_mode ? 0.0f : 1.0f;
        bg.w = 1.0f;
    }
    if (VTERM_COLOR_IS_INDEXED(&cell.bg)) {
        auto index = cell.bg.indexed.idx;
        if (index < 16) {
            bg = m_default_color_map[index];
        }
    }
    if (VTERM_COLOR_IS_RGB(&cell.bg)) {
        vterm_screen_convert_color_to_rgb(m_vterm_screen, &cell.bg);
        bg.x = static_cast<float>(cell.bg.rgb.red) / 256.0f;
        bg.y = static_cast<float>(cell.bg.rgb.green) / 256.0f;
        bg.z = static_cast<float>(cell.bg.rgb.blue) / 256.0f;
        bg.w = 1.0f;
    }
}

void Terminal::_render_cursor(ImDrawList* draw_list, const ImVec2& cursor_pos,
                              VTermScreenCell& cursor_cell, float char_width,
                              float line_height, float alpha) {
    if (m_state.mode & ModeInsert) {
        draw_list->AddRectFilled(
            cursor_pos, ImVec2(cursor_pos.x + 2, cursor_pos.y + line_height),
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.7f, 0.7f, 0.7f, alpha)));
    } else if (cursor_cell.chars[0] != '\0') {
        char text[g_utf_size] = {0};
        size_t len = 0;
        for (char i = 0; i < cursor_cell.width; i++) {
            len += _utf8_encode(cursor_cell.chars[i], &text[len]);
        }
        ImVec4 fg{m_dark_mode ? 1.0f : 0.0f, m_dark_mode ? 1.0f : 0.0f,
                  m_dark_mode ? 1.0f : 0.0f, 1.0f};
        ImVec4 bg{m_dark_mode ? 0.0f : 1.0f, m_dark_mode ? 0.0f : 1.0f,
                  m_dark_mode ? 0.0f : 1.0f, 1.0f};
        _handle_vterm_cell_colors(cursor_cell, fg, bg);

        ImVec4 cursor_color{m_dark_mode ? 0.7f : 0.3f,
                            m_dark_mode ? 0.7f : 0.3f,
                            m_dark_mode ? 0.7f : 0.3f, alpha};
        draw_list->AddRectFilled(
            cursor_pos,
            ImVec2(cursor_pos.x + char_width, cursor_pos.y + line_height),
            ImGui::ColorConvertFloat4ToU32(cursor_color));
        draw_list->AddText(cursor_pos, ImGui::ColorConvertFloat4ToU32(fg),
                           text);
    } else {
        ImVec4 cursor_color{m_dark_mode ? 0.7f : 0.3f,
                            m_dark_mode ? 0.7f : 0.3f,
                            m_dark_mode ? 0.7f : 0.3f, alpha};
        draw_list->AddRectFilled(
            cursor_pos,
            ImVec2(cursor_pos.x + char_width, cursor_pos.y + line_height),
            ImGui::ColorConvertFloat4ToU32(cursor_color));
    }
}

void Terminal::_handle_glyph_colors(const Glyph& glyph, ImVec4& fg,
                                    ImVec4& bg) {

    // Handle true color
    if (glyph.color_mode == ColorTrue) {
        uint32_t tc = glyph.true_color_fg;
        fg = ImVec4(((tc >> 16) & 0xFF) / 255.0f, ((tc >> 8) & 0xFF) / 255.0f,
                    (tc & 0xFF) / 255.0f, 1.0f);
    }

    // Handle reverse video
    if (glyph.mode & AttrReverse) {
        std::swap(fg, bg);
    }

    // Handle bold
    if (glyph.mode & AttrBold && glyph.color_mode == ColorBasic) {
        fg.x = std::min(1.0f, fg.x * 1.5f);
        fg.y = std::min(1.0f, fg.y * 1.5f);
        fg.z = std::min(1.0f, fg.z * 1.5f);
    }
}

void Terminal::_selection_start(int col, int row) {
    _selection_clear();
    m_selection.mode = SelectionEmpty;
    m_selection.type = SelectionRegular;
    m_selection.alt = m_state.mode & ModeAltscreen;
    m_selection.snap = 0;
    m_selection.oe.x = m_selection.ob.x = col;
    m_selection.oe.y = m_selection.ob.y = row;
    _selection_normalize();

    if (m_selection.snap != 0) {
        m_selection.mode = SelectionReady;
    }
}

void Terminal::_selection_extend(int col, int row) {
    if (m_selection.mode == SelectionIdle)
        return;
    if (m_selection.mode == SelectionEmpty) {
        m_selection.mode = SelectionSelecting;
    }

    m_selection.oe.x = col;
    m_selection.oe.y = row;
    _selection_normalize();
}

void Terminal::_selection_clear() {
    if (m_selection.ob.x == -1) {
        return;
    }
    m_selection.mode = SelectionIdle;
    m_selection.ob.x = -1;
}

void Terminal::_get_selection(std::string& selected) {
    if (m_selection.ob.x == -1) {
        return;
    }

    // Convert selection coordinates to absolute buffer positions
    int sel_start_y = m_sb_buffer.size() + m_selection.nb.y;
    int sel_end_y = m_sb_buffer.size() + m_selection.ne.y;

    for (int abs_y = sel_start_y; abs_y <= sel_end_y; abs_y++) {
        const std::vector<VTermScreenCell>* line = nullptr;

        bool use_sb_buffer = abs_y < m_sb_buffer.size();
        int row_idx = use_sb_buffer ? abs_y : abs_y - m_sb_buffer.size();
        // Determine which buffer this line is in
        if (abs_y < m_sb_buffer.size()) {
            // Line is in scrollback buffer
            line = &m_sb_buffer[abs_y];
        }

        int xstart = (abs_y == sel_start_y) ? m_selection.nb.x : 0;
        int xend = (abs_y == sel_end_y) ? m_selection.ne.x : m_state.col - 1;

        // Clamp xstart and xend to line size
        if (line != nullptr) {
            xstart = std::clamp(xstart, 0, static_cast<int>(line->size()) - 1);
            xend = std::clamp(xend, 0, static_cast<int>(line->size()) - 1);
        }

        for (int x = xstart; x <= xend; x++) {
            VTermScreenCell* cell = nullptr;
            VTermScreenCell vt_cell;
            // Determine which buffer this cell is in
            if (use_sb_buffer) {
                // Cell is in scrollback buffer
                cell = &m_sb_buffer[row_idx][x];
            } else {
                // Cell is in current screen buffer
                VTermPos vterm_pos{
                    .row = row_idx,
                    .col = x,
                };
                vterm_screen_get_cell(m_vterm_screen, vterm_pos, &vt_cell);
                cell = &vt_cell;
            }
            if (cell == nullptr) {
                continue;
            }

            char buf[g_utf_size];
            size_t len = 0;
            for (char i = 0; i < cell->width; i++) {
                len += _utf8_encode(cell->chars[i], &buf[len]);
            }
            selected.append(buf, len);
        }

        if (abs_y < sel_end_y) {
            selected += '\n';
        }
    }
}

void Terminal::_copy_selection() {
    std::string selected;
    _get_selection(selected);
    if (!selected.empty()) {
        // Use ImGui's clipboard functions
        ImGui::SetClipboardText(selected.c_str());
    }
}

void Terminal::_clear_region(int x1, int y1, int x2, int y2) {
    int temp;
    if (x1 > x2) {
        temp = x1;
        x1 = x2;
        x2 = temp;
    }
    if (y1 > y2) {
        temp = y1;
        y1 = y2;
        y2 = temp;
    }

    // Constrain to terminal size
    x1 = std::max(0, std::min(x1, m_state.col - 1));
    x2 = std::max(0, std::min(x2, m_state.col - 1));
    y1 = std::max(0, std::min(y1, m_state.row - 1));
    y2 = std::max(0, std::min(y2, m_state.row - 1));

    // Clear the cells and properly reset attributes
    for (int y = y1; y <= y2; y++) {
        for (int x = x1; x <= x2; x++) {
            Glyph& g = m_state.lines[y][x];
            g.u = ' ';
            g.mode = m_state.c.attrs & ~(AttrReverse | AttrBold | AttrItalic |
                                         AttrBlink | AttrUnderline);
            g.fg = m_state.c.fg;
            g.bg = m_state.c.bg;
            g.color_mode = m_state.c.color_mode;
            g.true_color_fg = m_state.c.true_color_fg;
            g.true_color_bg = m_state.c.true_color_bg;
        }
        m_state.dirty[y] = true;
    }
}

void Terminal::_move_to(int x, int y) {
    int miny, maxy;

    // Get scroll region bounds
    if (m_state.c.state & CursorOrigin) {
        miny = m_state.top;
        maxy = m_state.bot;
    } else {
        miny = 0;
        maxy = m_state.row - 1;
    }

    int oldx = m_state.c.x;
    int oldy = m_state.c.y;

    // Constrain cursor position
    m_state.c.x = std::clamp(x, 0, m_state.col - 1);
    m_state.c.y = std::clamp(y, miny, maxy);

    // Reset wrap flag if moved
    if (oldx != m_state.c.x || oldy != m_state.c.y) {
        m_state.c.state &= ~CursorWrapnext;
    }

    // Handle wrap-next state when moving to end of line
    if (m_state.c.x == m_state.col - 1) {
        m_state.c.state |= CursorWrapnext;
    }
}

void Terminal::_scroll_up(int orig, int n) {
    if (orig < 0 || orig >= m_state.row) {
        return;
    }
    if (n <= 0) {
        return;
    }

    n = std::min(n, m_state.bot - orig + 1);
    n = std::max(n, 0);

    // Add scrolled lines to scrollback when not in alt screen
    if (!(m_state.mode & ModeAltscreen)) {
        for (int i = orig; i < orig + n; ++i) {
            if (i < m_state.lines.size()) {
                _add_to_scrollback(m_state.lines[i]);
            }
        }
    }

    // Existing scroll handling...
    for (int y = orig; y <= m_state.bot - n; ++y) {
        m_state.lines[y] = std::move(m_state.lines[y + n]);
    }

    // Clear revealed lines
    for (int i = m_state.bot - n + 1; i <= m_state.bot && i < m_state.row;
         i++) {
        m_state.lines[i].resize(m_state.col);
        for (int j = 0; j < m_state.col; j++) {
            Glyph& g = m_state.lines[i][j];
            g.u = ' ';
            g.mode = m_state.c.attrs;
            g.fg = m_state.c.fg;
            g.bg = m_state.c.bg;
        }
    }
}

void Terminal::_scroll_down(int orig, int n) {
    if (orig < 0 || orig >= m_state.row) {
        return; // Safety check
    }
    if (n <= 0) {
        return;
    }

    n = std::min(n, m_state.bot - orig + 1);
    n = std::max(n, 0);

    if (orig + n > m_state.row) {
        return;
    }

    // Mark lines as dirty
    for (int i = orig; i <= m_state.bot && i < m_state.row; i++) {
        m_state.dirty[i] = true;
    }

    // Move lines down with bounds checking
    for (int i = m_state.bot; i >= orig + n && i < m_state.row; i--) {
        m_state.lines[i] = std::move(m_state.lines[i - n]);
    }

    // Clear new lines
    for (int i = orig; i < orig + n && i < m_state.row; i++) {
        m_state.lines[i].resize(m_state.col);
        for (int j = 0; j < m_state.col; j++) {
            Glyph& g = m_state.lines[i][j];
            g.u = ' ';
            g.mode = m_state.c.attrs;
            g.fg = m_state.c.fg;
            g.bg = m_state.c.bg;
        }
    }
}

void Terminal::_set_mode(bool set, int mode) {
    if (mode == ModeAppcursor) {
        LOG_INFO("ModeAppcursor {}.", set ? "enabled" : "disabled");
    }
    if (set) {
        m_state.mode |= mode;
    } else {
        m_state.mode &= ~mode;
    }
    switch (mode) {
    case 6: // DECOM -- Origin Mode
        MODBIT(m_state.c.state, set, CursorOrigin);
        if (set) {
            _move_to(0, m_state.top);
        }
        break;

    case ModeWrap:
        // Line wrapping mode
        break;
    case ModeInsert:
        // Toggle insert mode
        break;
    case ModeAltscreen:
        if (set) {
            m_state.alt_lines.swap(m_state.lines);
            m_state.mode |= ModeAltscreen;
            m_scroll_offset = 0; // Reset scroll on entering alt screen
        } else {
            m_state.alt_lines.swap(m_state.lines);
            m_state.mode &= ~ModeAltscreen;
            m_scroll_offset = 0; // Reset scroll on exiting alt screen
        }
        std::fill(m_state.dirty.begin(), m_state.dirty.end(), true);
        break;
    case ModeCrlf:
        // Change line feed behavior
        break;
    case ModeEcho:
        // Local echo mode
        break;
    }
}

void Terminal::_tnewline(int first_col) {
    int y = m_state.c.y;

    if (y == m_state.bot) {
        _scroll_up(m_state.top, 1);
    } else {
        y++;
    }
    _move_to(first_col ? 0 : m_state.c.x, y);
}

void Terminal::_tstrsequence(uchar c) {
    // Handle different string sequences more comprehensively
    switch (c) {
    case 0x90: // DCS - Device Control String
    case 0x9d: // OSC - Operating System Command
    case 0x9e: // PM - Privacy Message
    case 0x9f: // APC - Application Program Command
        // TODO: Implement full handling of these sequences
        // This may involve buffering and processing multi-character sequences
        m_state.esc |= EscStr;
        break;
    }
}

void Terminal::_tmoveto(int x, int y) {
    _move_to(x, y); // Use the existing _move_to method
}

void Terminal::_tmoveato(int x, int y) {
    // Origin mode moves relative to scroll region
    if (m_state.c.state & CursorOrigin) {
        _move_to(x, y + m_state.top);
    } else {
        _move_to(x, y);
    }
}

void Terminal::_tputtab(int n) {
    int x = m_state.c.x;

    if (n > 0) {
        while (x < m_state.col && n--) {
            // Find next tab stop
            do {
                x++;
            } while (x < m_state.col && !m_state.tabs[x]);
        }
    } else if (n < 0) {
        while (x > 0 && n++) {
            // Find previous tab stop
            do {
                x--;
            } while (x > 0 && !m_state.tabs[x]);
        }
    }

    m_state.c.x = std::clamp(x, 0, m_state.col - 1);
}

void Terminal::_tsetmode(int priv, int set, const std::vector<int>& args) {
    // Mode setting per st.c
    int alt;

    for (int arg : args) {
        if (priv) {
            switch (arg) {
            case 1: // DECCKM -- Application cursor keys
                _set_mode(set, ModeAppcursor);
                break;
            case 5: // DECSCNM -- Reverse video
                // TODO: Implement screen reversal
                break;
            case 6: // DECOM -- Origin
                MODBIT(m_state.c.state, set, CursorOrigin);
                _tmoveato(0, 0);
                break;
            case 7: // DECAWM -- Auto wrap
                if (set) {
                    m_state.mode |= ModeWrap;
                } else {
                    m_state.mode &= ~ModeWrap;
                }
                break;
            case 0: // Error (IGNORED)
            case 2: // DECANM -- ANSI/VT52 (IGNORED)
            case 3: // DECCOLM -- Column  (IGNORED)
            case 4: // DECSCLM -- Scroll (IGNORED)
            case 8: // DECARM -- Auto repeat (IGNORED)
                break;
            case 25: // DECTCEM -- Text Cursor Enable Mode
                // Optional: handle cursor visibility
                break;
            case 47:   // swap screen
            case 1047: // alternate screen
            case 1049:
                alt = (m_state.mode & ModeAltscreen) != 0;
                if (set ^ alt) {
                    m_state.alt_lines.swap(m_state.lines);
                    m_state.mode ^= ModeAltscreen;
                }
            case 1048:
                (set) ? _cursor_save() : _cursor_load();
                break;
            case 2004: // Bracketed paste mode
                if (set) {
                    m_state.mode |= ModeBracketpaste;
                } else {
                    m_state.mode &= ~ModeBracketpaste;
                }
                break;
            }
        } else {
            switch (arg) {
            case 4: // IRM -- Insertion-replacement
                MODBIT(m_state.mode, set, ModeInsert);
                break;
            case 20: // LNM -- Linefeed/new line
                MODBIT(m_state.mode, set, ModeCrlf);
                break;
            }
        }
    }
}

void Terminal::_cursor_save() { m_saved_cursor = m_state.c; }

void Terminal::_cursor_load() {
    m_state.c = m_saved_cursor;
    _move_to(m_state.c.x, m_state.c.y);
}

void Terminal::_add_to_scrollback(const std::vector<Glyph>& line) {
    m_scrollback_buffer.push_back(line);
    if (m_scrollback_buffer.size() > m_max_scrollback_lines) {
        m_scrollback_buffer.erase(m_scrollback_buffer.begin());
    }
}

void Terminal::_add_to_scrollback(int cols, const VTermScreenCell* cells) {
    auto c = static_cast<size_t>(cols);
    std::vector<VTermScreenCell> line(cells, cells + c);
    m_sb_buffer.emplace_back(line);
    if (m_sb_buffer.size() > m_max_scrollback_lines) {
        m_sb_buffer.erase(m_sb_buffer.begin());
    }
}

int Terminal::_pop_from_scrollback(int cols, VTermScreenCell* cells) {
    if (m_sb_buffer.empty()) {
        return 0;
    }
    auto& back = m_sb_buffer.back();
    std::copy(back.begin(), back.end(), cells);
    m_sb_buffer.pop_back();
    return 1;
}

void Terminal::_scrollback_clear() { m_sb_buffer.clear(); }

void Terminal::_parse_csi_param(CSIEscape& csi) {
    char* p = csi.buf;
    csi.args.clear();

    // Check for private mode
    if (*p == '?') {
        csi.priv = 1;
        p++;
    } else {
        csi.priv = 0;
    }

    // Parse arguments
    while (p < csi.buf + csi.len) {
        // Parse numeric parameter
        int param = 0;
        while (p < csi.buf + csi.len && BETWEEN(*p, '0', '9')) {
            param = param * 10 + (*p - '0');
            p++;
        }
        csi.args.push_back(param);

        // Move to next parameter or end
        if (*p == ';') {
            p++;
        } else {
            break;
        }
    }
}
void Terminal::_handle_csi(const CSIEscape& csi) {
    LOG_DEBUG("CSI sequece: '{}' args: {}", csi.mode[0],
              fmt::join(csi.args, " "));

    switch (csi.mode[0]) {
    case '@': // ICH -- Insert <n> blank char
    {
        int n = csi.args.empty() ? 1 : csi.args[0];
        if (n < 1)
            n = 1;

        for (int i = m_state.col - 1; i >= m_state.c.x + n; i--)
            m_state.lines[m_state.c.y][i] = m_state.lines[m_state.c.y][i - n];

        _clear_region(m_state.c.x, m_state.c.y, m_state.c.x + n - 1,
                      m_state.c.y);
    } break;

    case 'A': // CUU -- Cursor <n> Up
    {
        int n = csi.args.empty() ? 1 : csi.args[0];

        if (n < 1)
            n = 1;
        _move_to(m_state.c.x, m_state.c.y - n);
    } break;

    case 'B': // CUD -- Cursor <n> Down
    {
        int n = csi.args.empty() ? 1 : csi.args[0];
        if (n < 1)
            n = 1;
        _move_to(m_state.c.x, m_state.c.y + n);
    } break;
    case 'e': // VPR -- Cursor <n> Down
    {
        int n = csi.args.empty() ? 1 : csi.args[0];
        if (n < 1)
            n = 1;
        _move_to(m_state.c.x, m_state.c.y + n);
    } break;

    case 'c': // DA -- Device Attributes
        if (csi.args.empty() || csi.args[0] == 0) {
            // Respond with xterm-like capabilities including 2004 (bracketed
            // paste)
            process_input("\033[?2004;1;6c"); // Indicate xterm with bracketed
                                              // paste support
        }
        break;

    case 'C': // CUF -- Cursor <n> Forward
    case 'a': // HPR -- Cursor <n> Forward
    {
        int n = csi.args.empty() ? 1 : csi.args[0];
        if (n < 1)
            n = 1;
        _move_to(m_state.c.x + n, m_state.c.y);
    } break;

    case 'D': // CUB -- Cursor <n> Backward
    {
        int n = csi.args.empty() ? 1 : csi.args[0];
        if (n < 1)
            n = 1;
        _move_to(m_state.c.x - n, m_state.c.y);
    } break;

    case 'E': // CNL -- Cursor <n> Down and first col
    {
        int n = csi.args.empty() ? 1 : csi.args[0];
        if (n < 1)
            n = 1;
        _move_to(0, m_state.c.y + n);
    } break;

    case 'F': // CPL -- Cursor <n> Up and first col
    {
        int n = csi.args.empty() ? 1 : csi.args[0];
        if (n < 1)
            n = 1;
        _move_to(0, m_state.c.y - n);
    } break;

    case 'g': // TBC -- Tabulation Clear
        switch (csi.args.empty() ? 0 : csi.args[0]) {
        case 0: // Clear current tab stop
            m_state.tabs[m_state.c.x] = false;
            break;
        case 3: // Clear all tab stops
            std::fill(m_state.tabs.begin(), m_state.tabs.end(), false);
            break;
        }
        break;

    case 'G': // CHA -- Cursor Character Absolute
    case '`': // HPA -- Horizontal Position Absolute
        if (!csi.args.empty()) {
            _move_to(csi.args[0] - 1, m_state.c.y);
        }
        break;

    case 'H': // CUP -- Move to <row> <col>
    case 'f': // HVP
    {
        int row = csi.args.size() > 0 ? csi.args[0] : 1;
        int col = csi.args.size() > 1 ? csi.args[1] : 1;
        _tmoveato(col - 1, row - 1);
    } break;

    case 'I': // CHT -- Cursor Forward Tabulation <n>
    {
        int n = csi.args.empty() ? 1 : csi.args[0];
        if (n < 1)
            n = 1;
        _tputtab(n);
    } break;

    case 'J': // ED -- Erase in Display
        switch (csi.args.empty() ? 0 : csi.args[0]) {
        case 0: // Below
            _clear_region(m_state.c.x, m_state.c.y, m_state.col - 1,
                          m_state.c.y);
            if (m_state.c.y < m_state.row - 1) {
                _clear_region(0, m_state.c.y + 1, m_state.col - 1,
                              m_state.row - 1);
            }
            break;
        case 1: // Above
            _clear_region(0, 0, m_state.col - 1, m_state.c.y - 1);
            _clear_region(0, m_state.c.y, m_state.c.x, m_state.c.y);
            break;
        case 2: // All
            _clear_region(0, 0, m_state.col - 1, m_state.row - 1);
            break;
        }
        break;

    case 'K': // EL -- Erase in Line
        switch (csi.args.empty() ? 0 : csi.args[0]) {
        case 0: // Right
            _clear_region(m_state.c.x, m_state.c.y, m_state.col - 1,
                          m_state.c.y);
            break;
        case 1: // Left
            _clear_region(0, m_state.c.y, m_state.c.x, m_state.c.y);
            break;
        case 2: // All
            _clear_region(0, m_state.c.y, m_state.col - 1, m_state.c.y);
            break;
        }
        break;

    case 'L': // IL -- Insert <n> blank lines
    {
        int n = csi.args.empty() ? 1 : csi.args[0];
        if (n < 1)
            n = 1;
        if (BETWEEN(m_state.c.y, m_state.top, m_state.bot)) {
            _scroll_down(m_state.c.y, n);
        }
    } break;

    case 'M': // DL -- Delete <n> lines
    {
        int n = csi.args.empty() ? 1 : csi.args[0];
        if (n < 1)
            n = 1;
        if (BETWEEN(m_state.c.y, m_state.top, m_state.bot)) {
            _scroll_up(m_state.c.y, n);
        }
    } break;

    case 'P': // DCH -- Delete <n> char
    {
        int n = csi.args.empty() ? 1 : csi.args[0];
        if (n < 1)
            n = 1;

        for (int i = m_state.c.x; i + n < m_state.col && i < m_state.col; i++) {
            m_state.lines[m_state.c.y][i] = m_state.lines[m_state.c.y][i + n];
        }

        _clear_region(m_state.col - n, m_state.c.y, m_state.col - 1,
                      m_state.c.y);
    } break;

    case 'S': // SU -- Scroll <n> lines up
    {
        int n = csi.args.empty() ? 1 : csi.args[0];
        if (n < 1)
            n = 1;
        _scroll_up(m_state.top, n);
    } break;

    case 'T': // SD -- Scroll <n> lines down
    {
        int n = csi.args.empty() ? 1 : csi.args[0];
        if (n < 1)
            n = 1;
        _scroll_down(m_state.top, n);
    } break;

    case 'X': // ECH -- Erase <n> char
    {
        int n = csi.args.empty() ? 1 : csi.args[0];
        if (n < 1)
            n = 1;
        _clear_region(m_state.c.x, m_state.c.y, m_state.c.x + n - 1,
                      m_state.c.y);
    } break;
    case 'Z': // CBT -- Cursor Backward Tabulation <n>
    {
        int n = csi.args.empty() ? 1 : csi.args[0];
        if (n < 1)
            n = 1;
        _tputtab(-n);
    } break;

    case 'd': // VPA -- Move to <row>
    {
        int n = csi.args.empty() ? 1 : csi.args[0];
        _tmoveato(m_state.c.x, n - 1);
    } break;

    case 'h': // SM -- Set Mode
        _tsetmode(csi.priv, 1, csi.args);
        break;

    case 'l': // RM -- Reset Mode
        _tsetmode(csi.priv, 0, csi.args);
        break;

    case 'm': // SGR -- Select Graphic Rendition
        _handle_sgr(csi.args);
        break;

    case 'n': // DSR -- Device Status Report
        switch (csi.args.empty() ? 0 : csi.args[0]) {
        case 5: // Operating status
            process_input("\033[0n");
            break;
        case 6: // Cursor position
        {
            char buf[40];
            snprintf(buf, sizeof(buf), "\033[%i;%iR", m_state.c.y + 1,
                     m_state.c.x + 1);
            process_input(buf);
        } break;
        }
        break;

    case 'r': // DECSTBM -- Set Scrolling Region
        if (csi.args.size() >= 2) {
            int top = csi.args[0] - 1;
            int bot = csi.args[1] - 1;

            if (BETWEEN(top, 0, m_state.row - 1) &&
                BETWEEN(bot, 0, m_state.row - 1) && top < bot) {
                m_state.top = top;
                m_state.bot = bot;
                if (m_state.c.state & CursorOrigin)
                    _move_to(0, m_state.top);
            }
        } else {
            // Reset to full screen when no args
            m_state.top = 0;
            m_state.bot = m_state.row - 1;
            if (m_state.c.state & CursorOrigin) {
                _move_to(0, m_state.top);
            }
        }
        break;

    case 's': // DECSC -- Save cursor position
        _cursor_save();
        break;

    case 'u': // DECRC -- Restore cursor position
        _cursor_load();
        break;
    }
}

void Terminal::_handle_sgr(const std::vector<int>& args) {
    size_t i;
    int32_t idx;

    if (args.empty()) {
        // Reset all attributes if no parameters
        m_state.c.attrs = 0;
        m_state.c.fg = m_default_color_map[7]; // Default foreground
        m_state.c.bg = m_default_color_map[0]; // Default background
        m_state.c.color_mode = ColorBasic;
        return;
    }

    for (i = 0; i < args.size(); i++) {
        int attr = args[i];
        switch (attr) {
        case 0: // Reset
            m_state.c.attrs = 0;
            m_state.c.fg = m_default_color_map[7]; // Default foreground
            m_state.c.bg = m_default_color_map[0]; // Default background
            m_state.c.color_mode = ColorBasic;
            break;
        case 1:
            m_state.c.attrs |= AttrBold;
            break;
        case 2:
            m_state.c.attrs |= AttrFaint;
            break;
        case 3:
            m_state.c.attrs |= AttrItalic;
            break;
        case 4:
            m_state.c.attrs |= AttrUnderline;
            break;
        case 5:
            m_state.c.attrs |= AttrBlink;
            break;
        case 7:
            m_state.c.attrs |= AttrReverse;
            break;
        case 8:
            m_state.c.attrs |= AttrInvisible;
            break;
        case 9:
            m_state.c.attrs |= AttrStruck;
            break;

        case 22:
            m_state.c.attrs &= ~(AttrBold | AttrFaint);
            break;
        case 23:
            m_state.c.attrs &= ~AttrItalic;
            break;
        case 24:
            m_state.c.attrs &= ~AttrUnderline;
            break;
        case 25:
            m_state.c.attrs &= ~AttrBlink;
            break;
        case 27:
            m_state.c.attrs &= ~AttrReverse;
            break;
        case 28:
            m_state.c.attrs &= ~AttrInvisible;
            break;
        case 29:
            m_state.c.attrs &= ~AttrStruck;
            break;

        // Foreground color
        case 30:
        case 31:
        case 32:
        case 33:
        case 34:
        case 35:
        case 36:
        case 37:
            m_state.c.fg = m_default_color_map[attr - 30];
            break;
        case 38:
            if (i + 2 < args.size()) {
                if (args[i + 1] == 5) { // 256 colors
                    i += 2;
                    m_state.c.color_mode = Color256;
                    if (args[i] < 16) {
                        m_state.c.fg = m_default_color_map[args[i]];
                    } else {
                        // Convert 256 color to RGB
                        uint8_t r = 0, g = 0, b = 0;
                        if (args[i] < 232) { // 216 colors: 16-231
                            uint8_t index = args[i] - 16;
                            r = (index / 36) * 51;
                            g = ((index / 6) % 6) * 51;
                            b = (index % 6) * 51;
                        } else { // Grayscale: 232-255
                            r = g = b = (args[i] - 232) * 11;
                        }
                        m_state.c.fg =
                            ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
                    }
                } else if (args[i + 1] == 2 && i + 4 < args.size()) { // RGB
                    i += 4;
                    m_state.c.color_mode = ColorTrue;
                    uint8_t r = args[i - 2];
                    uint8_t g = args[i - 1];
                    uint8_t b = args[i];
                    m_state.c.fg =
                        ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
                    m_state.c.true_color_fg = (r << 16) | (g << 8) | b;
                }
            }
            break;
        case 39: // Default foreground
            m_state.c.fg = m_default_color_map[7];
            m_state.c.color_mode = ColorBasic;
            break;

        // Background color
        case 40:
        case 41:
        case 42:
        case 43:
        case 44:
        case 45:
        case 46:
        case 47:
            m_state.c.bg = m_default_color_map[attr - 40];
            break;
        case 48:
            if (i + 2 < args.size()) {
                if (args[i + 1] == 5) { // 256 colors
                    i += 2;
                    if (args[i] < 16) {
                        m_state.c.bg = m_default_color_map[args[i]];
                    } else {
                        // Convert 256 color to RGB
                        uint8_t r = 0, g = 0, b = 0;
                        if (args[i] < 232) { // 216 colors: 16-231
                            uint8_t index = args[i] - 16;
                            r = (index / 36) * 51;
                            g = ((index / 6) % 6) * 51;
                            b = (index % 6) * 51;
                        } else { // Grayscale: 232-255
                            r = g = b = (args[i] - 232) * 11;
                        }
                        m_state.c.bg =
                            ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
                    }
                } else if (args[i + 1] == 2 && i + 4 < args.size()) { // RGB
                    i += 4;
                    uint8_t r = args[i - 2];
                    uint8_t g = args[i - 1];
                    uint8_t b = args[i];
                    m_state.c.bg =
                        ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
                    m_state.c.true_color_bg = (r << 16) | (g << 8) | b;
                }
            }
            break;
        case 49: // Default background
            m_state.c.bg = m_default_color_map[0];
            break;

        // Bright foreground colors
        case 90:
        case 91:
        case 92:
        case 93:
        case 94:
        case 95:
        case 96:
        case 97:
            m_state.c.fg = m_default_color_map[(attr - 90) + 8];
            break;

        // Bright background colors
        case 100:
        case 101:
        case 102:
        case 103:
        case 104:
        case 105:
        case 106:
        case 107:
            m_state.c.bg = m_default_color_map[(attr - 100) + 8];
            break;
        }
    }
}

void Terminal::_handle_control_code(uchar c) {
    switch (c) {
    case '\t': // HT - Horizontal Tab
        _tputtab(1);
        break;
    case '\b': // BS - Backspace
        if (m_state.c.x > 0) {
            m_state.c.x--;
            m_state.c.state &= ~CursorWrapnext;
        }
        break;
    case '\r': // CR - Carriage Return
        m_state.c.x = 0;
        m_state.c.state &= ~CursorWrapnext;
        break;
    case '\f': // FF - Form Feed
    case '\v': // VT - Vertical Tab
    case '\n': // LF - Line Feed
        if (m_state.c.y == m_state.bot) {
            _scroll_up(m_state.top, 1);
        } else {
            m_state.c.y++;
        }
        if (m_state.mode & ModeCrlf) {
            m_state.c.x = 0;
        }
        m_state.c.state &= ~CursorWrapnext;
        break;
    case '\a': // BEL - Bell
        _ring_bell();
        break;
    case 033: // ESC - Escape
        m_state.esc = EscStart;
        break;
    }
}

int Terminal::_eschandle(uchar ascii) {
    switch (ascii) {
    case '[':
        m_state.esc |= EscCsi;
        return 0;

    // Handle O sequence directly for cursor moves
    case 'O':
        return 0; // Keep processing to handle next char directly

    case 'A':                          // Cursor Up
        if (m_state.esc == EscStart) { // Direct O-sequence
            _tmoveto(m_state.c.x, m_state.c.y - 1);
            return 1;
        }
        break;

    case 'B': // Cursor Down
        if (m_state.esc == EscStart) {
            _tmoveto(m_state.c.x, m_state.c.y + 1);
            return 1;
        }
        break;
    case 'z':                      // DECID -- Identify Terminal
        process_input("\033[?6c"); // Respond as VT102
        break;
    case ']':
    case 'P':
    case '_':
    case '^':
    case 'k':
        _tstrsequence(ascii);
        return 0;
    case 'n':
        m_state.charset = 2;
        break;
    case 'o':
        m_state.charset = 3;
        break;
    case '(':
    case ')':
    case '*':
    case '+':
        m_state.icharset = ascii - '(';
        m_state.esc |= EscAltcharset;
        return 0;
    case 'D': // IND
        if (m_state.c.y == m_state.bot) {
            _scroll_up(m_state.top, 1);
        } else {
            m_state.c.y++;
        }
        break;
    case 'E': // NEL
        _tnewline(1);
        break;
    case 'H': // HTS
        m_state.tabs[m_state.c.x] = true;
        break;
    case 'M': // RI
        if (m_state.c.y == m_state.top) {
            _scroll_down(m_state.top, 1);
        } else {
            m_state.c.y--;
        }
        break;
    case 'Z': // DECID
        process_input("\033[?6c");
        break;
    case 'c': // RIS
        _reset();
        break;
    case '=': // DECKPAM
        _set_mode(true, ModeAppcursor);
        break;
    case '>': // DECKPNM
        _set_mode(false, ModeAppcursor);
        break;
    case '7': // DECSC
        _cursor_save();
        break;
    case '8': // DECRC
        _cursor_load();
        break;
    case '\\':
        break;
    default:
        LOG_ERROR("ESC unhandled: ESC '{:c}'", ascii);
        break;
    }
    return 1;
}

void Terminal::_handle_dcs() const {
    // Basic DCS sequence handling
    // This function is called when DCS sequences are received
    // For now, we'll only implement some basic DCS handling

    // Extract DCS sequence from strescseq
    if (m_strescseq.buf.empty()) {
        return;
    }

    // Example DCS sequence handling:
    // $q - DECRQSS (Request Status String)
    if (m_strescseq.buf.length() >= 2 && m_strescseq.buf.substr(0, 2) == "$q") {
        std::string param = m_strescseq.buf.substr(2);
        // Handle DECRQSS request
        if (param == "\"q") { // DECSCA
            process_input(
                "\033P1$r0\"q\033\\"); // Reply with default protection
        } else if (param == "r") {     // DECSTBM
            char response[40];
            snprintf(response, sizeof(response), "\033P1$r%d;%dr\033\\",
                     m_state.top + 1, m_state.bot + 1);
            process_input(response);
        }
    }
}

void Terminal::_selection_normalize() {
    // Existing normalization logic
    if (m_selection.type == SelectionRegular &&
        m_selection.ob.y != m_selection.oe.y) {
        m_selection.nb.x = m_selection.ob.y < m_selection.oe.y
                               ? m_selection.ob.x
                               : m_selection.oe.x;
        m_selection.ne.x = m_selection.ob.y < m_selection.oe.y
                               ? m_selection.oe.x
                               : m_selection.ob.x;
    } else {
        m_selection.nb.x = std::min(m_selection.ob.x, m_selection.oe.x);
        m_selection.ne.x = std::max(m_selection.ob.x, m_selection.oe.x);
    }
    m_selection.nb.y = std::min(m_selection.ob.y, m_selection.oe.y);
    m_selection.ne.y = std::max(m_selection.ob.y, m_selection.oe.y);

    // Clamp X coordinates to terminal dimensions
    m_selection.nb.x = std::clamp(m_selection.nb.x, 0, m_state.col - 1);
    m_selection.ne.x = std::clamp(m_selection.ne.x, 0, m_state.col - 1);

    // Don't clamp Y coordinates to allow scrollback selection
    // Y coordinates can be negative for scrollback lines
}

void Terminal::_strparse() {
    // Parse string sequences into arguments
    m_strescseq.args.clear();
    std::string current;

    for (size_t i = 0; i < m_strescseq.len; i++) {
        char c = m_strescseq.buf[i];
        if (c == ';') {
            m_strescseq.args.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        m_strescseq.args.push_back(current);
    }
}

void Terminal::_handle_string_sequence() {
    if (m_strescseq.len == 0) {
        return;
    }

    switch (m_strescseq.type) {
    case ']': // OSC - Operating System Command
        if (m_strescseq.args.size() >= 2) {
            int cmd =
                std::atoi(m_strescseq.args[0].c_str()); // NOLINT(cert-err34-c)
            switch (cmd) {
            case 0: // Set window title and icon name
            case 1: // Set icon name
            case 2: // Set window title
                // You would implement window title setting here
                // For now, we'll just print it
                LOG_INFO("Title: {}", m_strescseq.args[1]);
                break;

            case 4: // Set/get color
                _handle_osc_color(m_strescseq.args);
                break;

            case 52: // Manipulate selection data
                _handle_osc_selection(m_strescseq.args);
                break;
            }
        }
        break;

    case 'P': // DCS - Device Control String
        _handle_dcs();
        break;

    case '_': // APC - Application Program Command
        // Not commonly used, implement if needed
        break;

    case '^': // PM - Privacy Message
        // Not commonly used, implement if needed
        break;

    case 'k': // Old title set compatibility
        // Set window title using old xterm sequence
        LOG_INFO("Old Title: {}", m_strescseq.buf);
        break;
    }
}

void Terminal::_handle_osc_color(const std::vector<std::string>& args) const {
    if (args.size() < 2)
        return;

    int index = std::atoi(args[1].c_str()); // NOLINT(cert-err34-c)
    if (args.size() > 2) {
        // Set color
        if (args[2][0] == '?') {
            // Color query - respond with current color
            char response[64];
            snprintf(response, sizeof(response),
                     "\033]4;%d;rgb:%.2X/%.2X/%.2X\007", index,
                     static_cast<int>(m_state.c.fg.x * 255),
                     static_cast<int>(m_state.c.fg.y * 255),
                     static_cast<int>(m_state.c.fg.z * 255));
            process_input(response);
        } else {
            // Set color - parse color value (typically in rgb:RR/GG/BB format)
            // Implementation would go here
        }
    }
}

void Terminal::
    _handle_osc_selection( // NOLINT(readability-convert-member-functions-to-static)
        const std::vector<std::string>& args) {
    if (args.size() < 3) {
        return;
    }

    // args[1] would contain the selection type (clipboard, primary, etc)
    // args[2] would contain the base64-encoded data

    // Example implementation:
    if (args[1] == "c") {    // clipboard
        std::string decoded; // You would implement base64 decoding
        ImGui::SetClipboardText(decoded.c_str());
    }
}

void Terminal::_ring_bell() {
    // Implement visual bell
    if (m_state.mode & ModeVisualbell) {
        // Briefly invert screen colors
        for (auto& line : m_state.lines) {
            for (auto& glyph : line) {
                std::swap(glyph.fg, glyph.bg);
            }
        }
        // Schedule screen restoration
        std::thread([this]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            for (auto& line : m_state.lines) {
                for (auto& glyph : line) {
                    std::swap(glyph.fg, glyph.bg);
                }
            }
        }).detach();
    } else {
        // System bell or audio bell
        // Implement platform-specific bell
    }
}

size_t Terminal::_utf8_decode(const char* c, Rune* u, size_t clen) {
    *u = g_utf_invalid;
    size_t len = 0;
    Rune udecoded = 0;

    // Determine sequence length and initial byte decoding
    if ((c[0] & 0x80) == 0) {
        // ASCII character
        *u = static_cast<uchar>(c[0]);
        return 1;
    } else if ((c[0] & 0xE0) == 0xC0) {
        // 2-byte sequence
        len = 2;
        udecoded = c[0] & 0x1F;
    } else if ((c[0] & 0xF0) == 0xE0) {
        // 3-byte sequence (used by box drawing characters)
        len = 3;
        udecoded = c[0] & 0x0F;
    } else if ((c[0] & 0xF8) == 0xF0) {
        // 4-byte sequence
        len = 4;
        udecoded = c[0] & 0x07;
    } else {
        LOG_ERROR("Invalid UTF-8 start byte: 0x{:x}", static_cast<int>(c[0]));
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
        if ((c[i] & 0xC0) != 0x80) {
            LOG_ERROR("Invalid continuation byte at position {}: 0x{:x}", i,
                      static_cast<int>(c[i]));
            return 0;
        }

        // Shift and add continuation byte
        udecoded = (udecoded << 6) | (c[i] & 0x3F);
    }

    // Additional validation for decoded Unicode point
    if (!BETWEEN(udecoded,         // NOLINT(readability-simplify-boolean-expr)
                 g_utfmin[len],    // NOLINT(readability-simplify-boolean-expr)
                 g_utfmax[len]) || // NOLINT(readability-simplify-boolean-expr)
        BETWEEN(udecoded, 0xD800, 0xDFFF) ||
        udecoded > 0x10FFFF) {
        LOG_ERROR("Invalid Unicode code point : U+{:x}", udecoded);
        *u = g_utf_invalid;
        return 0;
    }

    *u = udecoded;
    return len;
}

size_t Terminal::_utf8_encode(Rune u, char* c) {
    size_t len = 0;

    if (u < 0x80) {
        c[0] = u;
        len = 1;
    } else if (u < 0x800) {
        c[0] = 0xC0 | (u >> 6);
        c[1] = 0x80 | (u & 0x3F);
        len = 2;
    } else if (u < 0x10000) {
        c[0] = 0xE0 | (u >> 12);
        c[1] = 0x80 | ((u >> 6) & 0x3F);
        c[2] = 0x80 | (u & 0x3F);
        len = 3;
    } else {
        c[0] = 0xF0 | (u >> 18);
        c[1] = 0x80 | ((u >> 12) & 0x3F);
        c[2] = 0x80 | ((u >> 6) & 0x3F);
        c[3] = 0x80 | (u & 0x3F);
        len = 4;
    }

    return len;
}

#pragma region vterm callbacks
int Terminal::_vterm_settermprop(VTermProp prop, VTermValue* val, void* data) {
    // TODO: other prop.
    auto* self = static_cast<Terminal*>(data);
    switch (prop) {
    case VTERM_PROP_ALTSCREEN:
        self->_set_mode(val->boolean, ModeAltscreen);
        break;
    default:
        return 0;
    }
    return 1;
}

int Terminal::_vterm_damage(VTermRect rect, void* data) {
    auto* self = static_cast<Terminal*>(data);
    self->_clear_region(rect.start_col, rect.start_row, rect.end_col,
                        rect.end_row);
    return 1;
}

int Terminal::_vterm_moverect(VTermRect dest, VTermRect src, void* data) {
    auto* self = static_cast<Terminal*>(data);
    self->_clear_region(std::min(dest.start_col, src.start_col),
                        std::min(dest.start_row, src.start_row),
                        std::max(dest.end_col, src.end_col),
                        std::max(dest.end_row, src.end_row));
    return 1;
}

int Terminal::_vterm_movecursor(VTermPos new_pos, VTermPos old_pos, int visible,
                                void* data) {
    auto* self = static_cast<Terminal*>(data);
    self->_move_to(new_pos.col, new_pos.row);
    return 1;
}

int Terminal::_vterm_bell(void* data) {
    auto* self = static_cast<Terminal*>(data);
    self->_ring_bell();
    return 1;
}

int Terminal::_vterm_sb_pushline(int cols, const VTermScreenCell* cells,
                                 void* data) {
    auto* self = static_cast<Terminal*>(data);
    self->_add_to_scrollback(cols, cells);
    return 1;
}

int Terminal::_vterm_sb_popline(int cols, VTermScreenCell* cells, void* data) {
    auto* self = static_cast<Terminal*>(data);
    return self->_pop_from_scrollback(cols, cells);
}

int Terminal::_vterm_sb_clear(void* data) {
    auto* self = static_cast<Terminal*>(data);
    self->_scrollback_clear();
    return 1;
}

void Terminal::_vterm_output(const char* s, size_t len, void* data) {
    auto* self = static_cast<Terminal*>(data);
    self->m_pty->write(s, len);
}
#pragma endregion
} // namespace ImNeovim
