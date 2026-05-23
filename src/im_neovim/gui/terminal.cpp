#include "im_neovim/gui/terminal.h"
#include "im_neovim/logging.h"
#include <fmt/ranges.h>
#include <type_traits>

namespace ImNeovim {
#define BETWEEN(x, a, b) ((a) <= (x) && (x) <= (b))

Terminal::Terminal() : m_dark_mode(true) {
    m_window_title = "Terminal";
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
    m_state.dirty.resize(m_state.row, true);

    // Create VTerm instance
    m_vterm = vterm_new(m_state.row, m_state.col);
    vterm_set_utf8(m_vterm, 1);
    // Get screen and set up callbacks BEFORE enabling
    m_vterm_screen = vterm_obtain_screen(m_vterm);
    vterm_screen_enable_altscreen(m_vterm_screen, 1);
    vterm_screen_enable_reflow(m_vterm_screen, false);

    m_vterm_screen_callbacks.damage = _vterm_damage;
    m_vterm_screen_callbacks.moverect = _vterm_moverect;
    m_vterm_screen_callbacks.movecursor = _vterm_movecursor;
    m_vterm_screen_callbacks.settermprop = _vterm_settermprop;
    m_vterm_screen_callbacks.bell = _vterm_bell;
    m_vterm_screen_callbacks.resize = nullptr;
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

    _check_font_size_changed();
    bool window_created = TextWidget::setup_window();

    // Only render terminal content if window is open and not collapsed
    if (window_created && (m_is_embedded || !m_embedded_window_collapsed)) {
        ImGuiIO& io = ImGui::GetIO();
        _handle_terminal_resize();

        // Defer PTY launch until the content area has genuinely reached a
        // usable size.  We check the *raw* content dimensions (before the
        // minimum-size clamp) so that we don't launch the shell into a tiny
        // area that merely happens to be clamped up to 10×5.  This prevents
        // the banner / initial output from being truncated and pushed to
        // scrollback before the ImGui dockspace layout stabilizes.
        if (!m_pty->is_valid()) {
            ImVec2 content_size = ImGui::GetContentRegionAvail();
            float char_width = ImGui::GetFontBaked()->GetCharAdvance('M');
            float line_height = ImGui::GetTextLineHeight();
            int raw_cols = static_cast<int>(content_size.x / char_width);
            int raw_rows = static_cast<int>(content_size.y / line_height);

            if (raw_cols >= g_min_term_cols && raw_rows >= g_min_term_rows) {
                _start_shell();
            }
        }

        _render_buffer();
        _handle_scrollback(io, m_state.row);
        _handle_mouse_input(io);
        _handle_keyboard_input(io);
    }

    // Always call End() when Begin() was called, per ImGui requirements
    if (!m_is_embedded) {
        ImGui::End();
    }
}

void Terminal::resize(int cols, int rows) {
    std::lock_guard<std::mutex> lock(m_buffer_mutex);
    // Get actual content area size
    ImVec2 content_size = ImGui::GetContentRegionAvail();
    float char_width = ImGui::GetFontBaked()->GetCharAdvance('M');
    float line_height = ImGui::GetTextLineHeight();

    // Calculate new dimensions based on actual font metrics,
    // enforcing a minimum usable size to prevent truncation
    // during early frames when the layout is not yet stable.
    int new_cols = std::max(g_min_term_cols,
                            static_cast<int>(content_size.x / char_width));
    int new_rows = std::max(g_min_term_rows,
                            static_cast<int>(content_size.y / line_height));

    // Only resize if dimensions actually changed
    if (new_cols == m_state.col && new_rows == m_state.row) {
        return;
    }

    // Use the calculated (and clamped) values consistently
    int clamped_cols = new_cols;
    int clamped_rows = new_rows;

    // Create new buffers
    std::vector<bool> new_dirty(clamped_rows, true);

    // Update terminal state
    m_state.row = clamped_rows;
    m_state.col = clamped_cols;
    m_state.top = 0;
    m_state.bot = clamped_rows - 1;

    // Then clamp to ensure validity
    m_state.top = std::clamp(m_state.top, 0, clamped_rows - 1);
    m_state.bot = std::clamp(m_state.bot, m_state.top, clamped_rows - 1);
    if (m_state.bot < m_state.top) {
        m_state.bot = m_state.top;
    }

    // Swap in new buffers
    m_state.dirty = std::move(new_dirty);

    // Ensure cursor stays within bounds
    m_state.c.x = std::min(m_state.c.x, clamped_cols - 1);
    m_state.c.y = std::min(m_state.c.y, clamped_rows - 1);

    // Update PTY size if valid
    if (m_pty->is_valid()) {
        m_pty->resize(m_state.row, m_state.col);
    }
    vterm_set_size(m_vterm, m_state.row, m_state.col);
    vterm_screen_flush_damage(m_vterm_screen);

    LOG_DEBUG("Terminal resized to {}x{}", clamped_cols, clamped_rows);
}

void Terminal::process_input(const std::string& input) const {
    if (!m_pty->is_valid()) {
        return;
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
    if (text == nullptr) {
        return;
    }

    m_pty->write(text, strlen(text));
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
}

void Terminal::_check_font_size_changed() {
    float current_font_size = ImGui::GetFontBaked()->Size;
    if (current_font_size != m_last_font_size) {
        m_last_font_size = current_font_size;
        resize(m_state.col, m_state.row);
    }
}

// Helper to convert VTermScreenCell to ScreenCell
void Terminal::_vterm_cell_to_screen_cell(VTermScreenCell& vterm_cell,
                                          ScreenCell& screen_cell) {
    // Copy characters
    for (int i = 0; i < 4; i++) {
        screen_cell.chars[i] = vterm_cell.chars[i];
    }
    screen_cell.width = static_cast<unsigned char>(vterm_cell.width);

    // Handle colors
    _handle_vterm_cell_colors(vterm_cell, screen_cell.fg, screen_cell.bg);

    // Copy attributes
    screen_cell.bold = vterm_cell.attrs.bold;
    screen_cell.italic = vterm_cell.attrs.italic;
    screen_cell.underline = vterm_cell.attrs.underline;
    screen_cell.undercurl = false; // VTerm doesn't have undercurl
    screen_cell.reverse = vterm_cell.attrs.reverse;
}

void Terminal::_handle_terminal_resize() {
    ImVec2 content_size = ImGui::GetContentRegionAvail();
    float char_width = ImGui::GetFontBaked()->GetCharAdvance('M');
    float line_height = ImGui::GetTextLineHeight();

    int new_cols = std::max(g_min_term_cols,
                            static_cast<int>(content_size.x / char_width));
    int new_rows = std::max(g_min_term_rows,
                            static_cast<int>(content_size.y / line_height));

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
#if defined(IM_APP_DARWIN)
    if (io.KeySuper) {
#else
    if (io.KeyCtrl) {
#endif
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
#if !defined(IM_APP_WIN32)
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
    if (mod != VTERM_MOD_NONE && io.InputQueueCharacters.Size == 0 &&
        ImGui::IsKeyPressed(ImGuiKey_C)) {
        // Ctrl + C
        vterm_keyboard_unichar(m_vterm, 'c', mod);
    }
    for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
        const auto& cc = io.InputQueueCharacters[i];
        char c = static_cast<char>(io.InputQueueCharacters[i]);
        if (c != 0) {
#if defined(IM_APP_WIN32)
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
#if defined(IM_APP_WIN32)
            }
#endif
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

void Terminal::_render_vterm_cell(ImDrawList* draw_list, VTermScreenCell& cell,
                                  const ImVec2& char_pos, float char_width,
                                  float line_height) {
    ScreenCell screen_cell;
    _vterm_cell_to_screen_cell(cell, screen_cell);
    TextWidget::render_cell(draw_list, screen_cell, char_pos, char_width,
                            line_height);
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
    ScreenCell screen_cell;
    _vterm_cell_to_screen_cell(cursor_cell, screen_cell);

    // Override cursor color with alpha blending
    ImVec4 cursor_color{m_dark_mode ? 0.7f : 0.3f, m_dark_mode ? 0.7f : 0.3f,
                        m_dark_mode ? 0.7f : 0.3f, alpha};

    if (screen_cell.chars[0] != '\0') {
        // Draw cursor background
        draw_list->AddRectFilled(
            cursor_pos,
            ImVec2(cursor_pos.x + char_width, cursor_pos.y + line_height),
            ImGui::ColorConvertFloat4ToU32(cursor_color));

        // Draw the character
        char text[g_utf_size] = {0};
        size_t len = 0;
        for (int i = 0; i < screen_cell.width && i < 4; i++) {
            len += TextWidget::utf8_encode(screen_cell.chars[i], &text[len]);
        }
        draw_list->AddText(
            cursor_pos, ImGui::ColorConvertFloat4ToU32(screen_cell.fg), text);
    } else {
        // Just draw cursor
        draw_list->AddRectFilled(
            cursor_pos,
            ImVec2(cursor_pos.x + char_width, cursor_pos.y + line_height),
            ImGui::ColorConvertFloat4ToU32(cursor_color));
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
                len += utf8_encode(cell->chars[i], &buf[len]);
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
        m_state.dirty[y] = true;
    }
}

void Terminal::_move_to(int x, int y) {
    int miny, maxy;

    // Get scroll region bounds
    miny = 0;
    maxy = m_state.row - 1;

    int oldx = m_state.c.x;
    int oldy = m_state.c.y;

    // Constrain cursor position
    m_state.c.x = std::clamp(x, 0, m_state.col - 1);
    m_state.c.y = std::clamp(y, miny, maxy);
}

void Terminal::_set_mode(bool set, int mode) {
    if (set) {
        m_state.mode |= mode;
    } else {
        m_state.mode &= ~mode;
    }
    switch (mode) {
    case ModeAltscreen:
        if (set) {
            m_state.mode |= ModeAltscreen;
            m_scroll_offset = 0; // Reset scroll on entering alt screen
        } else {
            m_state.mode &= ~ModeAltscreen;
            m_scroll_offset = 0; // Reset scroll on exiting alt screen
        }
        std::fill(m_state.dirty.begin(), m_state.dirty.end(), true);
        break;
    }
}

void Terminal::_cursor_save() { m_saved_cursor = m_state.c; }

void Terminal::_cursor_load() {
    m_state.c = m_saved_cursor;
    _move_to(m_state.c.x, m_state.c.y);
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
    size_t to_copy = std::min(static_cast<size_t>(cols), back.size());
    std::copy(back.begin(), back.begin() + to_copy, cells);
    // Zero-fill remaining cells, ensuring width=1 to avoid
    // infinite loops in libvterm's for(pos.col += width) iteration
    for (size_t i = to_copy; i < static_cast<size_t>(cols); i++) {
        memset(&cells[i], 0, sizeof(VTermScreenCell));
        cells[i].width = 1;
    }
    m_sb_buffer.pop_back();
    return 1;
}

void Terminal::_scrollback_clear() { m_sb_buffer.clear(); }

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

void Terminal::_ring_bell() const {
    // System bell or audio bell
    // Implement platform-specific bell
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
