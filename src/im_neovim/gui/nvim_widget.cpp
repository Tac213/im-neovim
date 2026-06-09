#include "im_neovim/gui/nvim_widget.h"
#include "im_neovim/globals.h"
#include "im_neovim/gui/nvim_input.h"
#include "im_neovim/logging.h"
#include <algorithm>
#include <cmath>
#include <im_app/file_system.h>
#include <im_app/font_manager.h>
#include <imgui_internal.h>

namespace {

// djb2 compile-time string hash for O(1) switch-based dispatch.
[[nodiscard]] constexpr uint32_t _hash(std::string_view s) noexcept {
    uint32_t h = 5381;
    for (char c : s)
        h = ((h << 5) + h) + static_cast<uint32_t>(c);
    return h;
}

/// Decode a Neovim handle (Window, Buffer, Tabpage) from a msgpack object.
/// Neovim serializes handles as EXT types: fixext1 for small values (-0x1f to
/// 0x7f) or ext8+ for larger values. Also handles plain integers for backward
/// compatibility.
[[nodiscard]] inline int64_t _extract_handle(const msgpack::object& obj) {
    if (obj.type == msgpack::type::POSITIVE_INTEGER) {
        return static_cast<int64_t>(obj.as<uint64_t>());
    }
    if (obj.type == msgpack::type::NEGATIVE_INTEGER) {
        return obj.as<int64_t>();
    }
    if (obj.type == msgpack::type::EXT) {
        // Neovim EXT format: [type_byte][payload].
        // msgpack-c's via.ext.ptr points to the type byte; via.ext.size is
        // the payload-only length (type byte already excluded by msgpack-c).
        // fixext1 (size==1): payload is a raw signed byte.
        // ext8+   (size>=1): payload is a msgpack-encoded uint.
        const char* payload = obj.via.ext.ptr + 1; // skip type byte
        uint32_t payload_size = obj.via.ext.size;
        if (payload_size == 0) {
            return 0;
        }
        if (payload_size == 1) {
            // fixext 1: single signed byte payload
            return static_cast<int64_t>(
                static_cast<int8_t>(obj.via.ext.ptr[1]));
        }
        // ext 8+: msgpack-encoded uint
        msgpack::unpacked result;
        std::size_t off = 0;
        msgpack::unpack(result, payload, payload_size, off);
        msgpack::object inner = result.get();
        if (inner.type == msgpack::type::POSITIVE_INTEGER) {
            return static_cast<int64_t>(inner.as<uint64_t>());
        }
        if (inner.type == msgpack::type::NEGATIVE_INTEGER) {
            return inner.as<int64_t>();
        }
    }
    return 0; // Fallback: unrecognized type
}

} // namespace

namespace ImNeovim {

// Forward declaration: win_extmark events are intentionally not handled —
// signs and virtual text are rendered into grid cells by Neovim and delivered
// via standard grid_line events. See the definition below for details.
static void _redraw_win_extmark(msgpack::object_array& args);

// Forward declaration: update_menu events are a stub — menu bar rendering
// will be implemented later when multiple nvim widgets are supported.
static void _redraw_update_menu(msgpack::object_array& args);

// HighlightAttr implementation
NvimWidget::HighlightAttr::HighlightAttr()
    : fg(1.0f, 1.0f, 1.0f, 1.0f), bg(0.0f, 0.0f, 0.0f, 1.0f),
      sp(1.0f, 0.0f, 0.0f, 1.0f), bold(false), italic(false), underline(false),
      undercurl(false), reverse(false) {}

// Grid implementation
NvimWidget::Grid::Grid() : id(1), width(80), height(24) {
    resize(width, height);
}

void NvimWidget::Grid::clear() {
    for (auto& row : cells) {
        for (auto& cell : row) {
            cell.clear();
        }
    }
}

void NvimWidget::Grid::resize(uint32_t w, uint32_t h) {
    width = w;
    height = h;
    cells.resize(h);
    for (auto& row : cells) {
        row.resize(w);
    }
    m_scroll_region.top = 0;
    m_scroll_region.bot = h;
    m_scroll_region.left = 0;
    m_scroll_region.right = w;
}

void NvimWidget::Grid::scroll_region(int count) {
    uint32_t top = m_scroll_region.top;
    uint32_t bot = m_scroll_region.bot;
    uint32_t left = m_scroll_region.left;
    uint32_t right = m_scroll_region.right;

    // Clamp to grid bounds
    if (top >= height)
        top = 0;
    if (bot > height)
        bot = height;
    if (bot <= top)
        return;
    if (left >= width)
        left = 0;
    if (right > width)
        right = width;
    if (right <= left)
        return;

    if (count > 0) {
        // Scroll up: shift rows [top+count, bot) to [top, bot-count)
        for (uint32_t y = top; y + count < bot; y++) {
            for (uint32_t x = left; x < right; x++) {
                cells[y][x] = cells[y + count][x];
            }
        }
        // Clear vacated rows at the bottom
        for (uint32_t y =
                 (bot > static_cast<uint32_t>(count) ? bot - count : top);
             y < bot; y++) {
            for (uint32_t x = left; x < right; x++) {
                cells[y][x].clear();
            }
        }
    } else if (count < 0) {
        uint32_t abs_count = static_cast<uint32_t>(-count);
        // Scroll down: shift rows [top, bot-abs_count) to [top+abs_count, bot)
        for (uint32_t y = bot - 1; y >= top + abs_count && y < bot; y--) {
            for (uint32_t x = left; x < right; x++) {
                cells[y][x] = cells[y - abs_count][x];
            }
        }
        // Clear vacated rows at the top
        for (uint32_t y = top; y < top + abs_count && y < bot; y++) {
            for (uint32_t x = left; x < right; x++) {
                cells[y][x].clear();
            }
        }
    }
}

NvimWidget::NvimWidget() {
    m_window_title = "nvim (no file)";
    m_window_icon.clear();

    // Use the first workspace folder (or home directory if empty).
    m_nvim_cwd = ImApp::path_to_string(g_workspace.first_folder_or_home());

    // Initialize with safe default size
    m_state.row = 24;
    m_state.col = 80;

    // Initialize default colors
    m_default_fg = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    m_default_bg = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    m_default_sp = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);

    // Initialize current highlight
    m_current_hl.fg = m_default_fg;
    m_current_hl.bg = m_default_bg;
    m_current_hl.sp = m_default_sp;

    m_display_rows = m_state.row;

    // Create default grid
    Grid default_grid;
    default_grid.id = 1;
    default_grid.resize(m_state.col, m_state.row);
    m_grids[1] = std::move(default_grid);

    m_nvim_proc.data = nullptr;
    m_nvim_proc.pid = 0;
    m_in_pipe.data = nullptr;
    m_out_pipe.data = nullptr;
}

NvimWidget::~NvimWidget() {
    if (m_nvim_proc.pid > 0) {
        if (!m_nvim_exited) {
            // Nvim is still running — kill it first.
            uv_process_kill(&m_nvim_proc, SIGKILL);
        }
        // Always close the process handle from the destructor (never from
        // inside the exit callback, where libuv's exit_cb_pending is set).
        uv_close(reinterpret_cast<uv_handle_t*>(&m_nvim_proc), nullptr);
        m_nvim_proc.data = nullptr;
        m_nvim_proc.pid = 0;
    }
    if (m_in_pipe.data != nullptr) {
        uv_close(reinterpret_cast<uv_handle_t*>(&m_in_pipe), nullptr);
        m_in_pipe.data = nullptr;
    }
    if (m_out_pipe.data != nullptr) {
        uv_close(reinterpret_cast<uv_handle_t*>(&m_out_pipe), nullptr);
        m_out_pipe.data = nullptr;
    }
    m_requests.clear();
}

void NvimWidget::open_file(const std::filesystem::path& path) {
    // If the current buffer is modified, queue the save dialog instead
    // of immediately opening the new file.
    if (m_buffer_modified) {
        m_pending_file_path = path;
        m_save_dialog_action = SaveDialogAction::OpenFile;
        _show_save_modal();
        return;
    }
    _do_open_file(path);
}

void NvimWidget::_do_open_file(const std::filesystem::path& path, bool force) {
    // Ensure the window is visible
    set_visible(true);
    m_window_open = true;

    // Build the nvim command with UTF-8 path (forward slashes).
    std::string escaped_path = ImApp::path_to_string(path);
    std::replace(escaped_path.begin(), escaped_path.end(), '\\', '/');

    // Build the edit command — use 'edit!' when discarding changes
    std::string cmd = force ? "edit! " : "edit ";
    cmd += escaped_path;

    // Extract the base filename for the window title.
    std::string filename = ImApp::path_to_string(path.filename());

    // Send the command to nvim via nvim_command
    auto request = start_nvim_request(
        "nvim_command", 1,
        [this, filename](msgpack::object&) {
            m_window_title = filename;
            m_buffer_modified = false;
            m_needs_modified_check = true;
            LOG_DEBUG("File opened successfully: {}", filename);
        },
        [](int32_t error_code, const std::string& error_msg) {
            LOG_ERROR("Failed to open file: {} - {}", error_code, error_msg);
        });

    if (request) {
        request->arg_str(cmd.size());
        request->arg_str_body(cmd.data(), cmd.size());
    }
}

void NvimWidget::render() {
    if (m_nvim_proc.pid == 0) {
        _spawn_nvim();
    }

    // Process any pending font reload BEFORE any ImGui window operations.
    // Font atlas clear/reload must happen outside of
    // ImGui::NewFrame()..EndFrame().
    // _check_font_reload_needed() only parses/detects changes.
    // The actual Clear()+Load is done by process_pending_font_reload()
    // from LayerMainWindow::on_update() (between frames).
    _check_font_reload_needed();

    // Check if we need to query the buffer modified state
    if (m_needs_modified_check) {
        m_needs_modified_check = false;
        _query_buffer_modified();
    }

    _check_font_size_changed();
    bool window_created = TextWidget::setup_window();

    // Only render content if window is open and not collapsed
    if (window_created && (m_is_embedded || !m_embedded_window_collapsed)) {
        _handle_nvim_resize();
        _handle_keyboard_input();
        _handle_mouse_input();

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float char_width = ImGui::GetFontBaked()->GetCharAdvance('M');
        float line_height = ImGui::GetTextLineHeight();

        _render_grid(draw_list, pos, char_width, line_height);

        if (m_msg_visible || m_msg_history_visible || m_msg_showmode_visible) {
            _render_message_area(draw_list, pos, char_width, line_height);
        }

        if (m_cmdline_block_visible) {
            _render_cmdline_block(draw_list, pos, char_width, line_height);
        }

        if (m_cmdline_visible) {
            _render_cmdline(draw_list, pos, char_width, line_height);
        }

        if (m_popup_visible && !m_popup_items.empty()) {
            _render_popup_menu(draw_list, pos, char_width, line_height);
        }

        _update_ime_position();
    }

    // Render save dialog modal (outside window content check so it appears
    // even when the window is being closed)
    if (m_show_save_dialog) {
        _render_save_modal();
    }

    // Always call End() when Begin() was called, per ImGui requirements
    if (!m_is_embedded) {
        ImGui::End();
    }
}

std::vector<uint32_t> NvimWidget::_collect_visible_layers() const {
    std::vector<uint32_t> layers;

    // Gather all visible non-floating grids sorted by position
    for (const auto& [grid_id, info] : m_windows) {
        if (!info.visible) {
            continue;
        }
        auto grid_it = m_grids.find(grid_id);
        if (grid_it == m_grids.end()) {
            continue;
        }
        if (info.floating) {
            continue; // Floaters handled below
        }
        layers.push_back(grid_id);
    }

    // Ensure grid 1 is always rendered first as the base
    bool has_grid1 = false;
    for (auto id : layers) {
        if (id == 1) {
            has_grid1 = true;
            break;
        }
    }
    if (!has_grid1 && m_grids.find(1) != m_grids.end()) {
        layers.insert(layers.begin(), 1);
    }

    // Sort non-floating grids by row, then col
    std::sort(layers.begin(), layers.end(), [this](uint32_t a, uint32_t b) {
        auto it_a = m_windows.find(a);
        auto it_b = m_windows.find(b);
        if (it_a == m_windows.end() || it_b == m_windows.end()) {
            return a < b;
        }
        if (it_a->second.start_row != it_b->second.start_row) {
            return it_a->second.start_row < it_b->second.start_row;
        }
        return it_a->second.start_col < it_b->second.start_col;
    });

    // Gather floating grids sorted by compindex (ascending → later = on top)
    std::vector<std::pair<int, uint32_t>> floating;
    for (const auto& [grid_id, info] : m_windows) {
        if (!info.visible || !info.floating) {
            continue;
        }
        auto grid_it = m_grids.find(grid_id);
        if (grid_it == m_grids.end()) {
            continue;
        }
        floating.emplace_back(info.compindex, grid_id);
    }
    std::sort(floating.begin(), floating.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    for (const auto& [comp, grid_id] : floating) {
        layers.push_back(grid_id);
    }

    return layers;
}

void NvimWidget::_render_grid_layer(ImDrawList* draw_list, const Grid& grid,
                                    const ImVec2& origin, float char_width,
                                    float line_height, bool render_cursor) {
    float effective_line_height = line_height + static_cast<float>(m_linespace);

    // Bottom overlays (message area, cmdline block, cmdline) occupy dedicated
    // rows below the grid (reserved via m_display_rows - m_state.row).  The
    // grid itself is rendered in full — no rows are subtracted.

    // Draw all cells
    for (uint32_t y = 0; y < grid.height; y++) {
        bool skip_next = false;
        for (uint32_t x = 0; x < grid.width; x++) {
            if (skip_next) {
                skip_next = false;
                continue;
            }

            ImVec2 char_pos(origin.x + x * char_width,
                            origin.y + y * effective_line_height);
            bool wide_rendered = TextWidget::render_cell(
                draw_list, grid.cells[y][x], char_pos, char_width, line_height);
            if (wide_rendered) {
                skip_next = true;
            }
        }
    }

    // Draw cursor
    if (render_cursor && ImGui::IsWindowFocused() && m_nvim_attached) {
        bool show_cursor = true;
        if (m_cursor_blinkon > 0 || m_cursor_blinkoff > 0) {
            double now = ImGui::GetTime();
            double elapsed_ms = (now - m_last_blink_time) * 1000.0;

            if (elapsed_ms < static_cast<double>(m_cursor_blinkwait)) {
                show_cursor = true;
            } else {
                double blink_elapsed =
                    elapsed_ms - static_cast<double>(m_cursor_blinkwait);
                uint32_t cycle =
                    static_cast<uint32_t>(m_cursor_blinkon + m_cursor_blinkoff);
                if (cycle == 0) {
                    show_cursor = true;
                } else {
                    double cycle_pos =
                        fmod(blink_elapsed, static_cast<double>(cycle));
                    show_cursor =
                        cycle_pos < static_cast<double>(m_cursor_blinkon);
                }
            }
        }

        if (show_cursor) {
            ImVec2 cursor_pos(origin.x + m_state.cursor_x * char_width,
                              origin.y +
                                  m_state.cursor_y * effective_line_height);

            bool cursor_is_wide = false;
            ScreenCell cursor_cell;
            if (m_state.cursor_y < grid.height &&
                m_state.cursor_x < grid.width) {
                cursor_cell = grid.cells[m_state.cursor_y][m_state.cursor_x];
                cursor_is_wide = TextWidget::is_wide_char(cursor_cell.chars[0]);
            }

            float cursor_width =
                cursor_is_wide ? char_width * 2.0f : char_width;

            ImVec2 cursor_min = cursor_pos;
            ImVec2 cursor_max(cursor_pos.x + cursor_width,
                              cursor_pos.y + line_height);

            float pct = static_cast<float>(m_cursor_cell_percentage) / 100.0f;
            switch (m_cursor_shape) {
            case CursorShape::Horizontal:
                cursor_min.y = cursor_pos.y + line_height * (1.0f - pct);
                break;
            case CursorShape::Vertical:
                cursor_max.x = cursor_pos.x + cursor_width * pct;
                break;
            case CursorShape::Block:
                break;
            }

            ImVec4 cursor_color{m_dark_mode ? 0.7f : 0.3f,
                                m_dark_mode ? 0.7f : 0.3f,
                                m_dark_mode ? 0.7f : 0.3f, 0.8f};
            if (m_busy) {
                cursor_color.w = 0.4f;
            }

            if (cursor_cell.chars[0] != '\0') {
                draw_list->AddRectFilled(
                    cursor_min, cursor_max,
                    ImGui::ColorConvertFloat4ToU32(cursor_color));

                char text[TextWidget::g_utf_size] = {0};
                size_t len = 0;
                for (int i = 0; i < cursor_cell.width && i < 4; i++) {
                    len += TextWidget::utf8_encode(cursor_cell.chars[i],
                                                   &text[len]);
                }
                draw_list->AddText(
                    cursor_pos, ImGui::ColorConvertFloat4ToU32(cursor_cell.fg),
                    text);
            } else {
                draw_list->AddRectFilled(
                    cursor_min, cursor_max,
                    ImGui::ColorConvertFloat4ToU32(cursor_color));
            }
        }
    }
}

void NvimWidget::_render_grid(ImDrawList* draw_list, const ImVec2& pos,
                              float char_width, float line_height) {
    if (!m_multigrid_enabled) {
        // Fallback: single-grid rendering (legacy path)
        auto it = m_grids.find(m_current_grid);
        if (it == m_grids.end()) {
            return;
        }
        _render_grid_layer(draw_list, it->second, pos, char_width, line_height,
                           true);
        return;
    }

    // Composited multi-grid rendering
    auto layers = _collect_visible_layers();
    float effective_line_height = line_height + static_cast<float>(m_linespace);

    Grid* main_grid = nullptr;
    auto main_it = m_grids.find(1);
    if (main_it != m_grids.end()) {
        main_grid = &main_it->second;
    }

    for (size_t i = 0; i < layers.size(); i++) {
        uint32_t grid_id = layers[i];
        auto grid_it = m_grids.find(grid_id);
        if (grid_it == m_grids.end() || !grid_it->second.visible) {
            continue;
        }

        auto win_it = m_windows.find(grid_id);
        ImVec2 origin = pos;
        if (win_it != m_windows.end() && win_it->second.floating) {
            // Floating grids: positioned at their screen_row / screen_col
            origin.x = pos.x + win_it->second.start_col * char_width;
            origin.y = pos.y + win_it->second.start_row * effective_line_height;
        } else if (win_it != m_windows.end()) {
            // Non-floating grids at their win_pos position
            origin.x = pos.x + win_it->second.start_col * char_width;
            origin.y = pos.y + win_it->second.start_row * effective_line_height;
        }

        bool render_cursor = (grid_id == m_current_grid);
        _render_grid_layer(draw_list, grid_it->second, origin, char_width,
                           line_height, render_cursor);
    }

    // Visual bell flash (over the entire display area including overlay rows)
    if (m_bell_pending && main_grid) {
        double elapsed = ImGui::GetTime() - m_bell_timestamp;
        if (elapsed < 0.2) {
            float alpha = 0.15f * (1.0f - static_cast<float>(elapsed / 0.2));
            ImVec2 grid_end(pos.x + main_grid->width * char_width,
                            pos.y + m_display_rows * effective_line_height);
            draw_list->AddRectFilled(pos, grid_end,
                                     ImGui::ColorConvertFloat4ToU32(
                                         ImVec4(1.0f, 1.0f, 1.0f, alpha)));
        } else {
            m_bell_pending = false;
        }
    }
}

void NvimWidget::_render_cmdline(ImDrawList* draw_list, const ImVec2& pos,
                                 float char_width, float line_height) {
    auto it = m_grids.find(m_current_grid);
    if (it == m_grids.end()) {
        return;
    }

    Grid& grid = it->second;
    float effective_line_height = line_height + static_cast<float>(m_linespace);

    // The cmdline occupies the absolute last row of the widget, positioned
    // relative to m_display_rows (total area including reserved overlay rows).
    float cmdline_y = pos.y + (m_display_rows - 1) * effective_line_height;

    // Build the full display text and apply highlighting chunks.
    std::string full_text;
    for (const auto& chunk : m_cmdline_content) {
        full_text += chunk.text;
    }

    // Draw prompt (if any) before the content.
    // The indent applies to the content, not the prompt or firstc.
    float x = pos.x;
    ImU32 prompt_color = ImGui::ColorConvertFloat4ToU32(m_default_fg);
    if (!m_cmdline_prompt.empty()) {
        draw_list->AddText(ImVec2(x, cmdline_y), prompt_color,
                           m_cmdline_prompt.c_str());
        x += ImGui::CalcTextSize(m_cmdline_prompt.c_str()).x;
    }

    // Draw firstc (e.g. ':'). Neovim sends it separately from content.
    if (!m_cmdline_firstc.empty()) {
        ImU32 firstc_color = ImGui::ColorConvertFloat4ToU32(m_default_fg);
        draw_list->AddText(ImVec2(x, cmdline_y), firstc_color,
                           m_cmdline_firstc.c_str());
        x += ImGui::CalcTextSize(m_cmdline_firstc.c_str()).x;
    }

    // Apply cmdline indent to content.
    x += static_cast<float>(m_cmdline_indent) * char_width;

    // Draw each content chunk with its highlight attribute.
    for (const auto& chunk : m_cmdline_content) {
        ImVec4 fg = m_default_fg;
        ImVec4 bg = m_default_bg;

        if (chunk.hl_id != 0) {
            auto hl_it = m_hl_attrs.find(chunk.hl_id);
            if (hl_it != m_hl_attrs.end()) {
                fg = hl_it->second.fg;
                bg = hl_it->second.bg;
            }
        }

        if (chunk.text.empty()) {
            continue;
        }

        // Draw background for this chunk (fills the line height).
        float chunk_width = ImGui::CalcTextSize(chunk.text.c_str()).x;
        ImVec2 bg_min(x, cmdline_y);
        ImVec2 bg_max(x + chunk_width, cmdline_y + line_height);
        draw_list->AddRectFilled(bg_min, bg_max,
                                 ImGui::ColorConvertFloat4ToU32(bg));

        ImU32 text_color = ImGui::ColorConvertFloat4ToU32(fg);
        draw_list->AddText(ImVec2(x, cmdline_y), text_color,
                           chunk.text.c_str());
        x += chunk_width;
    }

    // Draw special character indicator if present (e.g. after CTRL-V).
    if (!m_cmdline_special_char.empty()) {
        ImU32 special_color =
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.9f, 0.9f, 0.3f, 1.0f));
        std::string indicator = "^" + m_cmdline_special_char;
        draw_list->AddText(ImVec2(x, cmdline_y), special_color,
                           indicator.c_str());
        x += ImGui::CalcTextSize(indicator.c_str()).x;
    }

    // Draw cursor when the window is focused.
    if (ImGui::IsWindowFocused() && m_nvim_attached) {
        // Blink timing (shared with main grid cursor).
        bool show_cursor = true;
        if (m_cursor_blinkon > 0 || m_cursor_blinkoff > 0) {
            double now = ImGui::GetTime();
            double elapsed_ms = (now - m_last_blink_time) * 1000.0;

            if (elapsed_ms < static_cast<double>(m_cursor_blinkwait)) {
                show_cursor = true;
            } else {
                double blink_elapsed =
                    elapsed_ms - static_cast<double>(m_cursor_blinkwait);
                uint32_t cycle =
                    static_cast<uint32_t>(m_cursor_blinkon + m_cursor_blinkoff);
                if (cycle == 0) {
                    show_cursor = true;
                } else {
                    double cycle_pos =
                        fmod(blink_elapsed, static_cast<double>(cycle));
                    show_cursor =
                        cycle_pos < static_cast<double>(m_cursor_blinkon);
                }
            }
        }

        if (show_cursor) {
            // Cursor position is anchored at indent + prompt + firstc.
            float cursor_base_x =
                pos.x + static_cast<float>(m_cmdline_indent) * char_width;
            if (!m_cmdline_prompt.empty()) {
                cursor_base_x +=
                    ImGui::CalcTextSize(m_cmdline_prompt.c_str()).x;
            }

            // firstc is rendered before content chunks — include its
            // width in the cursor base so that m_cmdline_pos=1 lands
            // right after the firstc.
            float firstc_width = 0.0f;
            if (!m_cmdline_firstc.empty()) {
                firstc_width = ImGui::CalcTextSize(m_cmdline_firstc.c_str()).x;
            }

            // m_cmdline_pos is a 0-based index into cmdbuff: the
            // character the cursor sits on.  Position the cursor AFTER
            // that character, i.e. after (m_cmdline_pos + 1) characters
            // from the start.
            // We render firstc separately, then content chunks (with
            // the firstc stripped from the first chunk).  Walk the
            // rendered pieces and stop after target_chars characters.
            int target_chars = m_cmdline_pos + 1;
            float cursor_x = cursor_base_x;
            int chars_walked = 0;
            bool found = false;
            ImVec4 cursor_text_fg = m_default_fg; // fg to use for cursor char

            // firstc
            if (!m_cmdline_firstc.empty() && chars_walked < target_chars) {
                chars_walked++;
                if (chars_walked == target_chars) {
                    cursor_x += firstc_width;
                    found = true;
                } else {
                    cursor_x += firstc_width;
                }
            }

            // content chunks
            if (!found) {
                for (size_t ci = 0; ci < m_cmdline_content.size(); ci++) {
                    std::string chunk_text = m_cmdline_content[ci].text;

                    int chunk_len = static_cast<int>(chunk_text.length());
                    int remaining = target_chars - chars_walked;
                    if (remaining <= chunk_len) {
                        std::string prefix = chunk_text.substr(0, remaining);
                        cursor_x += ImGui::CalcTextSize(prefix.c_str()).x;

                        // Record the chunk's foreground for cursor-char
                        // rendering.
                        if (remaining > 0 && ci < m_cmdline_content.size() &&
                            m_cmdline_content[ci].hl_id != 0) {
                            auto hl_it =
                                m_hl_attrs.find(m_cmdline_content[ci].hl_id);
                            if (hl_it != m_hl_attrs.end()) {
                                cursor_text_fg = hl_it->second.fg;
                            }
                        }
                        found = true;
                        break;
                    }
                    cursor_x += ImGui::CalcTextSize(chunk_text.c_str()).x;
                    chars_walked += chunk_len;
                }
            }

            // If target_chars is past all rendered characters, cursor
            // rests at the end (cursor_x already points there).
            if (!found) {
                // Cursor at end of all text; no highlight lookup needed.
            }

            ImVec2 cursor_min(cursor_x, cmdline_y);
            ImVec2 cursor_max(cursor_x + char_width, cmdline_y + line_height);

            float pct = static_cast<float>(m_cursor_cell_percentage) / 100.0f;
            switch (m_cursor_shape) {
            case CursorShape::Horizontal:
                cursor_min.y = cmdline_y + line_height * (1.0f - pct);
                break;
            case CursorShape::Vertical:
                cursor_max.x = cursor_x + char_width * pct;
                break;
            case CursorShape::Block:
                // Full cell, no adjustment needed.
                break;
            }

            ImVec4 cursor_color{m_dark_mode ? 0.7f : 0.3f,
                                m_dark_mode ? 0.7f : 0.3f,
                                m_dark_mode ? 0.7f : 0.3f, 0.8f};
            if (m_busy) {
                cursor_color.w = 0.4f;
            }

            draw_list->AddRectFilled(
                cursor_min, cursor_max,
                ImGui::ColorConvertFloat4ToU32(cursor_color));

            // Cursor-on-character: only when the cursor is positioned
            // ON a character (mid-string), not past the end.  Since we
            // currently position the cursor after the m_cmdline_pos-th
            // character, there is no character to render for end-of-line
            // typing.  TODO: adjust for mid-string cursor navigation.
            if (m_cmdline_pos >= 0 &&
                static_cast<size_t>(m_cmdline_pos) + 1 < full_text.length()) {
                int char_index = m_cmdline_pos;
                const char* ptr = full_text.c_str();
                const char* end = ptr + full_text.length();
                int char_idx = 0;
                uint32_t rune = 0;
                size_t clen = 0;
                while (ptr < end && char_idx <= char_index) {
                    clen = TextWidget::utf8_decode(ptr, &rune, end - ptr);
                    if (clen == 0) {
                        break;
                    }
                    if (char_idx == char_index) {
                        break;
                    }
                    ptr += clen;
                    char_idx++;
                }
                if (char_idx == char_index && clen > 0) {
                    char text[TextWidget::g_utf_size] = {0};
                    TextWidget::utf8_encode(rune, text);
                    draw_list->AddText(
                        cursor_min,
                        ImGui::ColorConvertFloat4ToU32(cursor_text_fg), text);
                }
            }
        }
    }
}

void NvimWidget::_render_popup_menu(ImDrawList* draw_list, const ImVec2& pos,
                                    float char_width, float line_height) {
    float effective_line_height = line_height + static_cast<float>(m_linespace);

    // Position popup below the anchor row
    float menu_x = pos.x + m_popup_anchor_col * char_width;
    float menu_y = pos.y + (m_popup_anchor_row + 1) * effective_line_height;

    // Build display strings and measure widest item
    std::vector<std::string> display_strings;
    display_strings.reserve(m_popup_items.size());
    float max_width = 100.0f; // minimum width

    for (const auto& item : m_popup_items) {
        std::string display = item.text;
        if (!item.kind.empty()) {
            display += " [" + item.kind + "]";
        }
        display_strings.push_back(display);

        float w = ImGui::CalcTextSize(display.c_str()).x;
        if (w > max_width) {
            max_width = w;
        }
    }

    // Add padding
    float menu_width = max_width + ImGui::GetStyle().WindowPadding.x * 2.0f;
    float menu_height =
        static_cast<float>(m_popup_items.size()) * effective_line_height +
        ImGui::GetStyle().WindowPadding.y * 2.0f;

    // Constrain to grid bounds
    auto it = m_grids.find(m_current_grid);
    if (it != m_grids.end()) {
        float grid_right = pos.x + it->second.width * char_width;
        if (menu_x + menu_width > grid_right) {
            menu_x = grid_right - menu_width;
        }
        if (menu_x < pos.x) {
            menu_x = pos.x;
            menu_width = std::min(menu_width, grid_right - pos.x);
        }

        float grid_bottom = pos.y + it->second.height * effective_line_height;
        if (menu_y + menu_height > grid_bottom) {
            menu_y = pos.y + m_popup_anchor_row * effective_line_height -
                     menu_height;
            if (menu_y < pos.y) {
                menu_y = pos.y;
                menu_height = std::min(menu_height, grid_bottom - pos.y);
            }
        }
    }

    ImGui::SetNextWindowPos(ImVec2(menu_x, menu_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(menu_width, menu_height), ImGuiCond_Always);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;

    // Use a unique ID so multiple popups don't conflict
    if (ImGui::Begin("##popup_menu", nullptr, flags)) {
        ImDrawList* popup_draw = ImGui::GetWindowDrawList();
        ImVec2 popup_pos = ImGui::GetCursorScreenPos();

        for (size_t i = 0; i < m_popup_items.size(); i++) {
            ImVec2 item_pos(popup_pos.x,
                            popup_pos.y + i * effective_line_height);

            // Highlight selected item
            if (static_cast<int32_t>(i) == m_popup_selected) {
                popup_draw->AddRectFilled(
                    item_pos,
                    ImVec2(item_pos.x + menu_width,
                           item_pos.y + effective_line_height),
                    ImGui::ColorConvertFloat4ToU32(
                        ImVec4(0.3f, 0.5f, 0.8f, 0.6f)));
            }

            ImU32 text_color =
                ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            popup_draw->AddText(ImVec2(item_pos.x + 4.0f, item_pos.y),
                                text_color, display_strings[i].c_str());
        }
    }
    ImGui::End();
}

void NvimWidget::resize(uint32_t cols, uint32_t rows) {
    // Ensure minimum size
    cols = std::max(1u, cols);
    rows = std::max(1u, rows);

    // Only resize if dimensions actually changed
    if (cols == m_state.col && rows == m_state.row) {
        return;
    }

    // Update nvim state
    m_state.col = cols;
    m_state.row = rows;

    // Resize grid
    auto it = m_grids.find(m_current_grid);
    if (it != m_grids.end()) {
        it->second.resize(cols, rows);
    }

    // Ensure cursor stays within bounds
    m_state.cursor_x = std::min(m_state.cursor_x, cols - 1);
    m_state.cursor_y = std::min(m_state.cursor_y, rows - 1);

    // Notify nvim if attached
    if (m_nvim_attached) {
        _notify_nvim_resize(cols, rows);
    }

    LOG_TRACE("Nvim widget resized to {}x{}", cols, rows);
}

std::shared_ptr<NvimRequest> NvimWidget::start_nvim_request(
    const std::string& method, uint8_t param_count,
    std::function<void(msgpack::object&)>&& on_result,
    std::function<void(int32_t, const std::string&)>&& on_error) {
    // [type(0), msgid, method, args]
    uint32_t cur_msgid = m_nvim_msgid.fetch_add(1);
    auto request = std::make_shared<NvimRequest>(
        cur_msgid, method, param_count, shared_from_this(),
        std::move(on_result), std::move(on_error));
    m_requests.emplace(cur_msgid, request);
    return request;
}

void NvimWidget::_spawn_nvim() {
    auto exe_path = ImApp::FileSystem::executable_path();
    auto exe_dir = exe_path.parent_path();
#if defined(IM_APP_DARWIN)
    // Bundled .app: Neovim is in Contents/Resources/nvim/
    // Development (non-bundled): nvim/ sits next to the executable
    auto nvim_exe_path =
        exe_dir.parent_path() / "Resources" / "nvim" / "bin" / "nvim";
    if (!std::filesystem::exists(nvim_exe_path)) {
        nvim_exe_path = exe_dir / "nvim" / "bin" / "nvim";
    }
#else
    auto nvim_exe_path = exe_dir / "nvim" / "bin" /
#if defined(IM_APP_WIN32)
                         "nvim.exe";
#else
                         "nvim";
#endif
#endif
    m_nvim_exe = ImApp::path_to_string(nvim_exe_path);

    char* args[3];
    args[0] = const_cast<char*>(m_nvim_exe.c_str());
    args[1] = const_cast<char*>("--embed");
    args[2] = nullptr;

    uv_pipe_init(globals::g_uv_loop, &m_in_pipe, 0);
    m_in_pipe.data = this;
    uv_pipe_init(globals::g_uv_loop, &m_out_pipe, 0);
    m_out_pipe.data = this;

    uv_stdio_container_t nvim_stdio[3];
    /* stdin for nvim */
    nvim_stdio[0].flags =
        static_cast<uv_stdio_flags>(UV_CREATE_PIPE | UV_READABLE_PIPE);
    nvim_stdio[0].data.stream = reinterpret_cast<uv_stream_t*>(&m_in_pipe);
    /* stdout for nvim */
    nvim_stdio[1].flags =
        static_cast<uv_stdio_flags>(UV_CREATE_PIPE | UV_WRITABLE_PIPE);
    nvim_stdio[1].data.stream = reinterpret_cast<uv_stream_t*>(&m_out_pipe);
    /* stderr for nvim */
    nvim_stdio[2].flags = UV_IGNORE;
    nvim_stdio[2].data.stream = nullptr;

    uv_process_options_t options = {nullptr};
    options.file = m_nvim_exe.c_str();
    options.args = args;
    options.cwd = m_nvim_cwd.c_str();
    options.flags = UV_PROCESS_WINDOWS_HIDE; // no console for --embed nvim
    options.env = nullptr;
    options.stdio_count = 3;
    options.stdio = nvim_stdio;
    options.exit_cb = _on_nvim_exit;

    int r;
    if ((r = uv_spawn(globals::g_uv_loop, &m_nvim_proc, &options))) {
        LOG_ERROR("Failed to spawn nvim: {}", uv_strerror(r));
        m_nvim_proc.data = nullptr;
        m_nvim_proc.pid = 0;
        uv_close(reinterpret_cast<uv_handle_t*>(&m_in_pipe), nullptr);
        m_in_pipe.data = nullptr;
        uv_close(reinterpret_cast<uv_handle_t*>(&m_out_pipe), nullptr);
        m_out_pipe.data = nullptr;
        return;
    }
    LOG_DEBUG("Launched nvim --embed with pid {}", m_nvim_proc.pid);
    m_nvim_proc.data = this;
    uv_read_start(reinterpret_cast<uv_stream_t*>(&m_out_pipe), _uv_alloc_cb,
                  _uv_read_cb);
    start_nvim_request(
        "nvim_get_api_info", 0,
        [this](msgpack::object& result) {
            if (result.type != msgpack::type::ARRAY ||
                result.via.array.size != 2 ||
                result.via.array.ptr[0].type !=
                    msgpack::type::POSITIVE_INTEGER ||
                result.via.array.ptr[1].type != msgpack::type::MAP) {
                return;
            }
            m_nvim_channel = result.via.array.ptr[0].as<uint64_t>();
            msgpack::object& meta_data = result.via.array.ptr[1];
            for (size_t i = 0; i < meta_data.via.map.size; i++) {
                auto& kv_pair = meta_data.via.map.ptr[i];
                auto& key = kv_pair.key;
                if (key.type != msgpack::type::STR) {
                    continue;
                }
                auto k = key.as<std::string>();
                auto& value = kv_pair.val;
                if (strcmp(k.c_str(), "version") == 0) {
                    if (value.type != msgpack::type::MAP) {
                        continue;
                    }
                    // Temporary values to build version from components
                    uint64_t version_major = 0;
                    uint64_t version_minor = 0;
                    uint64_t version_patch = 0;
                    bool version_prerelease = false;

                    for (size_t j = 0; j < value.via.map.size; j++) {
                        auto& version_kv_pair = value.via.map.ptr[j];
                        auto& version_key = version_kv_pair.key;
                        if (version_key.type != msgpack::type::STR) {
                            continue;
                        }
                        auto version_k = version_key.as<std::string>();
                        auto& version_value = version_kv_pair.val;
                        if (strcmp(version_k.c_str(), "api_compatible") == 0 &&
                            version_value.type ==
                                msgpack::type::POSITIVE_INTEGER) {
                            m_nvim_api_compatible =
                                version_value.as<uint64_t>();
                        } else if (strcmp(version_k.c_str(), "api_level") ==
                                       0 &&
                                   version_value.type ==
                                       msgpack::type::POSITIVE_INTEGER) {
                            m_nvim_api_level = version_value.as<uint64_t>();
                        } else if (strcmp(version_k.c_str(), "major") == 0 &&
                                   version_value.type ==
                                       msgpack::type::POSITIVE_INTEGER) {
                            version_major = version_value.as<uint64_t>();
                        } else if (strcmp(version_k.c_str(), "minor") == 0 &&
                                   version_value.type ==
                                       msgpack::type::POSITIVE_INTEGER) {
                            version_minor = version_value.as<uint64_t>();
                        } else if (strcmp(version_k.c_str(), "patch") == 0 &&
                                   version_value.type ==
                                       msgpack::type::POSITIVE_INTEGER) {
                            version_patch = version_value.as<uint64_t>();
                        } else if (strcmp(version_k.c_str(), "prerelease") ==
                                       0 &&
                                   version_value.type ==
                                       msgpack::type::BOOLEAN) {
                            version_prerelease = version_value.as<bool>();
                        }
                    }

                    // Build the human-readable version string
                    m_nvim_version_string =
                        fmt::format("NVIM v{}.{}.{}", version_major,
                                    version_minor, version_patch);
                    if (version_prerelease) {
                        m_nvim_version_string += "-dev";
                    }
                } else if (strcmp(k.c_str(), "ui_options") == 0) {
                    if (value.type != msgpack::type::ARRAY) {
                        continue;
                    }
                    for (size_t j = 0; j < value.via.array.size; j++) {
                        auto& ui_option = value.via.array.ptr[j];
                        if (ui_option.type != msgpack::type::STR) {
                            continue;
                        }
                        m_nvim_ui_options.emplace_back(
                            ui_option.as<std::string>());
                    }
                }
            }
            LOG_DEBUG(
                "Got nvim meta data, channle: {}, api compatible: {}, api "
                "level: {}",
                m_nvim_channel, m_nvim_api_compatible, m_nvim_api_level);
            _initialize();
        },
        nullptr);
}

/*
 * 'nvim_get_api_info' has responded, ready to attach.
 * https://neovim.io/doc/user/api-ui-events.html#ui-startup
 */
void NvimWidget::_initialize() {
    auto req = start_nvim_request(
        "nvim_ui_attach", 3,
        [this](msgpack::object& result) {
            if (result.type != msgpack::type::NIL) {
                return;
            }
            _set_nvim_attached(true);
        },
        nullptr);
    req->arg_uint32(m_state.col);
    req->arg_uint32(m_state.row);
    req->arg_map(6);
    {
        std::string rgb_key{"rgb"};
        req->arg_str(rgb_key.size());
        req->arg_str_body(rgb_key.c_str(), rgb_key.size());
        req->arg_true();

        // Second revision of the grid protocol: provides grid_line,
        // hl_attr_define, grid_resize, grid_scroll, grid_destroy, etc.
        // (Neovim force-enables this when ext_multigrid is set, but we
        //  list it explicitly here to document our dependency.)
        std::string linegrid_key{"ext_linegrid"};
        req->arg_str(linegrid_key.size());
        req->arg_str_body(linegrid_key.c_str(), linegrid_key.size());
        req->arg_true();

        std::string multigrid_key{"ext_multigrid"};
        req->arg_str(multigrid_key.size());
        req->arg_str_body(multigrid_key.c_str(), multigrid_key.size());
        req->arg_true();

        std::string cmdline_key{"ext_cmdline"};
        req->arg_str(cmdline_key.size());
        req->arg_str_body(cmdline_key.c_str(), cmdline_key.size());
        req->arg_true();

        std::string popupmenu_key{"ext_popupmenu"};
        req->arg_str(popupmenu_key.size());
        req->arg_str_body(popupmenu_key.c_str(), popupmenu_key.size());
        req->arg_true();

        std::string messages_key{"ext_messages"};
        req->arg_str(messages_key.size());
        req->arg_str_body(messages_key.c_str(), messages_key.size());
        req->arg_true();
    }
    m_multigrid_enabled = true;
}

void NvimWidget::_set_nvim_attached(bool attached) {
    m_nvim_attached = attached;
    if (attached) {
        _notify_nvim_resize(m_state.col, m_state.row);
    }
}

void NvimWidget::_handle_nvim_request(uint32_t msgid, std::string_view method,
                                      msgpack::object_array& args) {}

void NvimWidget::_handle_nvim_notification(std::string_view event,
                                           msgpack::object_array& args) {
    switch (_hash(event)) {
    case _hash("redraw"): {
        LOG_TRACE("Nvim redraw event.");
        for (size_t i = 0; i < args.size; i++) {
            auto& arg = args.ptr[i];
            if (arg.type != msgpack::type::ARRAY) {
                LOG_WARN("Received unexpected redraw operation, argument is "
                         "not an array.");
                continue;
            }
            if (arg.via.array.size < 2) {
                LOG_WARN("Received unexpected redraw operation, size of the "
                         "argument array is less than 2.");
                continue;
            }
            if (arg.via.array.ptr[0].type != msgpack::type::STR) {
                LOG_WARN("Received unexpected redraw operation, the first item "
                         "of the argument array is not a string.");
                continue;
            }
            if (arg.via.array.ptr[1].type != msgpack::type::ARRAY) {
                LOG_WARN("Received unexpected redraw operation, the second "
                         "item of the argument array is not an array.");
                continue;
            }
            std::string operation = arg.via.array.ptr[0].as<std::string>();

            for (size_t j = 1; j < arg.via.array.size; j++) {
                auto& op_args = arg.via.array.ptr[j];
                if (op_args.type != msgpack::type::ARRAY) {
                    LOG_WARN("Received unexpected redraw operation '{}', "
                             "operation argument is not an array.",
                             operation);
                    continue;
                }
                _handle_nvim_redraw(operation, op_args.via.array);
            }
        }
        break;
    }
    case _hash("Gui"): {
        if (args.size > 0) {
            std::string gui_event = args.ptr[0].as<std::string>();
            _handle_nvim_gui_event(gui_event, args);
        }
        break;
    }
    default:
        LOG_TRACE("Unhandled notification: {}", event);
        break;
    }
}

void NvimWidget::_handle_nvim_redraw(std::string_view operation,
                                     msgpack::object_array& args) {
    switch (_hash(operation)) {
    case _hash("resize"):
        _redraw_resize(args);
        break;
    case _hash("clear"):
        _redraw_clear(args);
        break;
    case _hash("cursor_goto"):
        _redraw_cursor_goto(args);
        break;
    case _hash("put"):
        _redraw_put(args);
        break;
    case _hash("scroll"):
        _redraw_scroll(args);
        break;
    case _hash("set_scroll_region"):
        _redraw_set_scroll_region(args);
        break;
    case _hash("highlight_set"):
        _redraw_highlight_set(args);
        break;
    case _hash("eol_clear"):
        _redraw_eol_clear(args);
        break;
    case _hash("flush"):
        _redraw_flush(args);
        break;
    case _hash("option_set"):
        _redraw_option_set(args);
        break;
    case _hash("set_title"):
        _redraw_set_title(args);
        break;
    case _hash("set_icon"):
        _redraw_set_icon(args);
        break;
    case _hash("default_colors_set"):
        _redraw_default_colors_set(args);
        break;
    case _hash("mode_info_set"):
        _redraw_mode_info_set(args);
        break;
    case _hash("mode_change"):
        _redraw_mode_change(args);
        break;
    case _hash("busy_start"):
        _redraw_busy_start(args);
        break;
    case _hash("busy_stop"):
        _redraw_busy_stop(args);
        break;
    case _hash("mouse_on"):
        _redraw_mouse_on(args);
        break;
    case _hash("mouse_off"):
        _redraw_mouse_off(args);
        break;
    case _hash("bell"):
        _redraw_bell(args);
        break;
    case _hash("suspend"):
        _redraw_suspend(args);
        break;
    case _hash("chdir"):
        _redraw_chdir(args);
        break;
    case _hash("popupmenu_show"):
        _redraw_popupmenu_show(args);
        break;
    case _hash("popupmenu_select"):
        _redraw_popupmenu_select(args);
        break;
    case _hash("popupmenu_hide"):
        _redraw_popupmenu_hide(args);
        break;
    case _hash("grid_resize"):
        _redraw_grid_resize(args);
        break;
    case _hash("grid_line"):
        _redraw_grid_line(args);
        break;
    case _hash("grid_clear"):
        _redraw_grid_clear(args);
        break;
    case _hash("grid_cursor_goto"):
        _redraw_grid_cursor_goto(args);
        break;
    case _hash("grid_scroll"):
        _redraw_grid_scroll(args);
        break;
    case _hash("grid_destroy"):
        _redraw_grid_destroy(args);
        break;
    case _hash("hl_attr_define"):
        _redraw_hl_attr_define(args);
        break;
    case _hash("hl_group_set"):
        _redraw_hl_group_set(args);
        break;
    case _hash("cmdline_show"):
        _cmdline_show(args);
        break;
    case _hash("cmdline_hide"):
        _cmdline_hide(args);
        break;
    case _hash("cmdline_pos"):
        _cmdline_pos(args);
        break;
    case _hash("cmdline_special_char"):
        _cmdline_special_char(args);
        break;
    case _hash("cmdline_block_show"):
        _cmdline_block_show(args);
        break;
    case _hash("cmdline_block_append"):
        _cmdline_block_append(args);
        break;
    case _hash("cmdline_block_hide"):
        _cmdline_block_hide(args);
        break;
    case _hash("msg_show"):
        _redraw_msg_show(args);
        break;
    case _hash("msg_clear"):
        _redraw_msg_clear(args);
        break;
    case _hash("msg_showmode"):
        _redraw_msg_showmode(args);
        break;
    case _hash("msg_showcmd"):
        _redraw_msg_showcmd(args);
        break;
    case _hash("msg_ruler"):
        _redraw_msg_ruler(args);
        break;
    case _hash("msg_history_show"):
        _redraw_msg_history_show(args);
        break;
    case _hash("win_pos"):
        _redraw_win_pos(args);
        break;
    case _hash("win_float_pos"):
        _redraw_win_float_pos(args);
        break;
    case _hash("win_hide"):
        _redraw_win_hide(args);
        break;
    case _hash("win_close"):
        _redraw_win_close(args);
        break;
    case _hash("win_viewport"):
        _redraw_win_viewport(args);
        break;
    case _hash("win_viewport_margins"):
        _redraw_win_viewport_margins(args);
        break;
    case _hash("win_extmark"):
        _redraw_win_extmark(args);
        break;
    case _hash("update_menu"):
        _redraw_update_menu(args);
        break;
    case _hash("msg_set_pos"):
        _redraw_msg_set_pos(args);
        break;
    default:
        LOG_TRACE("Unhandled redraw operation: {}", operation);
        break;
    }
}

void NvimWidget::_redraw_resize(msgpack::object_array& args) {
    uint32_t grid_id = m_current_grid;
    uint32_t width = 0;
    uint32_t height = 0;

    if (args.size == 2) {
        // Format: [width, height] (no grid_id, use current)
        if (args.ptr[0].type != msgpack::type::POSITIVE_INTEGER ||
            args.ptr[1].type != msgpack::type::POSITIVE_INTEGER) {
            LOG_WARN("resize: invalid argument types");
            return;
        }
        width = args.ptr[0].as<uint32_t>();
        height = args.ptr[1].as<uint32_t>();
    } else if (args.size >= 3) {
        // Format: [grid_id, width, height]
        if (args.ptr[0].type != msgpack::type::POSITIVE_INTEGER ||
            args.ptr[1].type != msgpack::type::POSITIVE_INTEGER ||
            args.ptr[2].type != msgpack::type::POSITIVE_INTEGER) {
            LOG_WARN("resize: invalid argument types");
            return;
        }
        grid_id = args.ptr[0].as<uint32_t>();
        width = args.ptr[1].as<uint32_t>();
        height = args.ptr[2].as<uint32_t>();
    } else {
        LOG_WARN("resize: expected 2 or 3 arguments, got {}", args.size);
        return;
    }

    auto it = m_grids.find(grid_id);
    if (it == m_grids.end()) {
        Grid new_grid;
        new_grid.id = grid_id;
        new_grid.resize(width, height);
        m_grids[grid_id] = std::move(new_grid);
    } else {
        it->second.resize(width, height);
    }

    if (grid_id == m_current_grid) {
        m_state.col = width;
        m_state.row = height;
    }
}

void NvimWidget::_redraw_clear(msgpack::object_array& args) {
    uint32_t grid_id = m_current_grid;

    if (args.size >= 1 && args.ptr[0].type == msgpack::type::POSITIVE_INTEGER) {
        grid_id = args.ptr[0].as<uint32_t>();
    }

    auto it = m_grids.find(grid_id);
    if (it != m_grids.end()) {
        it->second.clear();
    }
}

void NvimWidget::_redraw_cursor_goto(msgpack::object_array& args) {
    uint32_t grid_id = m_current_grid;
    uint32_t row = 0;
    uint32_t col = 0;

    if (args.size == 2) {
        // Format: [row, col] (no grid_id, use current)
        if (args.ptr[0].type != msgpack::type::POSITIVE_INTEGER ||
            args.ptr[1].type != msgpack::type::POSITIVE_INTEGER) {
            LOG_WARN("cursor_goto: invalid argument types");
            return;
        }
        row = args.ptr[0].as<uint32_t>();
        col = args.ptr[1].as<uint32_t>();
    } else if (args.size >= 3) {
        // Format: [grid_id, row, col]
        if (args.ptr[0].type != msgpack::type::POSITIVE_INTEGER ||
            args.ptr[1].type != msgpack::type::POSITIVE_INTEGER ||
            args.ptr[2].type != msgpack::type::POSITIVE_INTEGER) {
            LOG_WARN("cursor_goto: invalid argument types");
            return;
        }
        grid_id = args.ptr[0].as<uint32_t>();
        row = args.ptr[1].as<uint32_t>();
        col = args.ptr[2].as<uint32_t>();
    } else {
        LOG_WARN("cursor_goto: expected 2 or 3 arguments, got {}", args.size);
        return;
    }

    m_current_grid = grid_id;
    m_state.cursor_y = row;
    m_state.cursor_x = col;
}

void NvimWidget::_redraw_put(msgpack::object_array& args) {
    uint32_t grid_id = m_current_grid;
    std::string text;

    if (args.size == 1) {
        // Format: [text] (no grid_id, use current)
        if (args.ptr[0].type != msgpack::type::STR) {
            LOG_WARN("put: invalid argument types");
            return;
        }
        text = args.ptr[0].as<std::string>();
    } else if (args.size >= 2) {
        // Format: [grid_id, text]
        if (args.ptr[0].type != msgpack::type::POSITIVE_INTEGER ||
            args.ptr[1].type != msgpack::type::STR) {
            LOG_WARN("put: invalid argument types");
            return;
        }
        grid_id = args.ptr[0].as<uint32_t>();
        text = args.ptr[1].as<std::string>();
    } else {
        LOG_WARN("put: expected 1 or 2 arguments, got {}", args.size);
        return;
    }

    auto it = m_grids.find(grid_id);
    if (it == m_grids.end()) {
        return;
    }

    Grid& grid = it->second;
    if (m_state.cursor_y >= grid.height || m_state.cursor_x >= grid.width) {
        return;
    }

    ScreenCell& cell = grid.cells[m_state.cursor_y][m_state.cursor_x];

    // Decode UTF-8 text into the cell
    cell.clear();
    const char* ptr = text.c_str();
    size_t remaining = text.length();
    size_t offset = 0;
    int char_count = 0;

    while (remaining > 0 && char_count < 4) {
        uint32_t rune;
        size_t decoded =
            TextWidget::utf8_decode(ptr + offset, &rune, remaining);
        if (decoded == 0) {
            break;
        }
        cell.chars[char_count++] = rune;
        offset += decoded;
        remaining -= decoded;
    }

    cell.width = std::max(1, char_count);
    cell.fg = m_current_hl.fg;
    cell.bg = m_current_hl.bg;
    cell.bold = m_current_hl.bold;
    cell.italic = m_current_hl.italic;
    cell.underline = m_current_hl.underline;
    cell.undercurl = m_current_hl.undercurl;
    cell.reverse = m_current_hl.reverse;

    // Advance cursor
    m_state.cursor_x++;
}

void NvimWidget::_redraw_highlight_set(msgpack::object_array& args) {
    uint32_t grid_id = m_current_grid;
    msgpack::object_map* attr_map_ptr = nullptr;

    if (args.size == 1) {
        // Format: [attr_map] (no grid_id, use current)
        if (args.ptr[0].type != msgpack::type::MAP) {
            LOG_WARN("highlight_set: invalid argument types");
            return;
        }
        attr_map_ptr = &args.ptr[0].via.map;
    } else if (args.size >= 2) {
        // Format: [grid_id, attr_map]
        if (args.ptr[0].type != msgpack::type::POSITIVE_INTEGER ||
            args.ptr[1].type != msgpack::type::MAP) {
            LOG_WARN("highlight_set: invalid argument types");
            return;
        }
        grid_id = args.ptr[0].as<uint32_t>();
        attr_map_ptr = &args.ptr[1].via.map;
    } else {
        LOG_WARN("highlight_set: expected 1 or 2 arguments, got {}", args.size);
        return;
    }

    msgpack::object_map& attr_map = *attr_map_ptr;

    // Start with current highlight
    HighlightAttr new_hl = m_current_hl;

    for (size_t i = 0; i < attr_map.size; i++) {
        auto& kv = attr_map.ptr[i];
        if (kv.key.type != msgpack::type::STR) {
            continue;
        }
        std::string key = kv.key.as<std::string>();

        if (key == "foreground" &&
            kv.val.type == msgpack::type::POSITIVE_INTEGER) {
            uint32_t rgb = kv.val.as<uint32_t>();
            new_hl.fg.x = static_cast<float>((rgb >> 16) & 0xFF) / 255.0f;
            new_hl.fg.y = static_cast<float>((rgb >> 8) & 0xFF) / 255.0f;
            new_hl.fg.z = static_cast<float>(rgb & 0xFF) / 255.0f;
            new_hl.fg.w = 1.0f;
        } else if (key == "background" &&
                   kv.val.type == msgpack::type::POSITIVE_INTEGER) {
            uint32_t rgb = kv.val.as<uint32_t>();
            new_hl.bg.x = static_cast<float>((rgb >> 16) & 0xFF) / 255.0f;
            new_hl.bg.y = static_cast<float>((rgb >> 8) & 0xFF) / 255.0f;
            new_hl.bg.z = static_cast<float>(rgb & 0xFF) / 255.0f;
            new_hl.bg.w = 1.0f;
        } else if (key == "special" &&
                   kv.val.type == msgpack::type::POSITIVE_INTEGER) {
            uint32_t rgb = kv.val.as<uint32_t>();
            new_hl.sp.x = static_cast<float>((rgb >> 16) & 0xFF) / 255.0f;
            new_hl.sp.y = static_cast<float>((rgb >> 8) & 0xFF) / 255.0f;
            new_hl.sp.z = static_cast<float>(rgb & 0xFF) / 255.0f;
            new_hl.sp.w = 1.0f;
        } else if (key == "bold" && kv.val.type == msgpack::type::BOOLEAN) {
            new_hl.bold = kv.val.as<bool>();
        } else if (key == "italic" && kv.val.type == msgpack::type::BOOLEAN) {
            new_hl.italic = kv.val.as<bool>();
        } else if (key == "underline" &&
                   kv.val.type == msgpack::type::BOOLEAN) {
            new_hl.underline = kv.val.as<bool>();
        } else if (key == "undercurl" &&
                   kv.val.type == msgpack::type::BOOLEAN) {
            new_hl.undercurl = kv.val.as<bool>();
        } else if (key == "reverse" && kv.val.type == msgpack::type::BOOLEAN) {
            new_hl.reverse = kv.val.as<bool>();
        } else if (key == "id" &&
                   kv.val.type == msgpack::type::POSITIVE_INTEGER) {
            int hl_id = kv.val.as<int>();
            m_hl_attrs[hl_id] = new_hl;
        }
    }

    m_current_hl = new_hl;
}

void NvimWidget::_redraw_flush(msgpack::object_array& /*args*/) {
    m_needs_render = true;
    m_needs_modified_check = true;
}

void NvimWidget::_redraw_option_set(msgpack::object_array& args) {
    size_t option_name_idx = 0;

    // Skip grid_id if present
    if (args.size >= 2 && args.ptr[0].type == msgpack::type::POSITIVE_INTEGER) {
        option_name_idx = 1;
    }

    if (args.size < option_name_idx + 2) {
        LOG_WARN("option_set: expected at least {} arguments, got {}",
                 option_name_idx + 2, args.size);
        return;
    }
    if (args.ptr[option_name_idx].type != msgpack::type::STR) {
        LOG_WARN("option_set: option name must be a string");
        return;
    }

    std::string option = args.ptr[option_name_idx].as<std::string>();
    auto& val = args.ptr[option_name_idx + 1];

    if (option == "guifont") {
        if (val.type == msgpack::type::STR) {
            m_requested_font = val.as<std::string>();
            LOG_DEBUG("guifont requested: {}", m_requested_font);

            // Parse and compare against current font to decide if reload needed
            ParsedFont parsed = _parse_guifont(m_requested_font);
            if (parsed.family.empty()) {
                parsed.family = ImApp::FontManager::get_default_font_family();
                parsed.size_pt = 14.0f;
            }
            if (parsed != m_current_font) {
                m_font_reload_pending = true;
            }
        }
    } else if (option == "guifontwide") {
        if (val.type == msgpack::type::STR) {
            m_requested_font_wide = val.as<std::string>();
            LOG_DEBUG("guifontwide requested: {}", m_requested_font_wide);

            // Parse and compare against current wide font
            ParsedFont parsed = _parse_guifont(m_requested_font_wide);
            if (parsed != m_current_font_wide) {
                m_font_reload_pending = true;
            }
        }
    } else if (option == "linespace") {
        if (val.type == msgpack::type::POSITIVE_INTEGER) {
            m_linespace = static_cast<int32_t>(val.as<uint32_t>());
        } else if (val.type == msgpack::type::NEGATIVE_INTEGER) {
            m_linespace = val.as<int32_t>();
        }
    }
}

void NvimWidget::_redraw_set_title(msgpack::object_array& args) {
    size_t title_idx = 0;

    // Skip grid_id if present
    if (args.size >= 2 && args.ptr[0].type == msgpack::type::POSITIVE_INTEGER) {
        title_idx = 1;
    }

    if (args.size < title_idx + 1) {
        LOG_WARN("set_title: expected at least {} arguments, got {}",
                 title_idx + 1, args.size);
        return;
    }
    if (args.ptr[title_idx].type != msgpack::type::STR) {
        LOG_WARN("set_title: title must be a string");
        return;
    }

    std::string title = args.ptr[title_idx].as<std::string>();

    // Use default title if empty
    if (title.empty()) {
        m_window_title = "nvim (no file)";
    } else {
        m_window_title = title;
    }
}

void NvimWidget::_redraw_set_icon(msgpack::object_array& args) {
    size_t icon_idx = 0;

    // Skip grid_id if present
    if (args.size >= 2 && args.ptr[0].type == msgpack::type::POSITIVE_INTEGER) {
        icon_idx = 1;
    }

    if (args.size < icon_idx + 1) {
        LOG_WARN("set_icon: expected at least {} arguments, got {}",
                 icon_idx + 1, args.size);
        return;
    }
    if (args.ptr[icon_idx].type != msgpack::type::STR) {
        LOG_WARN("set_icon: icon must be a string");
        return;
    }

    m_window_icon = args.ptr[icon_idx].as<std::string>();
}

void NvimWidget::_redraw_default_colors_set(msgpack::object_array& args) {
    size_t color_idx = 0;

    // Skip grid_id if present
    if (args.size >= 1 && args.ptr[0].type == msgpack::type::POSITIVE_INTEGER) {
        // Check if the first argument is a grid_id (small integer) or rgb color
        // Grid ids are usually 1, 2, etc. while rgb colors are 24-bit values
        uint32_t first_val = args.ptr[0].as<uint32_t>();
        if (first_val < 256) {
            // Likely a grid_id, skip it
            color_idx = 1;
        }
    }

    if (args.size < color_idx + 3) {
        LOG_WARN("default_colors_set: expected at least {} arguments, got {}",
                 color_idx + 3, args.size);
        return;
    }

    if (args.ptr[color_idx].type == msgpack::type::POSITIVE_INTEGER) {
        uint32_t rgb = args.ptr[color_idx].as<uint32_t>();
        m_default_fg.x = static_cast<float>((rgb >> 16) & 0xFF) / 255.0f;
        m_default_fg.y = static_cast<float>((rgb >> 8) & 0xFF) / 255.0f;
        m_default_fg.z = static_cast<float>(rgb & 0xFF) / 255.0f;
        m_default_fg.w = 1.0f;
        m_current_hl.fg = m_default_fg;
    }
    if (args.ptr[color_idx + 1].type == msgpack::type::POSITIVE_INTEGER) {
        uint32_t rgb = args.ptr[color_idx + 1].as<uint32_t>();
        m_default_bg.x = static_cast<float>((rgb >> 16) & 0xFF) / 255.0f;
        m_default_bg.y = static_cast<float>((rgb >> 8) & 0xFF) / 255.0f;
        m_default_bg.z = static_cast<float>(rgb & 0xFF) / 255.0f;
        m_default_bg.w = 1.0f;
        m_current_hl.bg = m_default_bg;
    }
    if (args.ptr[color_idx + 2].type == msgpack::type::POSITIVE_INTEGER) {
        uint32_t rgb = args.ptr[color_idx + 2].as<uint32_t>();
        m_default_sp.x = static_cast<float>((rgb >> 16) & 0xFF) / 255.0f;
        m_default_sp.y = static_cast<float>((rgb >> 8) & 0xFF) / 255.0f;
        m_default_sp.z = static_cast<float>(rgb & 0xFF) / 255.0f;
        m_default_sp.w = 1.0f;
        m_current_hl.sp = m_default_sp;
    }
}

void NvimWidget::_redraw_set_scroll_region(msgpack::object_array& args) {
    if (args.size < 4) {
        LOG_WARN("set_scroll_region: expected 4 arguments, got {}", args.size);
        return;
    }
    if (args.ptr[0].type != msgpack::type::POSITIVE_INTEGER ||
        args.ptr[1].type != msgpack::type::POSITIVE_INTEGER ||
        args.ptr[2].type != msgpack::type::POSITIVE_INTEGER ||
        args.ptr[3].type != msgpack::type::POSITIVE_INTEGER) {
        LOG_WARN("set_scroll_region: arguments must be positive integers");
        return;
    }

    auto it = m_grids.find(m_current_grid);
    if (it == m_grids.end()) {
        return;
    }

    it->second.m_scroll_region.top = args.ptr[0].as<uint32_t>();
    it->second.m_scroll_region.bot = args.ptr[1].as<uint32_t>();
    it->second.m_scroll_region.left = args.ptr[2].as<uint32_t>();
    it->second.m_scroll_region.right = args.ptr[3].as<uint32_t>();
}

void NvimWidget::_redraw_scroll(msgpack::object_array& args) {
    if (args.size < 1) {
        LOG_WARN("scroll: expected at least 1 argument, got {}", args.size);
        return;
    }

    int64_t count = 0;
    if (args.ptr[0].type == msgpack::type::POSITIVE_INTEGER) {
        count = static_cast<int64_t>(args.ptr[0].as<uint64_t>());
    } else if (args.ptr[0].type == msgpack::type::NEGATIVE_INTEGER) {
        count = args.ptr[0].as<int64_t>();
    } else {
        LOG_WARN("scroll: argument must be an integer");
        return;
    }

    auto it = m_grids.find(m_current_grid);
    if (it == m_grids.end()) {
        return;
    }

    it->second.scroll_region(static_cast<int>(count));
}

void NvimWidget::_redraw_eol_clear(msgpack::object_array& /*args*/) {
    auto it = m_grids.find(m_current_grid);
    if (it == m_grids.end()) {
        return;
    }

    Grid& grid = it->second;
    if (m_state.cursor_y >= grid.height) {
        return;
    }

    for (uint32_t x = m_state.cursor_x; x < grid.width; x++) {
        grid.cells[m_state.cursor_y][x].clear();
    }
}

void NvimWidget::_redraw_mode_info_set(msgpack::object_array& args) {
    if (args.size < 2) {
        LOG_WARN("mode_info_set: expected at least 2 arguments, got {}",
                 args.size);
        return;
    }
    if (args.ptr[0].type != msgpack::type::BOOLEAN) {
        LOG_WARN("mode_info_set: first argument must be a boolean");
        return;
    }

    m_cursor_style_enabled = args.ptr[0].as<bool>();

    if (args.ptr[1].type != msgpack::type::ARRAY) {
        LOG_WARN("mode_info_set: second argument must be an array");
        return;
    }

    msgpack::object_array& mode_list = args.ptr[1].via.array;
    m_mode_info.clear();
    m_mode_info.reserve(mode_list.size);

    for (size_t i = 0; i < mode_list.size; i++) {
        if (mode_list.ptr[i].type != msgpack::type::MAP) {
            continue;
        }

        ModeInfoEntry entry;
        msgpack::object_map& props = mode_list.ptr[i].via.map;

        for (size_t j = 0; j < props.size; j++) {
            if (props.ptr[j].key.type != msgpack::type::STR) {
                continue;
            }
            std::string key = props.ptr[j].key.as<std::string>();
            auto& val = props.ptr[j].val;

            if (key == "cursor_shape" && val.type == msgpack::type::STR) {
                entry.cursor_shape = val.as<std::string>();
            } else if (key == "cell_percentage" &&
                       val.type == msgpack::type::POSITIVE_INTEGER) {
                entry.cell_percentage = val.as<uint32_t>();
            } else if (key == "blinkwait" &&
                       val.type == msgpack::type::POSITIVE_INTEGER) {
                entry.blinkwait = val.as<uint32_t>();
            } else if (key == "blinkon" &&
                       val.type == msgpack::type::POSITIVE_INTEGER) {
                entry.blinkon = val.as<uint32_t>();
            } else if (key == "blinkoff" &&
                       val.type == msgpack::type::POSITIVE_INTEGER) {
                entry.blinkoff = val.as<uint32_t>();
            } else if (key == "attr_id" &&
                       val.type == msgpack::type::POSITIVE_INTEGER) {
                entry.attr_id = val.as<int>();
            }
        }

        m_mode_info.push_back(entry);
    }
}

void NvimWidget::_redraw_mode_change(msgpack::object_array& args) {
    if (args.size < 2) {
        LOG_WARN("mode_change: expected at least 2 arguments, got {}",
                 args.size);
        return;
    }
    if (args.ptr[0].type != msgpack::type::STR) {
        LOG_WARN("mode_change: mode name must be a string");
        return;
    }
    if (args.ptr[1].type != msgpack::type::POSITIVE_INTEGER) {
        LOG_WARN("mode_change: mode index must be a positive integer");
        return;
    }

    m_current_mode_name = args.ptr[0].as<std::string>();
    uint64_t mode_index = args.ptr[1].as<uint64_t>();

    if (!m_cursor_style_enabled || m_mode_info.empty()) {
        // Hardcoded defaults when cursor style is disabled
        if (m_current_mode_name == "insert") {
            m_cursor_shape = CursorShape::Vertical;
            m_cursor_cell_percentage = 25;
        } else if (m_current_mode_name == "replace") {
            m_cursor_shape = CursorShape::Horizontal;
            m_cursor_cell_percentage = 20;
        } else {
            m_cursor_shape = CursorShape::Block;
            m_cursor_cell_percentage = 100;
        }
        m_cursor_blinkwait = 0;
        m_cursor_blinkon = 0;
        m_cursor_blinkoff = 0;
        m_cursor_visible = true;
    } else {
        if (mode_index >= m_mode_info.size()) {
            return;
        }

        const ModeInfoEntry& info =
            m_mode_info[static_cast<size_t>(mode_index)];

        if (info.cursor_shape == "block") {
            m_cursor_shape = CursorShape::Block;
        } else if (info.cursor_shape == "horizontal") {
            m_cursor_shape = CursorShape::Horizontal;
        } else if (info.cursor_shape == "vertical") {
            m_cursor_shape = CursorShape::Vertical;
        } else {
            m_cursor_shape = CursorShape::Block;
        }

        uint32_t percentage = info.cell_percentage;
        if (percentage == 0 || percentage > 100) {
            percentage = 100;
        }
        m_cursor_cell_percentage = percentage;

        m_cursor_blinkwait = info.blinkwait;
        m_cursor_blinkon = info.blinkon;
        m_cursor_blinkoff = info.blinkoff;

        m_cursor_visible = true;
    }

    m_last_blink_time = static_cast<double>(ImGui::GetTime());
}

void NvimWidget::_redraw_busy_start(msgpack::object_array& /*args*/) {
    m_busy = true;
}

void NvimWidget::_redraw_busy_stop(msgpack::object_array& /*args*/) {
    m_busy = false;
}

void NvimWidget::_redraw_mouse_on(msgpack::object_array& /*args*/) {
    m_mouse_enabled = true;
}

void NvimWidget::_redraw_mouse_off(msgpack::object_array& /*args*/) {
    m_mouse_enabled = false;
}

void NvimWidget::_redraw_bell(msgpack::object_array& /*args*/) {
    m_bell_pending = true;
    m_bell_timestamp = ImGui::GetTime();
}

void NvimWidget::_redraw_suspend(msgpack::object_array& /*args*/) {
    m_suspend_pending = true;
    LOG_DEBUG("suspend requested (window minimize deferred)");
}

void NvimWidget::_redraw_chdir(msgpack::object_array& args) {
    if (args.size < 1) {
        LOG_WARN("chdir: expected 1 argument, got {}", args.size);
        return;
    }
    if (args.ptr[0].type != msgpack::type::STR) {
        LOG_WARN("chdir: path must be a string");
        return;
    }
    m_nvim_cwd = args.ptr[0].as<std::string>();
    LOG_DEBUG("chdir: {}", m_nvim_cwd);
}

void NvimWidget::_redraw_popupmenu_show(msgpack::object_array& args) {
    if (args.size < 4) {
        LOG_WARN("popupmenu_show: expected at least 4 arguments, got {}",
                 args.size);
        return;
    }
    if (args.ptr[0].type != msgpack::type::ARRAY) {
        LOG_WARN("popupmenu_show: first argument must be an array");
        return;
    }

    // Parse items: array of [text, kind, extra, info]
    msgpack::object_array& items = args.ptr[0].via.array;
    m_popup_items.clear();
    m_popup_items.reserve(items.size);

    for (size_t i = 0; i < items.size; i++) {
        if (items.ptr[i].type != msgpack::type::ARRAY) {
            m_popup_items.push_back({});
            continue;
        }

        msgpack::object_array& item = items.ptr[i].via.array;
        PopupMenuEntry entry;

        if (item.size >= 1 && item.ptr[0].type == msgpack::type::STR) {
            entry.text = item.ptr[0].as<std::string>();
        }
        if (item.size >= 2 && item.ptr[1].type == msgpack::type::STR) {
            entry.kind = item.ptr[1].as<std::string>();
        }
        if (item.size >= 3 && item.ptr[2].type == msgpack::type::STR) {
            entry.extra = item.ptr[2].as<std::string>();
        }
        if (item.size >= 4 && item.ptr[3].type == msgpack::type::STR) {
            entry.info = item.ptr[3].as<std::string>();
        }

        m_popup_items.push_back(std::move(entry));
    }

    // Parse selected index
    if (args.ptr[1].type == msgpack::type::POSITIVE_INTEGER) {
        m_popup_selected = static_cast<int32_t>(args.ptr[1].as<uint32_t>());
    } else if (args.ptr[1].type == msgpack::type::NEGATIVE_INTEGER) {
        m_popup_selected = args.ptr[1].as<int32_t>();
    }

    // Parse anchor row
    if (args.ptr[2].type == msgpack::type::POSITIVE_INTEGER) {
        m_popup_anchor_row = static_cast<int32_t>(args.ptr[2].as<uint32_t>());
    }

    // Parse anchor col
    if (args.ptr[3].type == msgpack::type::POSITIVE_INTEGER) {
        m_popup_anchor_col = static_cast<int32_t>(args.ptr[3].as<uint32_t>());
    }

    m_popup_visible = true;
}

void NvimWidget::_redraw_popupmenu_select(msgpack::object_array& args) {
    if (args.size < 1) {
        LOG_WARN("popupmenu_select: expected at least 1 argument, got {}",
                 args.size);
        return;
    }

    if (args.ptr[0].type == msgpack::type::POSITIVE_INTEGER) {
        m_popup_selected = static_cast<int32_t>(args.ptr[0].as<uint32_t>());
    } else if (args.ptr[0].type == msgpack::type::NEGATIVE_INTEGER) {
        m_popup_selected = args.ptr[0].as<int32_t>();
    }
}

void NvimWidget::_redraw_popupmenu_hide(msgpack::object_array& /*args*/) {
    m_popup_visible = false;
    m_popup_items.clear();
    m_popup_selected = -1;
}

void NvimWidget::_redraw_grid_resize(msgpack::object_array& args) {
    if (args.size < 3) {
        LOG_WARN("grid_resize: expected 3 arguments, got {}", args.size);
        return;
    }
    if (args.ptr[0].type != msgpack::type::POSITIVE_INTEGER ||
        args.ptr[1].type != msgpack::type::POSITIVE_INTEGER ||
        args.ptr[2].type != msgpack::type::POSITIVE_INTEGER) {
        LOG_WARN("grid_resize: invalid argument types");
        return;
    }

    uint32_t grid_id = args.ptr[0].as<uint32_t>();
    uint32_t width = args.ptr[1].as<uint32_t>();
    uint32_t height = args.ptr[2].as<uint32_t>();

    auto it = m_grids.find(grid_id);
    if (it == m_grids.end()) {
        Grid new_grid;
        new_grid.id = grid_id;
        new_grid.resize(width, height);
        m_grids[grid_id] = std::move(new_grid);
    } else {
        it->second.resize(width, height);
    }

    if (grid_id == m_current_grid) {
        m_state.col = width;
        m_state.row = height;
    }
}

void NvimWidget::_redraw_grid_line(msgpack::object_array& args) {
    if (args.size < 4) {
        LOG_WARN("grid_line: expected at least 4 arguments, got {}", args.size);
        return;
    }
    if (args.ptr[0].type != msgpack::type::POSITIVE_INTEGER ||
        args.ptr[1].type != msgpack::type::POSITIVE_INTEGER ||
        args.ptr[2].type != msgpack::type::POSITIVE_INTEGER ||
        args.ptr[3].type != msgpack::type::ARRAY) {
        LOG_WARN("grid_line: invalid argument types");
        return;
    }

    uint32_t grid_id = args.ptr[0].as<uint32_t>();
    uint32_t row = args.ptr[1].as<uint32_t>();
    uint32_t col_start = args.ptr[2].as<uint32_t>();
    msgpack::object_array& cells = args.ptr[3].via.array;

    auto it = m_grids.find(grid_id);
    if (it == m_grids.end()) {
        return;
    }

    Grid& grid = it->second;
    if (row >= grid.height) {
        return;
    }

    uint32_t col = col_start;
    // Track last hl_id for stateful highlighting (0 = use last)
    int last_hl_id = 0;

    for (size_t i = 0; i < cells.size; i++) {
        if (cells.ptr[i].type != msgpack::type::ARRAY) {
            continue;
        }

        msgpack::object_array& cell = cells.ptr[i].via.array;
        if (cell.size < 1 || cell.ptr[0].type != msgpack::type::STR) {
            continue;
        }

        std::string text = cell.ptr[0].as<std::string>();

        // Optional hl_id. 0 means "use default colors" and must
        // update last_hl_id so subsequent cells don't inherit stale
        // highlights (e.g. after a Visual selection cell).
        int hl_id = last_hl_id;
        if (cell.size >= 2 &&
            cell.ptr[1].type == msgpack::type::POSITIVE_INTEGER) {
            hl_id = static_cast<int>(cell.ptr[1].as<uint32_t>());
            last_hl_id = hl_id;
        }

        // Optional repeat count
        uint32_t repeat = 1;
        if (cell.size >= 3 &&
            cell.ptr[2].type == msgpack::type::POSITIVE_INTEGER) {
            repeat = cell.ptr[2].as<uint32_t>();
        }

        // Look up highlight attributes
        HighlightAttr hl = m_current_hl;
        if (hl_id != 0) {
            auto hl_it = m_hl_attrs.find(hl_id);
            if (hl_it != m_hl_attrs.end()) {
                hl = hl_it->second;
            }
        }

        for (uint32_t r = 0; r < repeat && col < grid.width; r++) {
            if (col >= grid.width)
                break;

            ScreenCell& screen_cell = grid.cells[row][col];

            // Decode UTF-8 text
            screen_cell.clear();
            const char* ptr = text.c_str();
            size_t remaining = text.length();
            size_t offset = 0;
            int char_count = 0;

            while (remaining > 0 && char_count < 4) {
                uint32_t rune;
                size_t decoded =
                    TextWidget::utf8_decode(ptr + offset, &rune, remaining);
                if (decoded == 0)
                    break;
                screen_cell.chars[char_count++] = rune;
                offset += decoded;
                remaining -= decoded;
            }

            screen_cell.width = std::max(1, char_count);
            screen_cell.fg = hl.fg;
            screen_cell.bg = hl.bg;
            screen_cell.bold = hl.bold;
            screen_cell.italic = hl.italic;
            screen_cell.underline = hl.underline;
            screen_cell.undercurl = hl.undercurl;
            screen_cell.reverse = hl.reverse;

            col++;
        }
    }
}

void NvimWidget::_redraw_grid_clear(msgpack::object_array& args) {
    uint32_t grid_id = m_current_grid;
    if (args.size >= 1 && args.ptr[0].type == msgpack::type::POSITIVE_INTEGER) {
        grid_id = args.ptr[0].as<uint32_t>();
    }

    auto it = m_grids.find(grid_id);
    if (it != m_grids.end()) {
        it->second.clear();
    }
}

void NvimWidget::_redraw_grid_cursor_goto(msgpack::object_array& args) {
    if (args.size < 3) {
        LOG_WARN("grid_cursor_goto: expected 3 arguments, got {}", args.size);
        return;
    }
    if (args.ptr[0].type != msgpack::type::POSITIVE_INTEGER ||
        args.ptr[1].type != msgpack::type::POSITIVE_INTEGER ||
        args.ptr[2].type != msgpack::type::POSITIVE_INTEGER) {
        LOG_WARN("grid_cursor_goto: invalid argument types");
        return;
    }

    m_current_grid = args.ptr[0].as<uint32_t>();
    m_state.cursor_y = args.ptr[1].as<uint32_t>();
    m_state.cursor_x = args.ptr[2].as<uint32_t>();
}

void NvimWidget::_redraw_grid_scroll(msgpack::object_array& args) {
    if (args.size < 7) {
        LOG_WARN("grid_scroll: expected 7 arguments, got {}", args.size);
        return;
    }
    if (args.ptr[0].type != msgpack::type::POSITIVE_INTEGER ||
        args.ptr[1].type != msgpack::type::POSITIVE_INTEGER ||
        args.ptr[2].type != msgpack::type::POSITIVE_INTEGER ||
        args.ptr[3].type != msgpack::type::POSITIVE_INTEGER ||
        args.ptr[4].type != msgpack::type::POSITIVE_INTEGER ||
        (args.ptr[5].type != msgpack::type::POSITIVE_INTEGER &&
         args.ptr[5].type != msgpack::type::NEGATIVE_INTEGER) ||
        (args.ptr[6].type != msgpack::type::POSITIVE_INTEGER &&
         args.ptr[6].type != msgpack::type::NEGATIVE_INTEGER)) {
        LOG_WARN("grid_scroll: invalid argument types");
        return;
    }

    uint32_t grid_id = args.ptr[0].as<uint32_t>();
    auto it = m_grids.find(grid_id);
    if (it == m_grids.end()) {
        return;
    }

    it->second.m_scroll_region.top = args.ptr[1].as<uint32_t>();
    it->second.m_scroll_region.bot = args.ptr[2].as<uint32_t>();
    it->second.m_scroll_region.left = args.ptr[3].as<uint32_t>();
    it->second.m_scroll_region.right = args.ptr[4].as<uint32_t>();

    int64_t rows = 0;
    if (args.ptr[5].type == msgpack::type::POSITIVE_INTEGER) {
        rows = static_cast<int64_t>(args.ptr[5].as<uint64_t>());
    } else {
        rows = args.ptr[5].as<int64_t>();
    }

    it->second.scroll_region(static_cast<int>(rows));
}

void NvimWidget::_redraw_grid_destroy(msgpack::object_array& args) {
    if (args.size < 1) {
        LOG_WARN("grid_destroy: expected 1 argument, got {}", args.size);
        return;
    }
    if (args.ptr[0].type != msgpack::type::POSITIVE_INTEGER) {
        LOG_WARN("grid_destroy: argument must be a positive integer");
        return;
    }

    uint32_t grid_id = args.ptr[0].as<uint32_t>();
    m_grids.erase(grid_id);
    m_windows.erase(grid_id);

    // If we destroyed the current grid, switch to grid 1
    if (grid_id == m_current_grid && grid_id != 1) {
        m_current_grid = 1;
    }
}

void NvimWidget::_redraw_hl_attr_define(msgpack::object_array& args) {
    if (args.size < 2) {
        LOG_WARN("hl_attr_define: expected at least 2 arguments, got {}",
                 args.size);
        return;
    }
    if (args.ptr[0].type != msgpack::type::POSITIVE_INTEGER ||
        args.ptr[1].type != msgpack::type::MAP) {
        LOG_WARN("hl_attr_define: invalid argument types");
        return;
    }

    int hl_id = static_cast<int>(args.ptr[0].as<uint32_t>());
    msgpack::object_map& attr_map = args.ptr[1].via.map;

    // Start from defaults
    HighlightAttr hl;
    hl.fg = m_default_fg;
    hl.bg = m_default_bg;
    hl.sp = m_default_sp;

    for (size_t i = 0; i < attr_map.size; i++) {
        if (attr_map.ptr[i].key.type != msgpack::type::STR) {
            continue;
        }
        std::string key = attr_map.ptr[i].key.as<std::string>();
        auto& val = attr_map.ptr[i].val;

        if (key == "foreground" &&
            val.type == msgpack::type::POSITIVE_INTEGER) {
            uint32_t rgb = val.as<uint32_t>();
            hl.fg.x = static_cast<float>((rgb >> 16) & 0xFF) / 255.0f;
            hl.fg.y = static_cast<float>((rgb >> 8) & 0xFF) / 255.0f;
            hl.fg.z = static_cast<float>(rgb & 0xFF) / 255.0f;
            hl.fg.w = 1.0f;
        } else if (key == "background" &&
                   val.type == msgpack::type::POSITIVE_INTEGER) {
            uint32_t rgb = val.as<uint32_t>();
            hl.bg.x = static_cast<float>((rgb >> 16) & 0xFF) / 255.0f;
            hl.bg.y = static_cast<float>((rgb >> 8) & 0xFF) / 255.0f;
            hl.bg.z = static_cast<float>(rgb & 0xFF) / 255.0f;
            hl.bg.w = 1.0f;
        } else if (key == "special" &&
                   val.type == msgpack::type::POSITIVE_INTEGER) {
            uint32_t rgb = val.as<uint32_t>();
            hl.sp.x = static_cast<float>((rgb >> 16) & 0xFF) / 255.0f;
            hl.sp.y = static_cast<float>((rgb >> 8) & 0xFF) / 255.0f;
            hl.sp.z = static_cast<float>(rgb & 0xFF) / 255.0f;
            hl.sp.w = 1.0f;
        } else if (key == "bold" && val.type == msgpack::type::BOOLEAN) {
            hl.bold = val.as<bool>();
        } else if (key == "italic" && val.type == msgpack::type::BOOLEAN) {
            hl.italic = val.as<bool>();
        } else if (key == "underline" && val.type == msgpack::type::BOOLEAN) {
            hl.underline = val.as<bool>();
        } else if (key == "undercurl" && val.type == msgpack::type::BOOLEAN) {
            hl.undercurl = val.as<bool>();
        } else if (key == "reverse" && val.type == msgpack::type::BOOLEAN) {
            hl.reverse = val.as<bool>();
        }
    }

    m_hl_attrs[hl_id] = hl;
}

void NvimWidget::_redraw_hl_group_set(msgpack::object_array& args) {
    if (args.size < 2) {
        LOG_WARN("hl_group_set: expected 2 arguments, got {}", args.size);
        return;
    }
    if (args.ptr[0].type != msgpack::type::STR ||
        args.ptr[1].type != msgpack::type::POSITIVE_INTEGER) {
        LOG_WARN("hl_group_set: invalid argument types");
        return;
    }

    std::string group_name = args.ptr[0].as<std::string>();
    int hl_id = static_cast<int>(args.ptr[1].as<uint32_t>());
    m_hl_group_map[group_name] = hl_id;
}

// --- Window positioning handlers (ext_multigrid) ---

void NvimWidget::_redraw_win_pos(msgpack::object_array& args) {
    // ["win_pos", grid, win, start_row, start_col, width, height]
    if (args.size < 6) {
        LOG_WARN("win_pos: expected 6 arguments, got {}", args.size);
        return;
    }

    uint32_t grid_id = static_cast<uint32_t>(args.ptr[0].as<int64_t>());
    int64_t win_handle = _extract_handle(args.ptr[1]);
    int start_row = static_cast<int>(args.ptr[2].as<int64_t>());
    int start_col = static_cast<int>(args.ptr[3].as<int64_t>());
    int width = static_cast<int>(args.ptr[4].as<int64_t>());
    int height = static_cast<int>(args.ptr[5].as<int64_t>());

    auto& info = m_windows[grid_id];
    info.grid_id = grid_id;
    info.window_handle = static_cast<uint64_t>(win_handle);
    info.visible = true;
    info.floating = false;
    info.start_row = start_row;
    info.start_col = start_col;
    info.width = width;
    info.height = height;

    // Ensure grid exists and is sized correctly
    auto it = m_grids.find(grid_id);
    if (it == m_grids.end()) {
        Grid new_grid;
        new_grid.id = grid_id;
        new_grid.resize(static_cast<uint32_t>(width),
                        static_cast<uint32_t>(height));
        new_grid.visible = true;
        m_grids[grid_id] = std::move(new_grid);
    } else {
        it->second.resize(static_cast<uint32_t>(width),
                          static_cast<uint32_t>(height));
        it->second.visible = true;
    }

    LOG_TRACE("win_pos: grid={} win={} pos=({},{}) size=({},{})", grid_id,
              win_handle, start_row, start_col, width, height);
}

void NvimWidget::_redraw_win_float_pos(msgpack::object_array& args) {
    // ["win_float_pos", grid, win, anchor, anchor_grid, anchor_row,
    //   anchor_col, mouse_enabled, zindex, compindex, screen_row, screen_col]
    if (args.size < 11) {
        LOG_WARN("win_float_pos: expected 11 arguments, got {}", args.size);
        return;
    }

    uint32_t grid_id = static_cast<uint32_t>(args.ptr[0].as<int64_t>());
    int64_t win_val = _extract_handle(args.ptr[1]);
    uint64_t win_handle = (win_val >= 0) ? static_cast<uint64_t>(win_val) : 0;
    std::string anchor = args.ptr[2].as<std::string>();
    int anchor_grid = static_cast<int>(args.ptr[3].as<int64_t>());
    float anchor_row = args.ptr[4].as<float>();
    float anchor_col = args.ptr[5].as<float>();
    bool mouse_enabled = args.ptr[6].as<bool>();
    int zindex = static_cast<int>(args.ptr[7].as<int64_t>());
    int compindex = static_cast<int>(args.ptr[8].as<int64_t>());
    int screen_row = static_cast<int>(args.ptr[9].as<int64_t>());
    int screen_col = static_cast<int>(args.ptr[10].as<int64_t>());

    auto& info = m_windows[grid_id];
    info.grid_id = grid_id;
    info.window_handle = win_handle;
    info.visible = true;
    info.floating = true;
    info.start_row = screen_row;
    info.start_col = screen_col;
    info.anchor = std::move(anchor);
    info.anchor_grid = anchor_grid;
    info.anchor_row = anchor_row;
    info.anchor_col = anchor_col;
    info.mouse_enabled = mouse_enabled;
    info.zindex = zindex;
    info.compindex = compindex;

    // Ensure grid exists
    auto it = m_grids.find(grid_id);
    if (it != m_grids.end()) {
        info.width = static_cast<int>(it->second.width);
        info.height = static_cast<int>(it->second.height);
        it->second.visible = true;
        it->second.compindex = compindex;
    }

    LOG_TRACE("win_float_pos: grid={} win={} anchor={} zidx={} comp={} "
              "screen=({},{}) mouse={}",
              grid_id, win_handle, info.anchor, zindex, compindex, screen_row,
              screen_col, mouse_enabled);
}

void NvimWidget::_redraw_win_hide(msgpack::object_array& args) {
    // ["win_hide", grid]
    if (args.size < 1) {
        LOG_WARN("win_hide: expected 1 argument, got {}", args.size);
        return;
    }

    uint32_t grid_id = static_cast<uint32_t>(args.ptr[0].as<int64_t>());

    auto win_it = m_windows.find(grid_id);
    if (win_it != m_windows.end()) {
        win_it->second.visible = false;
    }

    auto grid_it = m_grids.find(grid_id);
    if (grid_it != m_grids.end()) {
        grid_it->second.visible = false;
    }

    LOG_TRACE("win_hide: grid={}", grid_id);
}

void NvimWidget::_redraw_win_close(msgpack::object_array& args) {
    // ["win_close", grid]
    if (args.size < 1) {
        LOG_WARN("win_close: expected 1 argument, got {}", args.size);
        return;
    }

    uint32_t grid_id = static_cast<uint32_t>(args.ptr[0].as<int64_t>());

    m_windows.erase(grid_id);
    m_grids.erase(grid_id);

    // If the closed grid was the current grid, fall back to grid 1
    if (m_current_grid == grid_id) {
        m_current_grid = 1;
    }

    LOG_TRACE("win_close: grid={}", grid_id);
}

void NvimWidget::_redraw_win_viewport(msgpack::object_array& args) {
    // ["win_viewport", grid, win, topline, botline, curline, curcol,
    //   line_count, scroll_delta]
    if (args.size < 8) {
        LOG_WARN("win_viewport: expected 8 arguments, got {}", args.size);
        return;
    }

    uint32_t grid_id = static_cast<uint32_t>(args.ptr[0].as<int64_t>());
    int topline = static_cast<int>(args.ptr[2].as<int64_t>());
    int botline = static_cast<int>(args.ptr[3].as<int64_t>());
    int curline = static_cast<int>(args.ptr[4].as<int64_t>());
    int curcol = static_cast<int>(args.ptr[5].as<int64_t>());
    int line_count = static_cast<int>(args.ptr[6].as<int64_t>());
    int64_t scroll_delta = args.ptr[7].as<int64_t>();

    auto win_it = m_windows.find(grid_id);
    if (win_it != m_windows.end()) {
        auto& info = win_it->second;
        info.topline = topline;
        info.botline = botline;
        info.curline = curline;
        info.curcol = curcol;
        info.line_count = line_count;
        info.scroll_delta = scroll_delta;
    } else {
        // WindowInfo not yet created — store in a default entry
        auto& info = m_windows[grid_id];
        info.grid_id = grid_id;
        info.topline = topline;
        info.botline = botline;
        info.curline = curline;
        info.curcol = curcol;
        info.line_count = line_count;
        info.scroll_delta = scroll_delta;
    }

    LOG_TRACE("win_viewport: grid={} topline={} botline={} cur=({},{}) "
              "line_count={} scroll_delta={}",
              grid_id, topline, botline, curline, curcol, line_count,
              scroll_delta);
}

void NvimWidget::_redraw_win_viewport_margins(msgpack::object_array& args) {
    // ["win_viewport_margins", grid, win, top, bottom, left, right]
    if (args.size < 6) {
        LOG_WARN("win_viewport_margins: expected 6 arguments, got {}",
                 args.size);
        return;
    }

    uint32_t grid_id = static_cast<uint32_t>(args.ptr[0].as<int64_t>());
    int top = static_cast<int>(args.ptr[2].as<int64_t>());
    int bottom = static_cast<int>(args.ptr[3].as<int64_t>());
    int left = static_cast<int>(args.ptr[4].as<int64_t>());
    int right = static_cast<int>(args.ptr[5].as<int64_t>());

    auto& info = m_windows[grid_id];
    info.grid_id = grid_id;
    info.margin_top = top;
    info.margin_bottom = bottom;
    info.margin_left = left;
    info.margin_right = right;

    LOG_TRACE("win_viewport_margins: grid={} margins=({},{},{},{})", grid_id,
              top, bottom, left, right);
}

// win_extmark events carry only position (grid, win, ns_id, mark_id, row, col)
// — no decoration data. Sign characters and virtual text are rendered into grid
// cells by Neovim and delivered via standard grid_line events, which
// _render_grid_layer already displays. This matches Neovide's approach.
static void _redraw_win_extmark(msgpack::object_array& args) { (void)args; }

// update_menu events signal that Neovim menu definitions have changed.
// This is a stub — full menu bar rendering will be added when the
// application supports multiple nvim widgets.
static void _redraw_update_menu(msgpack::object_array& args) { (void)args; }

void NvimWidget::_redraw_msg_set_pos(msgpack::object_array& args) {
    // ["msg_set_pos", grid, row, scrolled, sep_char, zindex, compindex]
    if (args.size < 6) {
        LOG_WARN("msg_set_pos: expected 6 arguments, got {}", args.size);
        return;
    }

    uint32_t grid_id = static_cast<uint32_t>(args.ptr[0].as<int64_t>());
    int row = static_cast<int>(args.ptr[1].as<int64_t>());
    bool scrolled = args.ptr[2].as<bool>();
    std::string sep_char = args.ptr[3].as<std::string>();
    int zindex = static_cast<int>(args.ptr[4].as<int64_t>());
    int compindex = static_cast<int>(args.ptr[5].as<int64_t>());

    // Store message grid positioning info
    auto& info = m_windows[grid_id];
    info.grid_id = grid_id;
    info.visible = true;
    info.floating = true;
    info.start_row = row;
    info.start_col = 0;
    info.zindex = zindex;
    info.compindex = compindex;

    auto grid_it = m_grids.find(grid_id);
    if (grid_it != m_grids.end()) {
        grid_it->second.visible = true;
        grid_it->second.compindex = compindex;
        info.width = static_cast<int>(grid_it->second.width);
        info.height = static_cast<int>(grid_it->second.height);
    }

    LOG_TRACE("msg_set_pos: grid={} row={} scrolled={} sep='{}' "
              "zidx={} comp={}",
              grid_id, row, scrolled, sep_char, zindex, compindex);
}

// --- Cmdline event handlers (ext_cmdline) ---

void NvimWidget::_cmdline_show(msgpack::object_array& args) {
    if (args.size < 7) {
        LOG_WARN("cmdline_show: expected 7 arguments, got {}", args.size);
        return;
    }

    // Parse content: Array of [hl_id, text, raw_hl_id]
    m_cmdline_content.clear();
    if (args.ptr[0].type == msgpack::type::ARRAY) {
        msgpack::object_array& content_arr = args.ptr[0].via.array;
        m_cmdline_content.reserve(content_arr.size);
        for (size_t i = 0; i < content_arr.size; i++) {
            if (content_arr.ptr[i].type != msgpack::type::ARRAY ||
                content_arr.ptr[i].via.array.size < 3) {
                continue;
            }
            msgpack::object_array& chunk = content_arr.ptr[i].via.array;
            CmdlineChunk c;
            if (chunk.ptr[0].type == msgpack::type::POSITIVE_INTEGER) {
                c.hl_id = static_cast<int>(chunk.ptr[0].as<uint32_t>());
            } else if (chunk.ptr[0].type == msgpack::type::NEGATIVE_INTEGER) {
                c.hl_id = chunk.ptr[0].as<int32_t>();
            }
            if (chunk.ptr[1].type == msgpack::type::STR) {
                c.text = chunk.ptr[1].as<std::string>();
            }
            if (chunk.ptr[2].type == msgpack::type::POSITIVE_INTEGER) {
                c.raw_hl_id = static_cast<int>(chunk.ptr[2].as<uint32_t>());
            } else if (chunk.ptr[2].type == msgpack::type::NEGATIVE_INTEGER) {
                c.raw_hl_id = chunk.ptr[2].as<int32_t>();
            }
            m_cmdline_content.push_back(std::move(c));
        }
    }

    // pos
    if (args.ptr[1].type == msgpack::type::POSITIVE_INTEGER) {
        m_cmdline_pos = static_cast<int>(args.ptr[1].as<uint32_t>());
    } else if (args.ptr[1].type == msgpack::type::NEGATIVE_INTEGER) {
        m_cmdline_pos = args.ptr[1].as<int32_t>();
    }

    // firstc
    if (args.ptr[2].type == msgpack::type::STR) {
        m_cmdline_firstc = args.ptr[2].as<std::string>();
    }

    // prompt
    if (args.ptr[3].type == msgpack::type::STR) {
        m_cmdline_prompt = args.ptr[3].as<std::string>();
    }

    // indent
    if (args.ptr[4].type == msgpack::type::POSITIVE_INTEGER) {
        m_cmdline_indent = static_cast<int>(args.ptr[4].as<uint32_t>());
    }

    // level
    if (args.ptr[5].type == msgpack::type::POSITIVE_INTEGER) {
        m_cmdline_level = static_cast<int>(args.ptr[5].as<uint32_t>());
    }

    // hl_id (prompt highlight, unused for now)
    // args.ptr[6] ignored

    // When cmdline is shown, clear any pending messages — the cmdline
    // takes priority over the message area (matching Neovim TUI behavior).
    if (m_msg_visible) {
        m_msg_visible = false;
        m_msg_entries.clear();
        m_msg_kind.clear();
    }
    m_msg_showmode_visible = false;
    m_msg_showmode_content.clear();
    m_msg_showcmd_visible = false;
    m_msg_showcmd_content.clear();
    m_msg_ruler_visible = false;
    m_msg_ruler_content.clear();

    m_cmdline_visible = true;
    LOG_TRACE("cmdline_show: level={}, pos={}, chunks={}, firstc='{}', "
              "prompt='{}', indent={}",
              m_cmdline_level, m_cmdline_pos, m_cmdline_content.size(),
              m_cmdline_firstc, m_cmdline_prompt, m_cmdline_indent);
}

void NvimWidget::_cmdline_hide(msgpack::object_array& args) {
    int level = 0;
    if (args.size >= 1) {
        if (args.ptr[0].type == msgpack::type::POSITIVE_INTEGER) {
            level = static_cast<int>(args.ptr[0].as<uint32_t>());
        } else if (args.ptr[0].type == msgpack::type::NEGATIVE_INTEGER) {
            level = args.ptr[0].as<int32_t>();
        }
    }

    if (level == m_cmdline_level) {
        m_cmdline_visible = false;
        m_cmdline_content.clear();
        m_cmdline_pos = 0;
        m_cmdline_firstc.clear();
        m_cmdline_prompt.clear();
        m_cmdline_special_char.clear();
        LOG_TRACE("cmdline_hide: level={}", level);
    }
}

void NvimWidget::_cmdline_pos(msgpack::object_array& args) {
    if (args.size < 2) {
        LOG_WARN("cmdline_pos: expected 2 arguments, got {}", args.size);
        return;
    }

    int pos = 0;
    if (args.ptr[0].type == msgpack::type::POSITIVE_INTEGER) {
        pos = static_cast<int>(args.ptr[0].as<uint32_t>());
    } else if (args.ptr[0].type == msgpack::type::NEGATIVE_INTEGER) {
        pos = args.ptr[0].as<int32_t>();
    }

    int level = 0;
    if (args.ptr[1].type == msgpack::type::POSITIVE_INTEGER) {
        level = static_cast<int>(args.ptr[1].as<uint32_t>());
    } else if (args.ptr[1].type == msgpack::type::NEGATIVE_INTEGER) {
        level = args.ptr[1].as<int32_t>();
    }

    if (level == m_cmdline_level) {
        m_cmdline_pos = pos;
    }
}

void NvimWidget::_cmdline_special_char(msgpack::object_array& args) {
    if (args.size < 3) {
        LOG_WARN("cmdline_special_char: expected 3 arguments, got {}",
                 args.size);
        return;
    }

    if (args.ptr[0].type == msgpack::type::STR) {
        m_cmdline_special_char = args.ptr[0].as<std::string>();
    }
    if (args.ptr[1].type == msgpack::type::BOOLEAN) {
        m_cmdline_special_shift = args.ptr[1].as<bool>();
    }
    // level in args.ptr[2] — ignored for now
}

void NvimWidget::_cmdline_block_show(msgpack::object_array& args) {
    m_cmdline_block_lines.clear();

    if (args.size < 1 || args.ptr[0].type != msgpack::type::ARRAY) {
        LOG_WARN("cmdline_block_show: expected 1 array argument");
        return;
    }

    msgpack::object_array& lines = args.ptr[0].via.array;
    m_cmdline_block_lines.reserve(lines.size);

    for (size_t li = 0; li < lines.size; li++) {
        if (lines.ptr[li].type != msgpack::type::ARRAY) {
            continue;
        }
        msgpack::object_array& chunks = lines.ptr[li].via.array;
        std::vector<CmdlineChunk> line_chunks;
        line_chunks.reserve(chunks.size);

        for (size_t ci = 0; ci < chunks.size; ci++) {
            if (chunks.ptr[ci].type != msgpack::type::ARRAY ||
                chunks.ptr[ci].via.array.size < 3) {
                continue;
            }
            msgpack::object_array& chunk = chunks.ptr[ci].via.array;
            CmdlineChunk c;
            if (chunk.ptr[0].type == msgpack::type::POSITIVE_INTEGER) {
                c.hl_id = static_cast<int>(chunk.ptr[0].as<uint32_t>());
            } else if (chunk.ptr[0].type == msgpack::type::NEGATIVE_INTEGER) {
                c.hl_id = chunk.ptr[0].as<int32_t>();
            }
            if (chunk.ptr[1].type == msgpack::type::STR) {
                c.text = chunk.ptr[1].as<std::string>();
            }
            if (chunk.ptr[2].type == msgpack::type::POSITIVE_INTEGER) {
                c.raw_hl_id = static_cast<int>(chunk.ptr[2].as<uint32_t>());
            } else if (chunk.ptr[2].type == msgpack::type::NEGATIVE_INTEGER) {
                c.raw_hl_id = chunk.ptr[2].as<int32_t>();
            }
            line_chunks.push_back(std::move(c));
        }

        m_cmdline_block_lines.push_back(std::move(line_chunks));
    }

    m_cmdline_block_visible = !m_cmdline_block_lines.empty();
    LOG_TRACE("cmdline_block_show: {} lines", m_cmdline_block_lines.size());
}

void NvimWidget::_cmdline_block_append(msgpack::object_array& args) {
    if (args.size < 1 || args.ptr[0].type != msgpack::type::ARRAY) {
        LOG_WARN("cmdline_block_append: expected 1 array argument");
        return;
    }

    msgpack::object_array& chunks = args.ptr[0].via.array;
    std::vector<CmdlineChunk> line_chunks;
    line_chunks.reserve(chunks.size);

    for (size_t ci = 0; ci < chunks.size; ci++) {
        if (chunks.ptr[ci].type != msgpack::type::ARRAY ||
            chunks.ptr[ci].via.array.size < 3) {
            continue;
        }
        msgpack::object_array& chunk = chunks.ptr[ci].via.array;
        CmdlineChunk c;
        if (chunk.ptr[0].type == msgpack::type::POSITIVE_INTEGER) {
            c.hl_id = static_cast<int>(chunk.ptr[0].as<uint32_t>());
        } else if (chunk.ptr[0].type == msgpack::type::NEGATIVE_INTEGER) {
            c.hl_id = chunk.ptr[0].as<int32_t>();
        }
        if (chunk.ptr[1].type == msgpack::type::STR) {
            c.text = chunk.ptr[1].as<std::string>();
        }
        if (chunk.ptr[2].type == msgpack::type::POSITIVE_INTEGER) {
            c.raw_hl_id = static_cast<int>(chunk.ptr[2].as<uint32_t>());
        } else if (chunk.ptr[2].type == msgpack::type::NEGATIVE_INTEGER) {
            c.raw_hl_id = chunk.ptr[2].as<int32_t>();
        }
        line_chunks.push_back(std::move(c));
    }

    m_cmdline_block_lines.push_back(std::move(line_chunks));
    LOG_TRACE("cmdline_block_append: line appended (total {} lines)",
              m_cmdline_block_lines.size());
}

void NvimWidget::_cmdline_block_hide(msgpack::object_array& /*args*/) {
    m_cmdline_block_lines.clear();
    m_cmdline_block_visible = false;
    LOG_TRACE("cmdline_block_hide");
}

// =========================================================================
// Message event handlers (ext_messages)
// =========================================================================

void NvimWidget::_redraw_msg_show(msgpack::object_array& args) {
    if (args.size < 7) {
        LOG_WARN("msg_show: expected at least 7 arguments, got {}", args.size);
        return;
    }

    // Parse kind (0)
    std::string kind;
    if (args.ptr[0].type == msgpack::type::STR) {
        kind = args.ptr[0].as<std::string>();
    }

    // Parse content (1): Array of [attr_id, text, hl_id] tuples
    std::vector<MessageChunk> chunks;
    if (args.ptr[1].type == msgpack::type::ARRAY) {
        msgpack::object_array& content_arr = args.ptr[1].via.array;
        chunks.reserve(content_arr.size);
        for (size_t i = 0; i < content_arr.size; i++) {
            if (content_arr.ptr[i].type != msgpack::type::ARRAY ||
                content_arr.ptr[i].via.array.size < 3) {
                continue;
            }
            msgpack::object_array& chunk = content_arr.ptr[i].via.array;
            MessageChunk c;
            if (chunk.ptr[0].type == msgpack::type::POSITIVE_INTEGER) {
                c.attr_id = static_cast<int>(chunk.ptr[0].as<uint32_t>());
            } else if (chunk.ptr[0].type == msgpack::type::NEGATIVE_INTEGER) {
                c.attr_id = chunk.ptr[0].as<int32_t>();
            }
            if (chunk.ptr[1].type == msgpack::type::STR) {
                c.text = chunk.ptr[1].as<std::string>();
            }
            if (chunk.ptr[2].type == msgpack::type::POSITIVE_INTEGER) {
                c.hl_id = static_cast<int>(chunk.ptr[2].as<uint32_t>());
            } else if (chunk.ptr[2].type == msgpack::type::NEGATIVE_INTEGER) {
                c.hl_id = chunk.ptr[2].as<int32_t>();
            }
            chunks.push_back(std::move(c));
        }
    }

    // Parse replace_last (2)
    bool replace_last = false;
    if (args.ptr[2].type == msgpack::type::BOOLEAN) {
        replace_last = args.ptr[2].as<bool>();
    }

    // Parse history (3) — stored for potential future use
    // bool history = false;
    // if (args.ptr[3].type == msgpack::type::BOOLEAN) {
    //     history = args.ptr[3].as<bool>();
    // }

    // Parse append (4)
    bool append = false;
    if (args.ptr[4].type == msgpack::type::BOOLEAN) {
        append = args.ptr[4].as<bool>();
    }

    // Parse id (5) — stored for potential future use
    // (msgpack object, can be Integer or String)
    // int64_t msg_id = 0;
    // if (args.ptr[5].type == msgpack::type::POSITIVE_INTEGER) {
    //     msg_id = args.ptr[5].as<int64_t>();
    // }

    // Parse trigger (6)
    // std::string trigger;
    // if (args.ptr[6].type == msgpack::type::STR) {
    //     trigger = args.ptr[6].as<std::string>();
    // }

    // Apply append: merge into the last entry if append is set
    if (append && !m_msg_entries.empty()) {
        auto& last = m_msg_entries.back();
        last.content.insert(last.content.end(),
                            std::make_move_iterator(chunks.begin()),
                            std::make_move_iterator(chunks.end()));
        last.kind = kind; // Update kind to the appended message's kind
    } else if (replace_last && !m_msg_entries.empty()) {
        // Replace the last entry in place
        m_msg_entries.back().kind = kind;
        m_msg_entries.back().content = std::move(chunks);
        m_msg_entries.back().append = append;
    } else {
        m_msg_entries.push_back({kind, std::move(chunks), append});
    }

    m_msg_kind = kind;
    m_msg_visible = true;

    // Showmode and regular messages are mutually exclusive — they share
    // the same bottom row in Neovim's TUI.  Clear showmode when a message
    // arrives (unless appending to an existing message).
    if (!append) {
        m_msg_showmode_visible = false;
        m_msg_showmode_content.clear();
    }

    LOG_TRACE("msg_show: kind='{}', chunks={}, replace_last={}, append={}",
              kind, chunks.size(), replace_last, append);
}

void NvimWidget::_redraw_msg_clear(msgpack::object_array& /*args*/) {
    m_msg_visible = false;
    m_msg_entries.clear();
    m_msg_kind.clear();
    LOG_TRACE("msg_clear");
}

void NvimWidget::_redraw_msg_showmode(msgpack::object_array& args) {
    m_msg_showmode_content.clear();

    if (args.size >= 1 && args.ptr[0].type == msgpack::type::ARRAY) {
        msgpack::object_array& content_arr = args.ptr[0].via.array;
        m_msg_showmode_content.reserve(content_arr.size);
        for (size_t i = 0; i < content_arr.size; i++) {
            if (content_arr.ptr[i].type != msgpack::type::ARRAY ||
                content_arr.ptr[i].via.array.size < 3) {
                continue;
            }
            msgpack::object_array& chunk = content_arr.ptr[i].via.array;
            MessageChunk c;
            if (chunk.ptr[0].type == msgpack::type::POSITIVE_INTEGER) {
                c.attr_id = static_cast<int>(chunk.ptr[0].as<uint32_t>());
            } else if (chunk.ptr[0].type == msgpack::type::NEGATIVE_INTEGER) {
                c.attr_id = chunk.ptr[0].as<int32_t>();
            }
            if (chunk.ptr[1].type == msgpack::type::STR) {
                c.text = chunk.ptr[1].as<std::string>();
            }
            if (chunk.ptr[2].type == msgpack::type::POSITIVE_INTEGER) {
                c.hl_id = static_cast<int>(chunk.ptr[2].as<uint32_t>());
            } else if (chunk.ptr[2].type == msgpack::type::NEGATIVE_INTEGER) {
                c.hl_id = chunk.ptr[2].as<int32_t>();
            }
            m_msg_showmode_content.push_back(std::move(c));
        }
    }

    m_msg_showmode_visible = !m_msg_showmode_content.empty();

    // Showmode and regular messages are mutually exclusive — they share
    // the same bottom row in Neovim's TUI.  When showmode content arrives,
    // clear any pending regular messages.
    if (m_msg_showmode_visible) {
        m_msg_visible = false;
        m_msg_entries.clear();
        m_msg_kind.clear();
    }

    LOG_TRACE("msg_showmode: visible={}, chunks={}", m_msg_showmode_visible,
              m_msg_showmode_content.size());
}

void NvimWidget::_redraw_msg_showcmd(msgpack::object_array& args) {
    m_msg_showcmd_content.clear();

    if (args.size >= 1 && args.ptr[0].type == msgpack::type::ARRAY) {
        msgpack::object_array& content_arr = args.ptr[0].via.array;
        m_msg_showcmd_content.reserve(content_arr.size);
        for (size_t i = 0; i < content_arr.size; i++) {
            if (content_arr.ptr[i].type != msgpack::type::ARRAY ||
                content_arr.ptr[i].via.array.size < 3) {
                continue;
            }
            msgpack::object_array& chunk = content_arr.ptr[i].via.array;
            MessageChunk c;
            if (chunk.ptr[0].type == msgpack::type::POSITIVE_INTEGER) {
                c.attr_id = static_cast<int>(chunk.ptr[0].as<uint32_t>());
            } else if (chunk.ptr[0].type == msgpack::type::NEGATIVE_INTEGER) {
                c.attr_id = chunk.ptr[0].as<int32_t>();
            }
            if (chunk.ptr[1].type == msgpack::type::STR) {
                c.text = chunk.ptr[1].as<std::string>();
            }
            if (chunk.ptr[2].type == msgpack::type::POSITIVE_INTEGER) {
                c.hl_id = static_cast<int>(chunk.ptr[2].as<uint32_t>());
            } else if (chunk.ptr[2].type == msgpack::type::NEGATIVE_INTEGER) {
                c.hl_id = chunk.ptr[2].as<int32_t>();
            }
            m_msg_showcmd_content.push_back(std::move(c));
        }
    }

    m_msg_showcmd_visible = !m_msg_showcmd_content.empty();
    LOG_TRACE("msg_showcmd: visible={}, chunks={}", m_msg_showcmd_visible,
              m_msg_showcmd_content.size());
}

void NvimWidget::_redraw_msg_ruler(msgpack::object_array& args) {
    m_msg_ruler_content.clear();

    if (args.size >= 1 && args.ptr[0].type == msgpack::type::ARRAY) {
        msgpack::object_array& content_arr = args.ptr[0].via.array;
        m_msg_ruler_content.reserve(content_arr.size);
        for (size_t i = 0; i < content_arr.size; i++) {
            if (content_arr.ptr[i].type != msgpack::type::ARRAY ||
                content_arr.ptr[i].via.array.size < 3) {
                continue;
            }
            msgpack::object_array& chunk = content_arr.ptr[i].via.array;
            MessageChunk c;
            if (chunk.ptr[0].type == msgpack::type::POSITIVE_INTEGER) {
                c.attr_id = static_cast<int>(chunk.ptr[0].as<uint32_t>());
            } else if (chunk.ptr[0].type == msgpack::type::NEGATIVE_INTEGER) {
                c.attr_id = chunk.ptr[0].as<int32_t>();
            }
            if (chunk.ptr[1].type == msgpack::type::STR) {
                c.text = chunk.ptr[1].as<std::string>();
            }
            if (chunk.ptr[2].type == msgpack::type::POSITIVE_INTEGER) {
                c.hl_id = static_cast<int>(chunk.ptr[2].as<uint32_t>());
            } else if (chunk.ptr[2].type == msgpack::type::NEGATIVE_INTEGER) {
                c.hl_id = chunk.ptr[2].as<int32_t>();
            }
            m_msg_ruler_content.push_back(std::move(c));
        }
    }

    m_msg_ruler_visible = !m_msg_ruler_content.empty();
    LOG_TRACE("msg_ruler: visible={}, chunks={}", m_msg_ruler_visible,
              m_msg_ruler_content.size());
}

void NvimWidget::_redraw_msg_history_show(msgpack::object_array& args) {
    m_msg_history_entries.clear();

    if (args.size < 2) {
        LOG_WARN("msg_history_show: expected 2 arguments, got {}", args.size);
        return;
    }

    // Parse entries (0): Array of [kind, content, append]
    if (args.ptr[0].type == msgpack::type::ARRAY) {
        msgpack::object_array& entries_arr = args.ptr[0].via.array;
        m_msg_history_entries.reserve(entries_arr.size);
        for (size_t i = 0; i < entries_arr.size; i++) {
            if (entries_arr.ptr[i].type != msgpack::type::ARRAY ||
                entries_arr.ptr[i].via.array.size < 3) {
                continue;
            }
            msgpack::object_array& entry_arr = entries_arr.ptr[i].via.array;

            MessageEntry entry;

            // kind (0)
            if (entry_arr.ptr[0].type == msgpack::type::STR) {
                entry.kind = entry_arr.ptr[0].as<std::string>();
            }

            // content (1): Array of [attr_id, text, hl_id]
            if (entry_arr.ptr[1].type == msgpack::type::ARRAY) {
                msgpack::object_array& content_arr = entry_arr.ptr[1].via.array;
                entry.content.reserve(content_arr.size);
                for (size_t j = 0; j < content_arr.size; j++) {
                    if (content_arr.ptr[j].type != msgpack::type::ARRAY ||
                        content_arr.ptr[j].via.array.size < 3) {
                        continue;
                    }
                    msgpack::object_array& chunk = content_arr.ptr[j].via.array;
                    MessageChunk c;
                    if (chunk.ptr[0].type == msgpack::type::POSITIVE_INTEGER) {
                        c.attr_id =
                            static_cast<int>(chunk.ptr[0].as<uint32_t>());
                    } else if (chunk.ptr[0].type ==
                               msgpack::type::NEGATIVE_INTEGER) {
                        c.attr_id = chunk.ptr[0].as<int32_t>();
                    }
                    if (chunk.ptr[1].type == msgpack::type::STR) {
                        c.text = chunk.ptr[1].as<std::string>();
                    }
                    if (chunk.ptr[2].type == msgpack::type::POSITIVE_INTEGER) {
                        c.hl_id = static_cast<int>(chunk.ptr[2].as<uint32_t>());
                    } else if (chunk.ptr[2].type ==
                               msgpack::type::NEGATIVE_INTEGER) {
                        c.hl_id = chunk.ptr[2].as<int32_t>();
                    }
                    entry.content.push_back(std::move(c));
                }
            }

            // append (2)
            if (entry_arr.ptr[2].type == msgpack::type::BOOLEAN) {
                entry.append = entry_arr.ptr[2].as<bool>();
            }

            m_msg_history_entries.push_back(std::move(entry));
        }
    }

    // Parse prev_cmd (1)
    if (args.ptr[1].type == msgpack::type::BOOLEAN) {
        m_msg_history_prev_cmd = args.ptr[1].as<bool>();
    }

    m_msg_history_visible = true;
    m_msg_history_scroll = 0;
    LOG_TRACE("msg_history_show: entries={}, prev_cmd={}",
              m_msg_history_entries.size(), m_msg_history_prev_cmd);
}

// =========================================================================
// Message area rendering
// =========================================================================

uint32_t NvimWidget::_message_area_rows() const {
    if (m_msg_history_visible) {
        // History mode: show entries with scroll, up to reasonable max
        return std::min(static_cast<uint32_t>(m_msg_history_entries.size()),
                        uint32_t{10});
    }
    if (m_msg_visible || m_msg_showmode_visible) {
        // Active messages or mode indicator (e.g. "-- INSERT --"):
        // reserve 1 row for display.
        return 1;
    }
    return 0;
}

void NvimWidget::_render_message_area(ImDrawList* draw_list, const ImVec2& pos,
                                      float char_width, float line_height) {
    auto it = m_grids.find(m_current_grid);
    if (it == m_grids.end()) {
        return;
    }

    Grid& grid = it->second;
    float effective_line_height = line_height + static_cast<float>(m_linespace);

    uint32_t cmdline_rows = m_cmdline_visible ? 1u : 0u;
    uint32_t block_rows = _cmdline_block_rows();
    uint32_t msg_rows = _message_area_rows();
    if (msg_rows == 0) {
        return;
    }

    // The message area starts above the cmdline block (if any) and cmdline,
    // positioned relative to m_display_rows (total area).
    float msg_y =
        pos.y + (m_display_rows - cmdline_rows - block_rows - msg_rows) *
                    effective_line_height;

    // Render background for the entire message area
    ImVec2 msg_area_min(pos.x, msg_y);
    ImVec2 msg_area_max(pos.x + grid.width * char_width,
                        msg_y + msg_rows * effective_line_height);

    // Determine background color based on message kind
    ImVec4 msg_bg = m_default_bg;
    if (m_msg_history_visible) {
        // History: use a slightly elevated background
        msg_bg = ImVec4(m_default_bg.x * 1.15f, m_default_bg.y * 1.15f,
                        m_default_bg.z * 1.15f, 1.0f);
    } else if (!m_msg_kind.empty()) {
        // Error kinds: red tint
        if (m_msg_kind == "emsg" || m_msg_kind == "echoerr" ||
            m_msg_kind == "lua_error" || m_msg_kind == "rpc_error") {
            msg_bg = ImVec4(0.25f, 0.05f, 0.05f, 1.0f);
        } else if (m_msg_kind == "wmsg") {
            // Warning: yellow tint
            msg_bg = ImVec4(0.20f, 0.18f, 0.02f, 1.0f);
        } else if (m_msg_kind == "confirm") {
            // Confirm: blue tint
            msg_bg = ImVec4(0.05f, 0.08f, 0.25f, 1.0f);
        }
    }

    draw_list->AddRectFilled(msg_area_min, msg_area_max,
                             ImGui::ColorConvertFloat4ToU32(msg_bg));

    // Draw a subtle separator line above the message area
    ImU32 sep_color =
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    draw_list->AddLine(ImVec2(msg_area_min.x, msg_area_min.y),
                       ImVec2(msg_area_max.x, msg_area_min.y), sep_color);

    // --- History mode ---
    if (m_msg_history_visible) {
        // Determine which entries to show based on scroll offset
        int visible_count = static_cast<int>(msg_rows);
        int total_entries = static_cast<int>(m_msg_history_entries.size());
        int start_idx =
            std::max(0, total_entries - visible_count - m_msg_history_scroll);
        int end_idx = std::min(total_entries, start_idx + visible_count);

        for (int ei = start_idx; ei < end_idx; ei++) {
            int row = ei - start_idx;
            float row_y = msg_y + row * effective_line_height;
            const auto& entry = m_msg_history_entries[ei];

            // Prefix: entry number (1-based from bottom)
            char prefix[16];
            snprintf(prefix, sizeof(prefix), "%d: ", ei + 1);
            float x = pos.x;

            ImU32 prefix_color =
                ImGui::ColorConvertFloat4ToU32(ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            draw_list->AddText(ImVec2(x, row_y), prefix_color, prefix);
            x += ImGui::CalcTextSize(prefix).x;

            // Render content chunks
            for (const auto& chunk : entry.content) {
                if (chunk.text.empty()) {
                    continue;
                }

                ImVec4 fg = m_default_fg;
                if (chunk.attr_id != 0) {
                    auto hl_it = m_hl_attrs.find(chunk.attr_id);
                    if (hl_it != m_hl_attrs.end()) {
                        fg = hl_it->second.fg;
                    }
                }

                ImU32 text_color = ImGui::ColorConvertFloat4ToU32(fg);
                draw_list->AddText(ImVec2(x, row_y), text_color,
                                   chunk.text.c_str());
                x += ImGui::CalcTextSize(chunk.text.c_str()).x;
            }
        }
        return;
    }

    // --- Normal message mode (single row) ---
    float msg_row_y = msg_y;
    float x = pos.x;

    // Render showmode content first (e.g. "-- INSERT --") if visible.
    // This is rendered even when there are no message entries, because
    // Neovim sends mode text exclusively via msg_showmode when
    // ext_messages is enabled.
    if (m_msg_showmode_visible) {
        for (const auto& chunk : m_msg_showmode_content) {
            if (chunk.text.empty()) {
                continue;
            }
            ImVec4 fg = m_default_fg;
            if (chunk.attr_id != 0) {
                auto hl_it = m_hl_attrs.find(chunk.attr_id);
                if (hl_it != m_hl_attrs.end()) {
                    fg = hl_it->second.fg;
                }
            }
            ImU32 text_color = ImGui::ColorConvertFloat4ToU32(fg);
            draw_list->AddText(ImVec2(x, msg_row_y), text_color,
                               chunk.text.c_str());
            x += ImGui::CalcTextSize(chunk.text.c_str()).x;
        }
    }

    // Build combined message text from the last visible entry.
    if (m_msg_entries.empty()) {
        return;
    }

    // Separator between showmode and message text
    if (m_msg_showmode_visible) {
        ImU32 sep =
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        draw_list->AddText(ImVec2(x, msg_row_y), sep, "  ");
        x += ImGui::CalcTextSize("  ").x;
    }

    // Render the last message entry's content with highlighting
    const auto& last_entry = m_msg_entries.back();
    for (const auto& chunk : last_entry.content) {
        if (chunk.text.empty()) {
            continue;
        }

        ImVec4 fg = m_default_fg;
        if (chunk.attr_id != 0) {
            auto hl_it = m_hl_attrs.find(chunk.attr_id);
            if (hl_it != m_hl_attrs.end()) {
                fg = hl_it->second.fg;
            }
        }

        ImU32 text_color = ImGui::ColorConvertFloat4ToU32(fg);
        draw_list->AddText(ImVec2(x, msg_row_y), text_color,
                           chunk.text.c_str());
        x += ImGui::CalcTextSize(chunk.text.c_str()).x;
    }

    // Render showcmd content right-aligned if visible (e.g. partial command)
    if (m_msg_showcmd_visible && !m_msg_showcmd_content.empty()) {
        // Recalculate x to right-align showcmd
        float showcmd_width = 0.0f;
        for (const auto& chunk : m_msg_showcmd_content) {
            showcmd_width += ImGui::CalcTextSize(chunk.text.c_str()).x;
        }
        float showcmd_x = pos.x + grid.width * char_width - showcmd_width;

        for (const auto& chunk : m_msg_showcmd_content) {
            if (chunk.text.empty()) {
                continue;
            }
            ImVec4 fg = m_default_fg;
            if (chunk.attr_id != 0) {
                auto hl_it = m_hl_attrs.find(chunk.attr_id);
                if (hl_it != m_hl_attrs.end()) {
                    fg = hl_it->second.fg;
                }
            }
            ImU32 text_color = ImGui::ColorConvertFloat4ToU32(fg);
            draw_list->AddText(ImVec2(showcmd_x, msg_row_y), text_color,
                               chunk.text.c_str());
            showcmd_x += ImGui::CalcTextSize(chunk.text.c_str()).x;
        }
    }

    // Render ruler content right-aligned if visible
    if (m_msg_ruler_visible && !m_msg_ruler_content.empty()) {
        float ruler_width = 0.0f;
        for (const auto& chunk : m_msg_ruler_content) {
            ruler_width += ImGui::CalcTextSize(chunk.text.c_str()).x;
        }
        // Position ruler at the right edge, accounting for showcmd if present
        float ruler_x = pos.x + grid.width * char_width - ruler_width;

        if (m_msg_showcmd_visible && !m_msg_showcmd_content.empty()) {
            // Place ruler to the left of showcmd
            float showcmd_width = 0.0f;
            for (const auto& chunk : m_msg_showcmd_content) {
                showcmd_width += ImGui::CalcTextSize(chunk.text.c_str()).x;
            }
            ruler_x -= showcmd_width + char_width; // + spacer
        }

        for (const auto& chunk : m_msg_ruler_content) {
            if (chunk.text.empty()) {
                continue;
            }
            ImVec4 fg = ImVec4(0.7f, 0.7f, 0.7f, 1.0f); // Dim for ruler
            ImU32 text_color = ImGui::ColorConvertFloat4ToU32(fg);
            draw_list->AddText(ImVec2(ruler_x, msg_row_y), text_color,
                               chunk.text.c_str());
            ruler_x += ImGui::CalcTextSize(chunk.text.c_str()).x;
        }
    }
}

// =========================================================================
// Cmdline block rendering (ext_cmdline multi-line input)
// =========================================================================

uint32_t NvimWidget::_cmdline_block_rows() const {
    if (!m_cmdline_block_visible) {
        return 0;
    }
    return static_cast<uint32_t>(m_cmdline_block_lines.size());
}

uint32_t NvimWidget::_overlay_rows() const {
    uint32_t rows = 0;
    if (m_cmdline_visible) {
        rows += 1;
    }
    rows += _cmdline_block_rows();
    rows += _message_area_rows();
    // Always reserve at least 1 row at the bottom for cmdline/messages,
    // matching Neovim's default 'cmdheight'=1 behavior.
    return std::max(1u, rows);
}

void NvimWidget::_render_cmdline_block(ImDrawList* draw_list, const ImVec2& pos,
                                       float char_width, float line_height) {
    auto it = m_grids.find(m_current_grid);
    if (it == m_grids.end()) {
        return;
    }

    Grid& grid = it->second;
    float effective_line_height = line_height + static_cast<float>(m_linespace);

    uint32_t cmdline_rows = m_cmdline_visible ? 1u : 0u;
    uint32_t msg_rows = _message_area_rows();
    uint32_t block_rows = _cmdline_block_rows();
    if (block_rows == 0) {
        return;
    }

    // The block sits above the cmdline, below the message area,
    // positioned relative to m_display_rows (total area).
    float block_y = pos.y + (m_display_rows - cmdline_rows - block_rows) *
                                effective_line_height;

    // Background fill
    ImVec2 block_min(pos.x, block_y);
    ImVec2 block_max(pos.x + grid.width * char_width,
                     block_y + block_rows * effective_line_height);
    ImVec4 block_bg = ImVec4(m_default_bg.x * 1.10f, m_default_bg.y * 1.10f,
                             m_default_bg.z * 1.10f, 1.0f);
    draw_list->AddRectFilled(block_min, block_max,
                             ImGui::ColorConvertFloat4ToU32(block_bg));

    // Separator line above the block
    ImU32 sep_color =
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
    draw_list->AddLine(ImVec2(block_min.x, block_min.y),
                       ImVec2(block_max.x, block_min.y), sep_color);

    // Render each line
    for (size_t li = 0; li < m_cmdline_block_lines.size(); li++) {
        float row_y = block_y + li * effective_line_height;
        float x = pos.x;
        const auto& line_chunks = m_cmdline_block_lines[li];

        // Draw background for the full row before rendering chunks
        // (ensure no gaps between chunks).
        ImVec2 row_bg_min(pos.x, row_y);
        ImVec2 row_bg_max(pos.x + grid.width * char_width, row_y + line_height);
        draw_list->AddRectFilled(row_bg_min, row_bg_max,
                                 ImGui::ColorConvertFloat4ToU32(block_bg));

        // Draw the prompt character (e.g. ':') at the start of each line.
        // Neovim omits the firstc from cmdline_block text; we restore it
        // using the firstc from the active cmdline (which is ':'
        // during Ex-mode continuation).
        std::string firstc = m_cmdline_firstc;
        if (firstc.empty()) {
            firstc = ":"; // fallback for Ex-mode blocks
        }
        if (!firstc.empty()) {
            ImU32 prompt_color = ImGui::ColorConvertFloat4ToU32(m_default_fg);
            draw_list->AddText(ImVec2(x, row_y), prompt_color, firstc.c_str());
            x += ImGui::CalcTextSize(firstc.c_str()).x;
        }

        for (const auto& chunk : line_chunks) {
            if (chunk.text.empty()) {
                continue;
            }

            // Neovim sends firstc separately from content; no strip needed.

            ImVec4 fg = m_default_fg;
            ImVec4 bg = block_bg;
            if (chunk.hl_id != 0) {
                auto hl_it = m_hl_attrs.find(chunk.hl_id);
                if (hl_it != m_hl_attrs.end()) {
                    fg = hl_it->second.fg;
                    bg = hl_it->second.bg;
                }
            }

            float chunk_width = ImGui::CalcTextSize(chunk.text.c_str()).x;
            ImVec2 bg_min(x, row_y);
            ImVec2 bg_max(x + chunk_width, row_y + line_height);
            draw_list->AddRectFilled(bg_min, bg_max,
                                     ImGui::ColorConvertFloat4ToU32(bg));

            ImU32 text_color = ImGui::ColorConvertFloat4ToU32(fg);
            draw_list->AddText(ImVec2(x, row_y), text_color,
                               chunk.text.c_str());
            x += chunk_width;
        }
    }
}

void NvimWidget::_handle_nvim_gui_event(std::string_view event,
                                        msgpack::object_array& /*args*/) {}

void NvimWidget::_check_font_size_changed() {
    float current_font_size = ImGui::GetFontBaked()->Size;
    if (current_font_size != m_last_font_size) {
        m_last_font_size = current_font_size;
        resize(m_state.col, m_state.row);
    }
}

// Parse a single guifont entry: "FamilyName[:hNN[:b][:i]]"
// Neovim has already resolved Vimscript escaping (\, etc.) before
// sending option_set, so we only need to handle commas as separators.
static ParsedFont parse_guifont_entry(const std::string& entry) {
    ParsedFont result;

    // Find the last ":h" delimiter (family name might contain colons).
    const std::string& s = entry;
    size_t h_pos = std::string::npos;
    for (size_t i = s.length(); i >= 2; i--) {
        if (s[i - 2] == ':' && s[i - 1] == 'h') {
            h_pos = i - 2;
            break;
        }
    }

    if (h_pos != std::string::npos) {
        result.family = s.substr(0, h_pos);
        // Trim trailing whitespace from family name
        while (!result.family.empty() && result.family.back() == ' ') {
            result.family.pop_back();
        }

        // Parse modifiers starting from ":h" position
        std::string mods = s.substr(h_pos);
        size_t h_end = 2; // skip ":h"
        while (h_end < mods.length() && mods[h_end] >= '0' &&
               mods[h_end] <= '9') {
            h_end++;
        }
        if (h_end > 2) {
            result.size_pt = std::stof(mods.substr(2, h_end - 2));
        }

        std::string remaining = mods.substr(h_end);
        result.bold = (remaining.find(":b") != std::string::npos);
        result.italic = (remaining.find(":i") != std::string::npos);
    } else {
        // No ":h" found — use the whole string as family, keep default size
        result.family = s;
    }

    // Trim leading/trailing whitespace from family
    size_t start = result.family.find_first_not_of(" \t");
    size_t end = result.family.find_last_not_of(" \t");
    if (start != std::string::npos && end != std::string::npos) {
        result.family = result.family.substr(start, end - start + 1);
    } else {
        result.family.clear();
    }

    return result;
}

// Split a guifont string by commas into individual entries.
static std::vector<ParsedFont>
parse_guifont_entries(const std::string& guifont_str) {
    std::vector<ParsedFont> entries;
    if (guifont_str.empty()) {
        return entries;
    }

    size_t start = 0;
    while (start < guifont_str.length()) {
        size_t comma = guifont_str.find(',', start);
        std::string entry = guifont_str.substr(start, comma - start);
        entries.push_back(parse_guifont_entry(entry));
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }

    return entries;
}

ParsedFont NvimWidget::_parse_guifont(const std::string& guifont_str) {
    auto entries = parse_guifont_entries(guifont_str);
    if (entries.empty()) {
        return ParsedFont{};
    }
    return entries[0];
}

void NvimWidget::_check_font_reload_needed() {
    if (!m_font_reload_pending) {
        return;
    }

    // Parse the requested fonts (detect phase only — no atlas manipulation)
    ParsedFont new_font = _parse_guifont(m_requested_font);
    ParsedFont new_wide = _parse_guifont(m_requested_font_wide);

    // If no explicit guifont set, keep the current family/size
    if (new_font.family.empty()) {
        new_font = m_current_font;
        if (new_font.family.empty()) {
            new_font.family = ImApp::FontManager::get_default_font_family();
            new_font.size_pt = 14.0f;
        }
    }

    // If the parsed values match what's currently loaded, skip reload
    if (new_font == m_current_font && new_wide == m_current_font_wide) {
        LOG_DEBUG("Font reload skipped: requested font matches current");
        m_font_reload_pending = false;
        return;
    }

    // Store the pending font for later execution (between frames)
    m_pending_font = new_font;
    m_pending_font_wide = new_wide;
}

void NvimWidget::process_pending_font_reload() {
    if (!m_font_reload_pending) {
        return;
    }

    // Compute the pending font from the raw requested strings.
    // This must happen here (in on_update, between frames) because
    // _redraw_option_set() may have set the flag during libuv processing
    // in the same on_update phase, before _check_font_reload_needed()
    // has had a chance to run in render().
    m_pending_font = _parse_guifont(m_requested_font);
    m_pending_font_wide = _parse_guifont(m_requested_font_wide);

    if (m_pending_font.family.empty()) {
        m_pending_font = m_current_font;
        if (m_pending_font.family.empty()) {
            m_pending_font.family =
                ImApp::FontManager::get_default_font_family();
            m_pending_font.size_pt = 14.0f;
        }
    }

    // Skip if nothing changed
    if (m_pending_font == m_current_font &&
        m_pending_font_wide == m_current_font_wide) {
        LOG_DEBUG("Font reload skipped: requested font matches current");
        m_font_reload_pending = false;
        return;
    }

    _execute_font_reload();
}

void NvimWidget::_execute_font_reload() {
    // Parse the full fallback list from the raw Neovim option strings.
    // Neovim sends comma-separated font names: "Font1:h12,Font2:h12,..."
    std::vector<ParsedFont> regular_entries =
        parse_guifont_entries(m_requested_font);
    std::vector<ParsedFont> wide_entries =
        parse_guifont_entries(m_requested_font_wide);

    // If no explicit guifont set, use the current or platform default.
    if (regular_entries.empty()) {
        regular_entries.push_back(m_current_font);
        if (regular_entries[0].family.empty()) {
            regular_entries[0].family =
                ImApp::FontManager::get_default_font_family();
            regular_entries[0].size_pt = 14.0f;
        }
    }

    LOG_INFO("Reloading fonts: {} regular candidate(s), {} wide candidate(s)",
             regular_entries.size(), wide_entries.size());

    // Save current font in case we need to revert
    ParsedFont prev_font = m_current_font;
    ParsedFont prev_wide = m_current_font_wide;

    // Try each regular-font entry in fallback order.
    bool loaded = false;
    for (const auto& entry : regular_entries) {
        // Use the first wide entry (or none) for this attempt.
        ParsedFont wide_entry;
        if (!wide_entries.empty()) {
            wide_entry = wide_entries[0];
        }

        LOG_DEBUG("Trying font: '{}' ({}pt, b={}, i={})", entry.family,
                  entry.size_pt, entry.bold, entry.italic);

        loaded = ImApp::FontManager::load_font_with_wide(
            entry.family, entry.size_pt, entry.bold, entry.italic,
            wide_entry.family, wide_entry.size_pt);

        if (loaded) {
            // Success
            m_current_font = entry;
            m_current_font_wide = wide_entry;
            break;
        }

        LOG_DEBUG("Font '{}' not found, trying next fallback...", entry.family);
    }

    if (!loaded) {
        LOG_WARN("No font in the fallback list could be loaded, reverting "
                 "to previous font");
        // If the previous font was never explicitly set (first load),
        // fall back to the platform default.
        if (prev_font.family.empty()) {
            prev_font.family = ImApp::FontManager::get_default_font_family();
            prev_font.size_pt = 14.0f;
        }
        // Revert to the previous font
        ImApp::FontManager::load_font_with_wide(
            prev_font.family, prev_font.size_pt, prev_font.bold,
            prev_font.italic, prev_wide.family, prev_wide.size_pt);
        // Keep the previous state
    }

    // Recalculate grid dimensions for the new font metrics
    resize(m_state.col, m_state.row);

    m_font_reload_pending = false;
}

void NvimWidget::_handle_nvim_resize() {
    ImVec2 content_size = ImGui::GetContentRegionAvail();
    float char_width = ImGui::GetFontBaked()->GetCharAdvance('M');
    float line_height =
        ImGui::GetTextLineHeight() + static_cast<float>(m_linespace);

    uint32_t new_cols =
        std::max(1u, static_cast<uint32_t>(content_size.x / char_width));
    uint32_t total_rows =
        std::max(1u, static_cast<uint32_t>(content_size.y / line_height));

    // Reserve bottom rows for cmdline / cmdline-block / message overlays.
    // Neovim gets a smaller grid so it places the statusline above the
    // reserved area, preventing overlap with the overlays.
    uint32_t overlay = _overlay_rows();
    uint32_t grid_rows =
        (total_rows > overlay) ? total_rows - overlay : total_rows;

    if (new_cols != m_state.col || grid_rows != m_state.row) {
        LOG_TRACE("Resizing nvim widget: {}x{} (display {}x{})", new_cols,
                  grid_rows, new_cols, total_rows);
        m_display_rows = total_rows;
        resize(new_cols, grid_rows);
    } else if (total_rows != m_display_rows) {
        m_display_rows = total_rows;
    }
}

void NvimWidget::_handle_keyboard_input() {
    if (!ImGui::IsWindowFocused()) {
        _flush_pending_input();
        return;
    }

    // In message history mode, intercept navigation keys before
    // forwarding to Neovim.
    if (m_msg_history_visible) {
        ImGuiIO& io = ImGui::GetIO();
        bool dismissed = false;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) ||
            ImGui::IsKeyPressed(ImGuiKey_Q)) {
            dismissed = true;
        } else if (ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                   ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
            dismissed = true;
        } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) ||
                   ImGui::IsKeyPressed(ImGuiKey_J)) {
            // Scroll toward newer entries (decrease scroll offset)
            if (m_msg_history_scroll > 0) {
                m_msg_history_scroll--;
            }
        } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) ||
                   ImGui::IsKeyPressed(ImGuiKey_K)) {
            // Scroll toward older entries (increase scroll offset)
            int max_scroll =
                std::max(0, static_cast<int>(m_msg_history_entries.size()) -
                                static_cast<int>(_message_area_rows()));
            if (m_msg_history_scroll < max_scroll) {
                m_msg_history_scroll++;
            }
        }

        if (dismissed) {
            m_msg_history_visible = false;
        }
        return;
    }

    if (!m_nvim_attached) {
        _flush_pending_input();
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    std::string keys = collect_input(io);
    if (keys.empty()) {
        _flush_pending_input();
        return;
    }

    m_pending_input += keys;
    if (m_pending_input.size() >= 64) {
        _flush_pending_input();
    }
}

void NvimWidget::_update_ime_position() {
    if (!m_nvim_attached) {
        return;
    }

    float char_width = ImGui::GetFontBaked()->GetCharAdvance('M');
    float line_height = ImGui::GetTextLineHeight();
    float eff_line_h = line_height + static_cast<float>(m_linespace);
    ImVec2 grid_pos = ImGui::GetCursorScreenPos();

    ImVec2 cursor_screen_pos;
    if (m_cmdline_visible) {
        // Place IME at the cmdline cursor position (absolute bottom row).
        auto it = m_grids.find(m_current_grid);
        float cmdline_y =
            grid_pos.y +
            (static_cast<float>(it != m_grids.end() ? it->second.height
                                                    : m_state.row) -
             1.0f) *
                eff_line_h;
        cursor_screen_pos = ImVec2(grid_pos.x + m_cmdline_pos * char_width,
                                   cmdline_y + eff_line_h);
    } else {
        cursor_screen_pos =
            ImVec2(grid_pos.x + m_state.cursor_x * char_width,
                   grid_pos.y + (m_state.cursor_y + 1) * eff_line_h);
    }

    ImGuiContext& g = *GImGui;
    g.PlatformImeData.InputPos = cursor_screen_pos;
    g.PlatformImeData.InputLineHeight = eff_line_h;
}

void NvimWidget::_flush_pending_input() {
    if (m_pending_input.empty()) {
        return;
    }
    auto req = start_nvim_request("nvim_input", 1, nullptr, nullptr);
    if (req) {
        req->arg_str(m_pending_input.size());
        req->arg_str_body(m_pending_input.data(), m_pending_input.size());
    }
    m_pending_input.clear();
}

void NvimWidget::_handle_mouse_input() {
    if (!m_mouse_enabled || !ImGui::IsWindowFocused() || !m_nvim_attached) {
        return;
    }

    ImVec2 grid_pos = ImGui::GetCursorScreenPos();
    float char_width = ImGui::GetFontBaked()->GetCharAdvance('M');
    float line_height = ImGui::GetTextLineHeight();
    float eff_line_h = line_height + static_cast<float>(m_linespace);

    ImGuiIO& io = ImGui::GetIO();

    // Find which visible grid the mouse is over (reverse compositing order —
    // check topmost grids first)
    uint32_t target_grid = m_current_grid;
    const Grid* grid_ptr = nullptr;
    float origin_x = grid_pos.x;
    float origin_y = grid_pos.y;

    if (m_multigrid_enabled) {
        auto layers = _collect_visible_layers();

        // Iterate in reverse (topmost first)
        for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
            uint32_t grid_id = *it;
            auto grid_it = m_grids.find(grid_id);
            if (grid_it == m_grids.end() || !grid_it->second.visible) {
                continue;
            }

            // Check mouse_enabled for floating windows
            auto win_it = m_windows.find(grid_id);
            if (win_it != m_windows.end() && win_it->second.floating &&
                !win_it->second.mouse_enabled) {
                continue;
            }

            // Compute grid origin
            float gx = grid_pos.x;
            float gy = grid_pos.y;
            if (win_it != m_windows.end()) {
                if (win_it->second.floating) {
                    gx = grid_pos.x + win_it->second.start_col * char_width;
                    gy = grid_pos.y + win_it->second.start_row * eff_line_h;
                } else if (grid_id != 1) {
                    gx = grid_pos.x + win_it->second.start_col * char_width;
                    gy = grid_pos.y + win_it->second.start_row * eff_line_h;
                }
            }

            float grid_right = gx + grid_it->second.width * char_width;
            float grid_bot = gy + grid_it->second.height * eff_line_h;

            if (io.MousePos.x >= gx && io.MousePos.x < grid_right &&
                io.MousePos.y >= gy && io.MousePos.y < grid_bot) {
                target_grid = grid_id;
                grid_ptr = &grid_it->second;
                origin_x = gx;
                origin_y = gy;
                break;
            }
        }
    }

    // Fallback: use current grid
    if (!grid_ptr) {
        auto it = m_grids.find(target_grid);
        if (it == m_grids.end()) {
            return;
        }
        grid_ptr = &it->second;
    }

    const Grid& grid = *grid_ptr;

    // Bail if mouse is outside the grid rect
    if (io.MousePos.x < origin_x || io.MousePos.y < origin_y) {
        return;
    }
    float grid_right = origin_x + grid.width * char_width;
    float grid_bot = origin_y + grid.height * eff_line_h;
    if (io.MousePos.x >= grid_right || io.MousePos.y >= grid_bot) {
        return;
    }

    int col = static_cast<int>((io.MousePos.x - origin_x) / char_width);
    int row = static_cast<int>((io.MousePos.y - origin_y) / eff_line_h);

    col = std::clamp(col, 0, static_cast<int>(grid.width) - 1);
    row = std::clamp(row, 0, static_cast<int>(grid.height) - 1);

    std::string mods = modifier_prefix(io);

    // Compute edge transitions
    uint32_t pressed = 0;
    uint32_t released = 0;
    for (int b = 0; b < ImGuiMouseButton_COUNT; b++) {
        bool down = io.MouseDown[b];
        bool was = (m_mouse.was_down & (1u << b)) != 0;
        if (down && !was) {
            pressed |= (1u << b);
        }
        if (!down && was) {
            released |= (1u << b);
        }
    }

    // Press events (rising edge)
    for (int b = 0; b < ImGuiMouseButton_COUNT; b++) {
        if (!(pressed & (1u << b))) {
            continue;
        }
        auto btn = static_cast<ImGuiMouseButton>(b);
        uint8_t cnt =
            static_cast<uint8_t>(((io.MouseClickedCount[b] - 1) % 4) + 1);
        std::string s = mouse_input_string(mods, mouse_button_name(btn, cnt),
                                           "Mouse", col, row);
        auto req = start_nvim_request("nvim_input", 1, nullptr, nullptr);
        if (req) {
            req->arg_str(s.size());
            req->arg_str_body(s.data(), s.size());
        }
        m_mouse.last_drag_cell_x = -1;
        m_mouse.last_drag_cell_y = -1;
    }

    // Release events (falling edge)
    for (int b = 0; b < ImGuiMouseButton_COUNT; b++) {
        if (!(released & (1u << b))) {
            continue;
        }
        auto btn = static_cast<ImGuiMouseButton>(b);
        std::string s = mouse_input_string(mods, mouse_button_name(btn, 0),
                                           "Release", col, row);
        auto req = start_nvim_request("nvim_input", 1, nullptr, nullptr);
        if (req) {
            req->arg_str(s.size());
            req->arg_str_body(s.data(), s.size());
        }
        m_mouse.last_drag_cell_x = -1;
        m_mouse.last_drag_cell_y = -1;
    }

    // Drag events (throttled: only when cell changes)
    for (int b = 0; b < ImGuiMouseButton_COUNT; b++) {
        if (!io.MouseDown[b]) {
            continue;
        }
        auto btn = static_cast<ImGuiMouseButton>(b);
        if (!ImGui::IsMouseDragging(btn, 0.0f)) {
            continue;
        }
        if (col == m_mouse.last_drag_cell_x &&
            row == m_mouse.last_drag_cell_y) {
            break;
        }
        m_mouse.last_drag_cell_x = col;
        m_mouse.last_drag_cell_y = row;
        std::string s = mouse_input_string(mods, mouse_button_name(btn, 0),
                                           "Drag", col, row);
        auto req = start_nvim_request("nvim_input", 1, nullptr, nullptr);
        if (req) {
            req->arg_str(s.size());
            req->arg_str_body(s.data(), s.size());
        }
        break;
    }

    // Scroll events
    if (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f) {
        std::string s =
            convert_scroll(io.MouseWheel, io.MouseWheelH, mods, col, row,
                           m_mouse.scroll_rem_y, m_mouse.scroll_rem_x);
        if (!s.empty()) {
            auto req = start_nvim_request("nvim_input", 1, nullptr, nullptr);
            if (req) {
                req->arg_str(s.size());
                req->arg_str_body(s.data(), s.size());
            }
        }
    }

    // Update was_down for next frame
    m_mouse.was_down = 0;
    for (int b = 0; b < ImGuiMouseButton_COUNT; b++) {
        if (io.MouseDown[b]) {
            m_mouse.was_down |= (1u << b);
        }
    }
}

void NvimWidget::_notify_nvim_resize(uint32_t cols, uint32_t rows) {
    if (!m_nvim_attached) {
        return;
    }

    auto req = start_nvim_request("nvim_ui_try_resize", 2, nullptr, nullptr);
    req->arg_uint32(cols);
    req->arg_uint32(rows);
}

/**
 * Send error response for the given request message
 */
void NvimWidget::_send_nvim_error(const msgpack::object& req,
                                  const std::string& msg) {
    if (req.via.array.ptr[0].as<uint64_t>() != 0) {
        LOG_ERROR("Errors can only be sent as replies to Requests(type=0)");
    }
    uint32_t msgid = req.via.array.ptr[1].as<uint32_t>();
    _send_nvim_error(msgid, msg);
}

void NvimWidget::_send_nvim_error(uint32_t msgid, const std::string& msg) {
    // [type(1), msgid, error, result(nil)]
    std::stringstream buffer;
    msgpack::packer<std::stringstream> packer(buffer);
    packer.pack_array(4);
    packer.pack_int(1); // 1 = Response
    packer.pack_uint32(msgid);
    packer.pack_bin(msg.size());
    packer.pack_bin_body(msg.data(), msg.size());
    packer.pack_nil();

    uv_write_t* write_req = new uv_write_t();
    write_req->data = this;

    std::string buffer_string = buffer.str();
    uv_buf_t uv_buf = uv_buf_init(buffer_string.data(), buffer_string.size());

    int r = uv_write(write_req, reinterpret_cast<uv_stream_t*>(&m_in_pipe),
                     &uv_buf, 1, _uv_write_cb);
    if (r) {
        LOG_ERROR("uv_write failed when sending error msgid {}: {}", msgid,
                  uv_strerror(r));
        delete write_req;
    }
}

std::size_t
NvimWidget::_handle_nvim_rpc(const std::vector<char>& msgpack_data) {
    if (msgpack_data.empty())
        return 0;
    std::size_t len = msgpack_data.size();
    std::size_t off = 0;
    while (off < len) {
        msgpack::unpacked result;
        std::size_t prev_off = off;
        try {
            msgpack::unpack(result, msgpack_data.data(), len, off);
            LOG_TRACE("Parsed a complete nvim msgpack package (offset: {})",
                      off);
            msgpack::object obj(result.get());
            _dispatch(obj);
        } catch (const msgpack::insufficient_bytes&) { // Incomplete data - stop
                                                       // and wait for more
            // msgpack::unpack may partially advance `off` before throwing.
            // Restore the previous offset to avoid losing data.
            off = prev_off;
            LOG_TRACE("Incomplete msgpack data, waiting for more (offset: {})",
                      off);
            break;
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to parse nvim msgpack data: {}", e.what());
            return len; // Discard all data on parse error
        }
    }
    return off;
}

void NvimWidget::_dispatch(msgpack::object& req) {
    if (req.type != msgpack::type::ARRAY) {
        LOG_ERROR("Invalid nvim RPC: not an array.");
        return;
    }
    if (req.via.array.size < 3 || req.via.array.size > 4) {
        LOG_ERROR("Invalid nvim RPC: message length MUST be 3 or 4.");
        return;
    }
    if (req.via.array.ptr[0].type != msgpack::type::POSITIVE_INTEGER) {
        LOG_ERROR("Invalid nvim RPC: message type MUST be a positive "
                  "integer.");
        return;
    }
    uint64_t type = req.via.array.ptr[0].as<uint64_t>();
    switch (type) {
    case 0:
        if (req.via.array.ptr[1].type != msgpack::type::POSITIVE_INTEGER) {
            LOG_ERROR("Invalid nvim request: message id MUST be a positive "
                      "integer.");
            _send_nvim_error(req, "Msg Id must be a positive integer.");
            return;
        }
        if (req.via.array.ptr[2].type != msgpack::type::STR) {
            LOG_ERROR("Invalid nvim request: method MUST be a string.");
            _send_nvim_error(req, "Method must be a string.");
            return;
        }
        if (req.via.array.ptr[3].type != msgpack::type::ARRAY) {
            LOG_ERROR("Invalid nvim request: arguments MUST be an array.");
            _send_nvim_error(req, "Arguments must be a array.");
            return;
        }
        _dispatch_request(req);
        break;
    case 1:
        if (req.via.array.ptr[1].type != msgpack::type::POSITIVE_INTEGER) {
            LOG_ERROR("Invalid nvim response: message id MUST be a positive "
                      "integer.");
            return;
        }
        _dispatch_response(req);
        break;
    case 2:
        if (req.via.array.ptr[1].type != msgpack::type::STR) {
            LOG_ERROR("Invalid nvim notification: event MUST be a string.");
            _send_nvim_error(req, "event must be a string.");
            return;
        }
        if (req.via.array.ptr[2].type != msgpack::type::ARRAY) {
            LOG_ERROR("Invalid nvim notification: arguments MUST be an array.");
            _send_nvim_error(req, "Arguments must be a array.");
            return;
        }
        _dispatch_notification(req);
        break;
    default:
        LOG_ERROR("Unsupported nvim message type: {}", type);
    }
}

void NvimWidget::_dispatch_request(msgpack::object& req) {
    /*
     * nvim msgpack requests are
     * [type(0), msgid(uint), method(str), args(object_array)]
     * See: `serialize_request` in 'nvim/msgpack_rpc/channel.c'
     */
    uint32_t msgid = req.via.array.ptr[1].as<uint32_t>();
    std::string method = req.via.array.ptr[2].as<std::string>();
    _handle_nvim_request(msgid, method, req.via.array.ptr[3].via.array);
}

void NvimWidget::_dispatch_response(msgpack::object& resp) {
    /*
     * If there's no error, nvim msgpack responses are
     * [type(1), msgid(uint), nil, return value(object)]
     * otherwise, nvim msgpack responses are
     * [type(1), msgid(uint), [error_type(int), error_msg(str), nil]
     * See: `serialize_response` in 'nvim/msgpack_rpc/channel.c'
     */

    uint64_t msgid = resp.via.array.ptr[1].as<uint64_t>();
    auto req_it = m_requests.find(msgid);
    if (req_it == m_requests.end()) {
        LOG_WARN("Received response for unknown message id: {}", msgid);
        return;
    }
    auto& request = req_it->second;
    if (resp.via.array.ptr[2].type != msgpack::type::NIL) {
        auto& err = resp.via.array.ptr[2];
        if (err.via.array.size >= 2 &&
            (err.via.array.ptr[0].type == msgpack::type::POSITIVE_INTEGER |
             err.via.array.ptr[0].type == msgpack::type::NEGATIVE_INTEGER) &&
            err.via.array.ptr[1].type == msgpack::type::STR &&
            request->m_on_error) {
            int32_t error_type = err.via.array.ptr[0].as<int32_t>();
            std::string error_msg = err.via.array.ptr[1].as<std::string>();
            request->m_on_error(error_type, error_msg);
        }
    } else {
        if (request->m_on_result) {
            request->m_on_result(resp.via.array.ptr[3]);
        }
    }
    m_requests.erase(req_it);
}

void NvimWidget::_dispatch_notification(msgpack::object& nt) {
    /*
     * nvim msgpack notifications are
     * [type(0), method(str), args(object_array)]
     * See: `serialize_request` in 'nvim/msgpack_rpc/channel.c'
     */
    std::string event = nt.via.array.ptr[1].as<std::string>();
    _handle_nvim_notification(event, nt.via.array.ptr[2].via.array);
}

void NvimWidget::_on_nvim_exit(uv_process_t* nvim_proc, int64_t exit_status,
                               int term_signal) {
    auto* self = static_cast<NvimWidget*>(nvim_proc->data);
    LOG_DEBUG("nvim exited with status {}, signal {}", exit_status,
              term_signal);
    uv_read_stop(reinterpret_cast<uv_stream_t*>(&self->m_out_pipe));
    uv_close(reinterpret_cast<uv_handle_t*>(&self->m_in_pipe), nullptr);
    self->m_in_pipe.data = nullptr;
    uv_close(reinterpret_cast<uv_handle_t*>(&self->m_out_pipe), nullptr);
    self->m_out_pipe.data = nullptr;
    // Mark as exited so the destructor knows not to kill it again.
    // Do NOT call uv_close on the process handle — doing so from within
    // the exit callback triggers a libuv assertion on Windows
    // (exit_cb_pending is still set). The destructor handles cleanup.
    self->m_nvim_exited = true;
    self->m_nvim_msgid.store(1);
}

void NvimWidget::_uv_alloc_cb(uv_handle_t* handle, size_t suggested,
                              uv_buf_t* buf) {
    *buf = uv_buf_init(static_cast<char*>(malloc(suggested)), suggested);
}

void NvimWidget::_uv_read_cb(uv_stream_t* stream, ssize_t nread,
                             const uv_buf_t* buf) {
    auto* self = static_cast<NvimWidget*>(stream->data);
    if (nread < 0) {
        if (nread != UV_EOF) {
            LOG_ERROR("Read nvim data failed: {}",
                      uv_strerror(static_cast<int>(nread)));
        } else {
            LOG_DEBUG("Nvim stdout pipe closed (EOF)");
        }
        uv_read_stop(stream);
        if (buf->base) {
            free(buf->base);
        }
        return;
    }
    if (nread == 0) {
        // cnt == 0 means libuv asked for a buffer and decided it wasn't needed:
        // http://docs.libuv.org/en/latest/stream.html#c.uv_read_start.
        if (buf->base) {
            free(buf->base);
        }
        return;
    }
    if (buf->base) {
        self->m_nvim_resp_buf.insert(self->m_nvim_resp_buf.end(), buf->base,
                                     buf->base + nread);
        free(buf->base);
        std::size_t consumed = self->_handle_nvim_rpc(self->m_nvim_resp_buf);
        if (consumed >= self->m_nvim_resp_buf.size()) {
            self->m_nvim_resp_buf.clear();
        } else {
            self->m_nvim_resp_buf.erase(self->m_nvim_resp_buf.begin(),
                                        self->m_nvim_resp_buf.begin() +
                                            consumed);
        }
    }
}

void NvimWidget::_uv_write_cb(uv_write_t* req, int status) {
    auto* request = static_cast<NvimRequest*>(req->data);
    if (status) {
        LOG_ERROR("Nvim RPC write failed: {}", uv_strerror(status));
        if (auto self = request->m_nvim.lock()) {
            if (self->m_requests.find(request->msgid) !=
                self->m_requests.end()) {
                self->m_requests.erase(request->msgid);
            }
        }
    }
    delete req;
}

NvimRequest::NvimRequest(
    uint32_t msgid, const std::string& method, uint8_t param_count,
    std::weak_ptr<NvimWidget> nvim,
    std::function<void(msgpack::object&)>&& on_result,
    std::function<void(int32_t, const std::string&)>&& on_error)
    : msgid(msgid), m_method(method), m_arg_count(0),
      m_param_count(param_count), m_nvim(nvim),
      m_on_result(std::move(on_result)), m_on_error(std::move(on_error)) {
    /*
     * nvim msgpack requests are
     * [type(0), msgid(uint), method(str), args(object_array)]
     * See: `serialize_request` in 'nvim/msgpack_rpc/channel.c'
     */
    m_buffer.str(std::string());
    m_packer = std::make_unique<msgpack::packer<std::stringstream>>(m_buffer);
    m_packer->pack_array(4);
    m_packer->pack_int(0); /* Request(0) */
    m_packer->pack_uint32(msgid);
    m_packer->pack_bin(method.size());
    m_packer->pack_bin_body(method.data(), method.size());
    m_packer->pack_array(param_count);
    if (param_count == m_arg_count) {
        _send();
    }
}

#define ERROR_TYPE_ARG_COUNT_MISMATCH (-32602)
#define ERROR_TYPE_UNCLOSED_CONTAINER (-32601)
#define ERROR_TYPE_NVIM_DESTROYED (-32600)
#define ERROR_TYPE_LIBUV_WRITE_FAILED (-32599)

void NvimRequest::_send() {
    if (!m_packer) {
        LOG_ERROR("Nvim RPC '{}'[msgid: {}] packer is null, send abort.",
                  m_method, msgid);
        return;
    }
    if (m_arg_count != m_param_count) {
        LOG_ERROR("Trying to send nvim RPC '{}'[msgid: {}], but the number of "
                  "parameters does not match the declaration!!",
                  m_method, msgid);
        if (m_on_error) {
            m_on_error(ERROR_TYPE_ARG_COUNT_MISMATCH,
                       "Invalid arguments: count mismatch with parameters.");
        }
        m_packer.reset();
        return;
    }
    if (!m_container_stack.empty()) {
        LOG_ERROR("Trying to send nvim RPC '{}'[msgid: {}], but the "
                  "container stack is not empty, something went wrong!!",
                  m_method, msgid);
        if (m_on_error) {
            m_on_error(ERROR_TYPE_UNCLOSED_CONTAINER,
                       "Invalid arguments: found unclosed container.");
        }
        m_packer.reset();
        return;
    }
    if (auto nvim = m_nvim.lock()) {
        std::string send_buf = m_buffer.str();
        LOG_TRACE("Send nvim RPC '{}'[msgid:{}] ({} bytes)", m_method, msgid,
                  send_buf.size());

        // Will be deleted in NvimWidget::_nv_write_cb.
        uv_write_t* write_req = new uv_write_t();
        write_req->data = this;

        uv_buf_t uv_buf =
            uv_buf_init(const_cast<char*>(send_buf.data()), send_buf.size());

        int r = uv_write(write_req,
                         reinterpret_cast<uv_stream_t*>(&nvim->m_in_pipe),
                         &uv_buf, 1, NvimWidget::_uv_write_cb);
        if (r) {
            LOG_ERROR("uv_write failed for '{}'[msgid: {}]: {}", m_method,
                      msgid, uv_strerror(r));
            delete write_req;
            if (m_on_error) {
                m_on_error(ERROR_TYPE_LIBUV_WRITE_FAILED,
                           "Failed to write into the stdin of nvim.");
            }
        }
    } else {
        LOG_ERROR("Faield to send Nvim RPC '{}'[msgid: {}], NvimWidget has "
                  "been destroyed.",
                  m_method, msgid);
        if (m_on_error) {
            m_on_error(ERROR_TYPE_NVIM_DESTROYED,
                       "NvimWidget has been destroyed.");
        }
    }
    /* The packer is no longer needed. */
    m_packer.reset();
}

#define TRY_TO_SEND()                                                          \
    do {                                                                       \
        while (!m_container_stack.empty()) {                                   \
            m_container_stack.top() -= 1;                                      \
            if (m_container_stack.top() == 0) {                                \
                m_container_stack.pop();                                       \
            } else {                                                           \
                break;                                                         \
            }                                                                  \
        }                                                                      \
        if (m_container_stack.empty()) {                                       \
            m_arg_count++;                                                     \
        }                                                                      \
        if (m_arg_count == m_param_count && m_container_stack.empty()) {       \
            _send();                                                           \
        }                                                                      \
    } while (0)

void NvimRequest::arg_uint8(uint8_t d) {
    if (!m_packer) {
        return;
    }
    m_packer->pack_uint8(d);
    TRY_TO_SEND();
}

void NvimRequest::arg_uint16(uint16_t d) {
    if (!m_packer)
        return;
    m_packer->pack_uint16(d);
    TRY_TO_SEND();
}

void NvimRequest::arg_uint32(uint32_t d) {
    if (!m_packer)
        return;
    m_packer->pack_uint32(d);
    TRY_TO_SEND();
}

void NvimRequest::arg_uint64(uint64_t d) {
    if (!m_packer)
        return;
    m_packer->pack_uint64(d);
    TRY_TO_SEND();
}

void NvimRequest::arg_int8(int8_t d) {
    if (!m_packer)
        return;
    m_packer->pack_int8(d);
    TRY_TO_SEND();
}

void NvimRequest::arg_int16(int16_t d) {
    if (!m_packer)
        return;
    m_packer->pack_int16(d);
    TRY_TO_SEND();
}

void NvimRequest::arg_int32(int32_t d) {
    if (!m_packer)
        return;
    m_packer->pack_int32(d);
    TRY_TO_SEND();
}

void NvimRequest::arg_int64(int64_t d) {
    if (!m_packer)
        return;
    m_packer->pack_int64(d);
    TRY_TO_SEND();
}

void NvimRequest::arg_fix_uint8(uint8_t d) {
    if (!m_packer)
        return;
    m_packer->pack_fix_uint8(d);
    TRY_TO_SEND();
}

void NvimRequest::arg_fix_uint16(uint16_t d) {
    if (!m_packer)
        return;
    m_packer->pack_fix_uint16(d);
    TRY_TO_SEND();
}

void NvimRequest::arg_fix_uint32(uint32_t d) {
    if (!m_packer)
        return;
    m_packer->pack_fix_uint32(d);
    TRY_TO_SEND();
}

void NvimRequest::arg_fix_uint64(uint64_t d) {
    if (!m_packer)
        return;
    m_packer->pack_fix_uint64(d);
    TRY_TO_SEND();
}

void NvimRequest::arg_fix_int8(int8_t d) {
    if (!m_packer)
        return;
    m_packer->pack_fix_int8(d);
    TRY_TO_SEND();
}

void NvimRequest::arg_fix_int16(int16_t d) {
    if (!m_packer)
        return;
    m_packer->pack_fix_int16(d);
    TRY_TO_SEND();
}

void NvimRequest::arg_fix_int32(int32_t d) {
    if (!m_packer)
        return;
    m_packer->pack_fix_int32(d);
    TRY_TO_SEND();
}

void NvimRequest::arg_fix_int64(int64_t d) {
    if (!m_packer)
        return;
    m_packer->pack_fix_int64(d);
    TRY_TO_SEND();
}

void NvimRequest::arg_char(char d) {
    if (!m_packer)
        return;
    m_packer->pack_char(d);
    TRY_TO_SEND();
}

void NvimRequest::arg_signed_char(signed char d) {
    if (!m_packer)
        return;
    m_packer->pack_signed_char(d);
    TRY_TO_SEND();
}

void NvimRequest::arg_short(int16_t d) {
    if (!m_packer)
        return;
    m_packer->pack_short(d);
    TRY_TO_SEND();
}

void NvimRequest::arg_int(int d) {
    if (!m_packer)
        return;
    m_packer->pack_int(d);
    TRY_TO_SEND();
}

void NvimRequest::arg_long(int32_t d) {
    if (!m_packer)
        return;
    m_packer->pack_long(d);
    TRY_TO_SEND();
}

void NvimRequest::arg_long_long(int64_t d) {
    if (!m_packer)
        return;
    m_packer->pack_long_long(d);
    TRY_TO_SEND();
}

void NvimRequest::arg_unsigned_char(uint8_t d) {
    if (!m_packer)
        return;
    m_packer->pack_unsigned_char(d);
    TRY_TO_SEND();
}

void NvimRequest::arg_unsigned_short(uint16_t d) {
    if (!m_packer)
        return;
    m_packer->pack_unsigned_short(d);
    TRY_TO_SEND();
}

void NvimRequest::arg_unsigned_int(uint32_t d) {
    if (!m_packer)
        return;
    m_packer->pack_unsigned_int(d);
    TRY_TO_SEND();
}

void NvimRequest::arg_unsigned_long(uint32_t d) {
    if (!m_packer)
        return;
    m_packer->pack_unsigned_long(d);
    TRY_TO_SEND();
}

void NvimRequest::arg_unsigned_long_long(uint64_t d) {
    if (!m_packer)
        return;
    m_packer->pack_unsigned_long_long(d);
    TRY_TO_SEND();
}

void NvimRequest::arg_float(float d) {
    if (!m_packer)
        return;
    m_packer->pack_float(d);
    TRY_TO_SEND();
}

void NvimRequest::arg_double(double d) {
    if (!m_packer)
        return;
    m_packer->pack_double(d);
    TRY_TO_SEND();
}

void NvimRequest::arg_nil() {
    if (!m_packer)
        return;
    m_packer->pack_nil();
    TRY_TO_SEND();
}

void NvimRequest::arg_true() {
    if (!m_packer)
        return;
    m_packer->pack_true();
    TRY_TO_SEND();
}

void NvimRequest::arg_false() {
    if (!m_packer)
        return;
    m_packer->pack_false();
    TRY_TO_SEND();
}

void NvimRequest::arg_array(size_t n) {
    if (!m_packer) {
        return;
    }
    m_packer->pack_array(n);
    if (n > 0) {
        m_container_stack.push(n);
    } else {
        TRY_TO_SEND();
    }
}

void NvimRequest::arg_map(size_t n) {
    if (!m_packer) {
        return;
    }
    m_packer->pack_map(n);
    if (n > 0) {
        m_container_stack.push(n * 2);
    } else {
        TRY_TO_SEND();
    }
}

void NvimRequest::arg_bin(size_t l) {
    if (!m_packer) {
        return;
    }
    m_packer->pack_bin(l);
    m_container_stack.push(1);
}

void NvimRequest::arg_bin_body(const char* b, size_t l) {
    if (!m_packer) {
        return;
    }
    m_packer->pack_bin_body(b, l);
    TRY_TO_SEND();
}

void NvimRequest::arg_str(size_t l) {
    if (!m_packer)
        return;
    m_packer->pack_str(l);
    m_container_stack.push(1);
}

void NvimRequest::arg_str_body(const char* b, size_t l) {
    if (!m_packer)
        return;
    m_packer->pack_str_body(b, l);
    TRY_TO_SEND();
}

void NvimRequest::arg_ext(size_t l, int8_t type) {
    if (!m_packer)
        return;
    m_packer->pack_ext(l, type);
    m_container_stack.push(1);
}

void NvimRequest::arg_ext_body(const char* b, size_t l) {
    if (!m_packer)
        return;
    m_packer->pack_ext_body(b, l);
    TRY_TO_SEND();
}

#undef TRY_TO_SEND

// --- Buffer modification tracking ---

ImGuiWindowFlags NvimWidget::get_additional_window_flags() const {
    if (m_buffer_modified) {
        return ImGuiWindowFlags_UnsavedDocument;
    }
    return 0;
}

void NvimWidget::on_close_attempted() {
    m_save_dialog_action = SaveDialogAction::Close;
    _show_save_modal();
}

void NvimWidget::_query_buffer_modified() {
    // Query Neovim for the current buffer's modified flag.
    // Uses nvim_get_option_value("modified", {}) — non-deprecated API
    // that queries the buffer-local option on the current buffer.
    if (!m_nvim_attached) {
        return;
    }

    auto self = shared_from_this();
    auto req = start_nvim_request(
        "nvim_get_option_value", 2,
        [self](msgpack::object& opt_result) {
            bool modified = false;
            if (opt_result.type == msgpack::type::BOOLEAN) {
                modified = opt_result.as<bool>();
            } else if (opt_result.type == msgpack::type::POSITIVE_INTEGER) {
                modified = (opt_result.as<uint32_t>() != 0);
            }
            self->_on_modified_check_result(modified);
        },
        nullptr);

    if (req) {
        const std::string key{"modified"};
        req->arg_str(key.size());
        req->arg_str_body(key.data(), key.size());
        req->arg_map(0); // empty opts dict — queries current buffer
    }
}

void NvimWidget::_on_modified_check_result(bool modified) {
    m_buffer_modified = modified;
}

// --- Save dialog ---

void NvimWidget::_show_save_modal() {
    // Only open the popup once
    if (!ImGui::IsPopupOpen("##SaveModified")) {
        ImGui::OpenPopup("##SaveModified");
        m_show_save_dialog = true;
    }
}

void NvimWidget::_handle_save_decision(bool save, bool discard) {
    if (save) {
        // Send :w to Neovim, then proceed after save completes
        auto self = shared_from_this();
        auto action = m_save_dialog_action;
        auto pending_path = m_pending_file_path;
        auto req = start_nvim_request(
            "nvim_command", 1,
            [self, action, pending_path](msgpack::object&) {
                self->m_buffer_modified = false;
                self->m_needs_modified_check = true;
                // Proceed with the pending action
                if (action == SaveDialogAction::Close) {
                    self->set_visible(false);
                } else if (action == SaveDialogAction::OpenFile &&
                           !pending_path.empty()) {
                    self->_do_open_file(pending_path);
                }
            },
            [self](int32_t error_code, const std::string& error_msg) {
                LOG_ERROR("Failed to save file: {} - {}", error_code,
                          error_msg);
                // Still dismiss dialog on error to avoid getting stuck
            });
        if (req) {
            const std::string cmd{"write"};
            req->arg_str(cmd.size());
            req->arg_str_body(cmd.data(), cmd.size());
        }
    } else if (discard) {
        // Discard changes: tell Neovim to force-delete the buffer,
        // then proceed with close or open.
        m_buffer_modified = false;
        auto self = shared_from_this();
        auto action = m_save_dialog_action;
        auto pending_path = m_pending_file_path;
        auto req = start_nvim_request(
            "nvim_command", 1,
            [self, action, pending_path](msgpack::object&) {
                self->m_needs_modified_check = true;
                if (action == SaveDialogAction::Close) {
                    self->set_visible(false);
                } else if (action == SaveDialogAction::OpenFile &&
                           !pending_path.empty()) {
                    self->_do_open_file(pending_path, true);
                }
            },
            [](int32_t error_code, const std::string& error_msg) {
                LOG_ERROR("Failed to discard changes: {} - {}", error_code,
                          error_msg);
            });
        if (req) {
            const std::string cmd{"bdelete!"};
            req->arg_str(cmd.size());
            req->arg_str_body(cmd.data(), cmd.size());
        }
    }
    // Cancel: do nothing, dismiss dialog

    m_show_save_dialog = false;
    m_pending_file_path.clear();
    ImGui::CloseCurrentPopup();
}

void NvimWidget::_render_save_modal() {
    // ImGui modal pattern: OpenPopup must be called every frame before
    // BeginPopupModal
    if (!ImGui::IsPopupOpen("##SaveModified")) {
        ImGui::OpenPopup("##SaveModified");
    }

    // Center the modal on screen
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("##SaveModified", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        // Determine the display filename
        std::string display_name = m_window_title;
        if (display_name.empty() || display_name == "nvim (no file)") {
            display_name = "untitled";
        }

        ImGui::Text("Do you want to save changes to \"%s\"?",
                    display_name.c_str());
        ImGui::Spacing();

        float button_width = ImGui::GetFontSize() * 7.0f;

        if (ImGui::Button("Save", ImVec2(button_width, 0))) {
            _handle_save_decision(true, false);
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(button_width, 0))) {
            _handle_save_decision(false, true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(button_width, 0))) {
            _handle_save_decision(false, false);
        }

        // Also allow closing with Escape key
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            _handle_save_decision(false, false);
        }

        ImGui::EndPopup();
    }
}

} // namespace ImNeovim
