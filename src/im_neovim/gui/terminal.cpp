#include "im_neovim/gui/terminal.h"
#include "im_neovim/logging.h"
#include <csignal>
#if defined(__unix__)
#include <errno.h>
#include <fcntl.h>
#include <limits.h> // For PATH_MAX (on Linux)
#include <pwd.h>    // For getpwuid
#include <stdlib.h> // For getenv, setenv, unsetenv, realpath
#include <string.h> // For strrchr, strcpy, strncpy, strerror
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace ImNeovim {
#define BETWEEN(x, a, b) ((a) <= (x) && (x) <= (b))

Terminal::Terminal() : m_window_title("Terminal") {
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
}

Terminal::~Terminal() {
    m_should_terminate = true;
    /* We need to write somethting into the `m_pty_fd`,
     * otherwise the `m_read_thread` can't be joined.
     */
    process_input("exit\r");
    if (m_read_thread.joinable()) {
        m_read_thread.join();
    }
    if (m_pty_fd >= 0) {
        close(m_pty_fd);
    }
    if (m_child_pid > 0) {
        kill(m_child_pid, SIGTERM);
    }
}

void Terminal::render() {
    if (!m_is_visible) {
        return;
    }
    if (m_pty_fd < 0) {
        _start_shell();
    }

    _check_font_size_changed();
    bool window_created = _setup_window();

    // Only render terminal content if window is open and not collapsed
    if (window_created && (m_is_embedded || !m_embedded_window_collapsed)) {
        _handle_terminal_resize();
        _render_buffer();
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
    struct winsize ws = {};
    ws.ws_row = m_state.row;
    ws.ws_col = m_state.col;
    ioctl(m_pty_fd, TIOCSWINSZ, &ws); // Set master side size

    LOG_DEBUG("Terminal resized to {}x{}", cols, rows);
}

void Terminal::process_input(const std::string& input) const {
    if (m_pty_fd < 0)
        return;
    if (m_state.mode & ModeBracketpaste) {
        if (input.substr(0, 4) == "\033[200~") {
            write(m_pty_fd, input.c_str(), input.length());
            return;
        }
        if (input.substr(0, 4) == "\033[201~") {
            write(m_pty_fd, input.c_str(), input.length());
            return;
        }
    }
    if (m_state.mode & ModeAppcursor) {
        if (input == "\033[A") {
            write(m_pty_fd, "\033OA", 3); // Up
            return;
        }
        if (input == "\033[B") {
            write(m_pty_fd, "\033OB", 3); // Down
            return;
        }
        if (input == "\033[C") {
            write(m_pty_fd, "\033OC", 3); // Right
            return;
        }
        if (input == "\033[D") {
            write(m_pty_fd, "\033OD", 3); // Left
            return;
        }
    }

    if (input == "\r\n" || input == "\n") {
        write(m_pty_fd, "\r", 1);
        return;
    }

    if (input == "\b") {
        write(m_pty_fd, "\b \b", 3);
        return;
    }

    write(m_pty_fd, input.c_str(), input.length());
}

bool Terminal::selected_text(int x, int y) {
    if (m_selection.mode == SelectionIdle || m_selection.ob.x == -1 ||
        m_selection.alt != (m_state.mode & ModeAltscreen)) {
        return false;
    }

    // Convert coordinates to absolute buffer positions
    int actual_y = m_scrollback_buffer.size() + y;
    int sel_start_y = m_scrollback_buffer.size() + m_selection.nb.y;
    int sel_end_y = m_scrollback_buffer.size() + m_selection.ne.y;

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

void Terminal::_start_shell() {
    // Open PTY master
    m_pty_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (m_pty_fd < 0) {
        LOG_CRITICAL("Failed to call posix_openpt!");
        return;
    }
    if (grantpt(m_pty_fd) < 0) {
        LOG_CRITICAL("Failed to call grantpt!");
        close(m_pty_fd);
        m_pty_fd = -1;
        return;
    }
    if (unlockpt(m_pty_fd) < 0) {
        LOG_CRITICAL("Failed to call unlockpt!");
        close(m_pty_fd);
        m_pty_fd = -1;
        return;
    }
    char* slave_name = ptsname(m_pty_fd);
    if (!slave_name) {
        LOG_CRITICAL("Failed to get the slave's name!");
        close(m_pty_fd);
        m_pty_fd = -1;
        return;
    }

    m_child_pid = fork();

    if (m_child_pid < 0) {
        LOG_CRITICAL("Failed to fork current process!");
        close(m_pty_fd);
        m_pty_fd = -1;
        return;
    }

    if (m_child_pid == 0) {
        /* We are now in the forked child process. */
        close(m_pty_fd); // Close master PTY in child

        if (setsid() < 0) {
            // Create new session, detach from parent's controlling TTY
            LOG_CRITICAL("Failed to call setsid!");
            exit(EXIT_FAILURE);
        }

        // Open slave PTY
        int slave_fd = open(slave_name, O_RDWR);
        if (slave_fd < 0) {
            LOG_CRITICAL("Failed to slave PTY!");
            exit(EXIT_FAILURE);
        }

        // Make the slave PTY the controlling terminal for this new session
        if (ioctl(slave_fd, TIOCSCTTY, 0) < 0) {
            // This can fail if the process is not a session leader and already
            // has a controlling TTY. setsid() should make us a session leader.
            LOG_WARN("ioctl TIOCSCTTY failed (can be non-fatal depending on "
                     "context)");
        }

        dup2(slave_fd, STDIN_FILENO);
        dup2(slave_fd, STDOUT_FILENO);
        dup2(slave_fd, STDERR_FILENO);

        if (slave_fd > STDERR_FILENO) {
            close(slave_fd);
        }

        // Configure terminal modes for the slave PTY
        struct termios tios;
        if (tcgetattr(STDIN_FILENO, &tios) < 0) {
            LOG_CRITICAL("tcgetattr failed on slave pty");
            exit(EXIT_FAILURE);
        }

        // Set reasonable default modes (from st/typical terminal settings)
        tios.c_iflag = ICRNL | IXON | IXANY | IMAXBEL | BRKINT;
#if defined(IUTF8) // Common on Linux, good to enable if available
        tios.c_iflag |= IUTF8;
#endif
        tios.c_oflag =
            OPOST |
            ONLCR; // OPOST: enable output processing, ONLCR: map NL to CR-NL

        tios.c_cflag &= ~(CSIZE | PARENB); // Clear size and parity bits
        tios.c_cflag |= CS8;               // 8 bits per character
        tios.c_cflag |= CREAD;             // Enable receiver
        tios.c_cflag |= HUPCL; // Hang up on last close (sends SIGHUP to
                               // foreground process group)

        // Standard local modes for interactive shells
        tios.c_lflag =
            ICANON | ISIG | IEXTEN | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE;

        if (tcsetattr(STDIN_FILENO, TCSANOW, &tios) < 0) {
            LOG_CRITICAL("tcsetattr failed on slave pty");
            exit(EXIT_FAILURE);
        }

        // Set window size
        struct winsize ws = {};
        ws.ws_row =
            m_state.row; // Use initial rows/cols from Terminal state object
        ws.ws_col = m_state.col;
        if (ioctl(STDIN_FILENO, TIOCSWINSZ, &ws) < 0) {
            LOG_WARN("ioctl TIOCSWINSZ failed on slave pty (non-fatal, shell "
                     "might misbehave)");
        }

        // Prepare environment for the shell
        setenv("TERM", "xterm-256color", 1);
        // Unsetting these is often good as the login shell will set them
        // appropriately
        unsetenv("COLUMNS");
        unsetenv("LINES");
        // Optionally, clear other inherited variables that might cause issues,
        // e.g., unsetenv("TERMCAP"); unsetenv("WINDOWID"); // Common in some
        // terminal emulators

        // Logic for Linux and other Unix-like systems (also launch as login
        // shell)
        char shell_path_buf[PATH_MAX];
        const char* shell_env_val =
            getenv("SHELL"); // SHELL env var is primary on Linux

        if (shell_env_val && shell_env_val[0] != '\0') {
            strncpy(shell_path_buf, shell_env_val, sizeof(shell_path_buf));
            shell_path_buf[sizeof(shell_path_buf) - 1] = '\0';
        } else {
            struct passwd* pw_linux =
                getpwuid(getuid()); // Fallback to passwd entry
            if (pw_linux && pw_linux->pw_shell &&
                pw_linux->pw_shell[0] != '\0') {
                strncpy(shell_path_buf, pw_linux->pw_shell,
                        sizeof(shell_path_buf));
                shell_path_buf[sizeof(shell_path_buf) - 1] = '\0';
            } else {
                strncpy(shell_path_buf, "/bin/bash",
                        sizeof(shell_path_buf)); // Absolute fallback for Linux
                shell_path_buf[sizeof(shell_path_buf) - 1] = '\0';
            }
        }

        char shell_argv0_login_linux[PATH_MAX + 1];
        shell_argv0_login_linux[0] = '-';
        const char* shell_basename_linux = strrchr(shell_path_buf, '/');
        if (shell_basename_linux) {
            strncpy(shell_argv0_login_linux + 1, shell_basename_linux + 1,
                    sizeof(shell_argv0_login_linux) - 2);
        } else {
            strncpy(shell_argv0_login_linux + 1, shell_path_buf,
                    sizeof(shell_argv0_login_linux) - 2);
        }
        shell_argv0_login_linux[sizeof(shell_argv0_login_linux) - 1] = '\0';

        char* const new_argv_linux[] = {shell_argv0_login_linux, nullptr};

#if defined(IM_NVIM_DEBUG)
        LOG_INFO("[TERMINAL DEBUG] Linux/Other Shell Launch Information:");
        struct passwd* pw_linux_debug = getpwuid(getuid());
        LOG_INFO("  User's pw_shell (from getpwuid): '{}'",
                 (pw_linux_debug && pw_linux_debug->pw_shell)
                     ? pw_linux_debug->pw_shell
                     : "(not found or empty)");
        const char* env_shell_child_linux = getenv("SHELL");
        LOG_INFO("  getenv(\"SHELL\") in child process: '{}'",
                 env_shell_child_linux ? env_shell_child_linux
                                       : "(not set or empty)");
        LOG_INFO("  Path to be executed (shell_path_buf): '{}'",
                 shell_path_buf);
        LOG_INFO("  argv[0] for child shell (new_argv_linux[0]): '{}'",
                 new_argv_linux[0] ? new_argv_linux[0] : "(NULL)");
#endif

        execv(shell_path_buf, new_argv_linux);

        LOG_CRITICAL(
            "FATAL: Failed to execv shell '{}' (intended argv[0]='{}'): {}",
            shell_path_buf, new_argv_linux[0] ? new_argv_linux[0] : "(null)",
            strerror(errno));
        exit(127); // Standard exit code for command not found / exec failure
    }

    m_read_thread = std::thread(&Terminal::_read_output, this);
}

void Terminal::_read_output() {
    char buffer[4096];
    while (!m_should_terminate) {
        ssize_t bytes_read = read(m_pty_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
            std::lock_guard<std::mutex> lock(m_buffer_mutex);
            _write_to_buffer(buffer, bytes_read);
        } else if (bytes_read < 0 && errno != EINTR) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void Terminal::_write_to_buffer(const char* data, size_t length) {}

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
            const Glyph& glyph = m_state.lines[y][x];
            if (glyph.mode & AttrWdummy) {
                continue;
            }

            ImVec2 char_pos(pos.x + x * char_width, pos.y + y * line_height);
            _render_glyph(draw_list, glyph, char_pos, char_width, line_height);

            if (glyph.mode & AttrWide) {
                x++;
            }
        }
    }

    // Draw cursor
    if (ImGui::IsWindowFocused()) {
        ImVec2 cursor_pos(pos.x + m_state.c.x * char_width,
                          pos.y + m_state.c.y * line_height);
        float alpha = (sin(ImGui::GetTime() * 3.14159f) * 0.3f) + 0.5f;
        _render_cursor(draw_list, cursor_pos,
                       m_state.lines[m_state.c.y][m_state.c.x], char_width,
                       line_height, alpha);
    }
}

void Terminal::_render_main_screen(ImDrawList* draw_list, const ImVec2& pos,
                                   float char_width, float line_height) {
    ImVec2 content_size = ImGui::GetContentRegionAvail();
    int visible_rows =
        std::max(1, static_cast<int>(content_size.y / line_height));
    int total_lines = m_scrollback_buffer.size() + m_state.row;

    // Handle scrollback clamping
    int max_scroll = std::max(0, total_lines - visible_rows);
    m_scroll_offset = std::clamp(m_scroll_offset, 0, max_scroll);
    int start_line = std::max(0, total_lines - visible_rows - m_scroll_offset);

    // Handle selection highlighting
    if (m_selection.mode != SelectionIdle && m_selection.ob.x != -1) {
        _render_selection_highlight(draw_list, pos, char_width, line_height,
                                    start_line, start_line + visible_rows,
                                    m_scrollback_buffer.size());
    }

    // Draw content
    for (int vis_y = 0; vis_y < visible_rows; vis_y++) {
        int current_line = start_line + vis_y;
        const std::vector<Glyph>* line = nullptr;

        if (current_line < m_scrollback_buffer.size()) {
            line = &m_scrollback_buffer[current_line];
        } else {
            int screen_y = current_line - m_scrollback_buffer.size();
            if (screen_y >= 0 && screen_y < m_state.lines.size()) {
                line = &m_state.lines[screen_y];
            }
        }

        if (!line) {
            continue;
        }

        for (int x = 0; x < m_state.col && x < line->size(); x++) {
            const Glyph& glyph = (*line)[x];
            if (glyph.mode & AttrWdummy) {
                continue;
            }

            ImVec2 char_pos(pos.x + x * char_width,
                            pos.y + vis_y * line_height);
            _render_glyph(draw_list, glyph, char_pos, char_width, line_height);

            if (glyph.mode & AttrWide) {
                x++;
            }
        }
    }

    // Draw cursor when not scrolled
    if (ImGui::IsWindowFocused() && m_scroll_offset == 0) {
        ImVec2 cursor_pos(pos.x + m_state.c.x * char_width,
                          pos.y + (visible_rows -
                                   (total_lines - m_scrollback_buffer.size()) +
                                   m_state.c.y) *
                                      line_height);
        float alpha = (sin(ImGui::GetTime() * 3.14159f) * 0.3f) + 0.5f;
        _render_cursor(draw_list, cursor_pos,
                       m_state.lines[m_state.c.y][m_state.c.x], char_width,
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
                int selection_y =
                    screen_offset + screen_y - m_scrollback_buffer.size();
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
                scrollback_index < m_scrollback_buffer.size()) {
                for (int x = 0; x < m_state.col; x++) {
                    // Convert visible coordinate to selection coordinate system
                    int selection_y =
                        screen_offset + screen_y - m_scrollback_buffer.size();
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

void Terminal::_render_cursor(ImDrawList* draw_list, const ImVec2& cursor_pos,
                              const Glyph& cursor_cell, float char_width,
                              float line_height, float alpha) const {
    if (m_state.mode & ModeInsert) {
        draw_list->AddRectFilled(
            cursor_pos, ImVec2(cursor_pos.x + 2, cursor_pos.y + line_height),
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.7f, 0.7f, 0.7f, alpha)));
    } else if (cursor_cell.u != 0) {
        char text[g_utf_size] = {0};
        _utf8_encode(cursor_cell.u, text);
        ImVec4 bg = cursor_cell.fg;
        ImVec4 fg = cursor_cell.bg;

        draw_list->AddRectFilled(
            cursor_pos,
            ImVec2(cursor_pos.x + char_width, cursor_pos.y + line_height),
            ImGui::ColorConvertFloat4ToU32(ImVec4(bg.x, bg.y, bg.z, alpha)));
        draw_list->AddText(cursor_pos, ImGui::ColorConvertFloat4ToU32(fg),
                           text);
    } else {
        draw_list->AddRectFilled(
            cursor_pos,
            ImVec2(cursor_pos.x + char_width, cursor_pos.y + line_height),
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.7f, 0.7f, 0.7f, alpha)));
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
} // namespace ImNeovim
