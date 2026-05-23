#include "im_neovim/gui/nvim_widget.h"
#include "im_neovim/globals.h"
#include "im_neovim/gui/nvim_input.h"
#include "im_neovim/logging.h"
#include <algorithm>
#include <cmath>
#include <im_app/file_system.h>
#include <im_app/font_manager.h>
#include <imgui_internal.h>

namespace ImNeovim {

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

void NvimWidget::open_file(const std::string& path) {
    // Escape the path for use in a vim command
    std::string escaped_path = path;
    // Replace backslashes with forward slashes for vim
    std::replace(escaped_path.begin(), escaped_path.end(), '\\', '/');

    // Build the edit command
    std::string cmd = "edit " + escaped_path;

    // Send the command to nvim via nvim_command
    auto request = start_nvim_request(
        "nvim_command", 1,
        [](msgpack::object&) { LOG_DEBUG("File opened successfully"); },
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

        if (m_popup_visible && !m_popup_items.empty()) {
            _render_popup_menu(draw_list, pos, char_width, line_height);
        }

        _update_ime_position();
    }

    // Always call End() when Begin() was called, per ImGui requirements
    if (!m_is_embedded) {
        ImGui::End();
    }
}

void NvimWidget::_render_grid(ImDrawList* draw_list, const ImVec2& pos,
                              float char_width, float line_height) {
    auto it = m_grids.find(m_current_grid);
    if (it == m_grids.end()) {
        return;
    }

    Grid& grid = it->second;

    float effective_line_height = line_height + static_cast<float>(m_linespace);

    // Draw all cells
    for (uint32_t y = 0; y < grid.height; y++) {
        bool skip_next = false;
        for (uint32_t x = 0; x < grid.width; x++) {
            // Skip the filler cell after a double-width character
            if (skip_next) {
                skip_next = false;
                continue;
            }

            ImVec2 char_pos(pos.x + x * char_width,
                            pos.y + y * effective_line_height);
            bool wide_rendered = TextWidget::render_cell(
                draw_list, grid.cells[y][x], char_pos, char_width, line_height);
            if (wide_rendered) {
                skip_next = true;
            }
        }
    }

    // Draw cursor
    if (ImGui::IsWindowFocused() && m_nvim_attached) {
        // Blink timing logic
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
            ImVec2 cursor_pos(pos.x + m_state.cursor_x * char_width,
                              pos.y + m_state.cursor_y * effective_line_height);

            // Determine if the character under cursor is wide
            bool cursor_is_wide = false;
            ScreenCell cursor_cell;
            if (m_state.cursor_y < grid.height &&
                m_state.cursor_x < grid.width) {
                cursor_cell = grid.cells[m_state.cursor_y][m_state.cursor_x];
                cursor_is_wide = TextWidget::is_wide_char(cursor_cell.chars[0]);
            }

            float cursor_width =
                cursor_is_wide ? char_width * 2.0f : char_width;

            // Cursor rect based on shape and cell percentage
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
                // Full cell, no adjustment needed
                break;
            }

            ImVec4 cursor_color{m_dark_mode ? 0.7f : 0.3f,
                                m_dark_mode ? 0.7f : 0.3f,
                                m_dark_mode ? 0.7f : 0.3f, 0.8f};
            // Dim cursor when busy
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

    // Mode indicator overlay
    if (!m_current_mode_name.empty()) {
        std::string mode_text = "-- " + m_current_mode_name + " --";
        // Capitalize first letter
        if (!mode_text.empty() && mode_text[3] >= 'a' && mode_text[3] <= 'z') {
            mode_text[3] = static_cast<char>(mode_text[3] - 'a' + 'A');
        }

        float text_width = ImGui::CalcTextSize(mode_text.c_str()).x;
        ImVec2 mode_pos(pos.x + (grid.width * char_width - text_width) * 0.5f,
                        pos.y + grid.height * effective_line_height -
                            effective_line_height);

        ImU32 mode_color =
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.7f, 0.7f, 0.7f, 0.6f));
        draw_list->AddText(mode_pos, mode_color, mode_text.c_str());
    }

    // Visual bell flash (200ms semi-transparent overlay)
    if (m_bell_pending) {
        double elapsed = ImGui::GetTime() - m_bell_timestamp;
        if (elapsed < 0.2) {
            float alpha = 0.15f * (1.0f - static_cast<float>(elapsed / 0.2));
            ImVec2 grid_end(pos.x + grid.width * char_width,
                            pos.y + grid.height * effective_line_height);
            draw_list->AddRectFilled(pos, grid_end,
                                     ImGui::ColorConvertFloat4ToU32(
                                         ImVec4(1.0f, 1.0f, 1.0f, alpha)));
        } else {
            m_bell_pending = false;
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
    auto cwd = exe_path.parent_path();
    auto nvim_exe_path = cwd / "nvim" / "bin" /
#if defined(IM_APP_WIN32)
                         "nvim.exe";
#else
                         "nvim";
#endif
    m_nvim_exe = nvim_exe_path.string();
    m_nvim_cwd = cwd.string();

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
    options.flags = 0;
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
                        }
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
    req->arg_map(2);
    {
        std::string rgb_key{"rgb"};
        req->arg_str(rgb_key.size());
        req->arg_str_body(rgb_key.c_str(), rgb_key.size());
        req->arg_true();

        std::string multigrid_key{"ext_multigrid"};
        req->arg_str(multigrid_key.size());
        req->arg_str_body(multigrid_key.c_str(), multigrid_key.size());
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

void NvimWidget::_handle_nvim_request(const uint32_t& msgid, const char* method,
                                      msgpack::object_array& args) {}

void NvimWidget::_handle_nvim_notification(const char* event,
                                           msgpack::object_array& args) {
    if (strcmp(event, "redraw") == 0) {
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

            for (size_t i = 1; i < arg.via.array.size; i++) {
                auto& op_args = arg.via.array.ptr[i];
                if (op_args.type != msgpack::type::ARRAY) {
                    LOG_WARN("Received unexpected redraw operation '{}', "
                             "operation argument is not an array.",
                             operation);
                    continue;
                }
                _handle_nvim_redraw(operation.c_str(), op_args.via.array);
            }
        }
    } else if (strcmp(event, "Gui") == 0 && args.size > 0) {
        std::string gui_event = args.ptr[0].as<std::string>();
        _handle_nvim_gui_event(gui_event.c_str(), args);
    }
}

void NvimWidget::_handle_nvim_redraw(const char* operation,
                                     msgpack::object_array& args) {
    if (strcmp(operation, "resize") == 0) {
        _redraw_resize(args);
    } else if (strcmp(operation, "clear") == 0) {
        _redraw_clear(args);
    } else if (strcmp(operation, "cursor_goto") == 0) {
        _redraw_cursor_goto(args);
    } else if (strcmp(operation, "put") == 0) {
        _redraw_put(args);
    } else if (strcmp(operation, "scroll") == 0) {
        _redraw_scroll(args);
    } else if (strcmp(operation, "set_scroll_region") == 0) {
        _redraw_set_scroll_region(args);
    } else if (strcmp(operation, "highlight_set") == 0) {
        _redraw_highlight_set(args);
    } else if (strcmp(operation, "eol_clear") == 0) {
        _redraw_eol_clear(args);
    } else if (strcmp(operation, "flush") == 0) {
        _redraw_flush(args);
    } else if (strcmp(operation, "option_set") == 0) {
        _redraw_option_set(args);
    } else if (strcmp(operation, "set_title") == 0) {
        _redraw_set_title(args);
    } else if (strcmp(operation, "default_colors_set") == 0) {
        _redraw_default_colors_set(args);
    } else if (strcmp(operation, "mode_info_set") == 0) {
        _redraw_mode_info_set(args);
    } else if (strcmp(operation, "mode_change") == 0) {
        _redraw_mode_change(args);
    } else if (strcmp(operation, "busy_start") == 0) {
        _redraw_busy_start(args);
    } else if (strcmp(operation, "busy_stop") == 0) {
        _redraw_busy_stop(args);
    } else if (strcmp(operation, "mouse_on") == 0) {
        _redraw_mouse_on(args);
    } else if (strcmp(operation, "mouse_off") == 0) {
        _redraw_mouse_off(args);
    } else if (strcmp(operation, "bell") == 0) {
        _redraw_bell(args);
    } else if (strcmp(operation, "suspend") == 0) {
        _redraw_suspend(args);
    } else if (strcmp(operation, "chdir") == 0) {
        _redraw_chdir(args);
    } else if (strcmp(operation, "popupmenu_show") == 0) {
        _redraw_popupmenu_show(args);
    } else if (strcmp(operation, "popupmenu_select") == 0) {
        _redraw_popupmenu_select(args);
    } else if (strcmp(operation, "popupmenu_hide") == 0) {
        _redraw_popupmenu_hide(args);
    } else if (strcmp(operation, "grid_resize") == 0) {
        _redraw_grid_resize(args);
    } else if (strcmp(operation, "grid_line") == 0) {
        _redraw_grid_line(args);
    } else if (strcmp(operation, "grid_clear") == 0) {
        _redraw_grid_clear(args);
    } else if (strcmp(operation, "grid_cursor_goto") == 0) {
        _redraw_grid_cursor_goto(args);
    } else if (strcmp(operation, "grid_scroll") == 0) {
        _redraw_grid_scroll(args);
    } else if (strcmp(operation, "grid_destroy") == 0) {
        _redraw_grid_destroy(args);
    } else if (strcmp(operation, "hl_attr_define") == 0) {
        _redraw_hl_attr_define(args);
    } else if (strcmp(operation, "hl_group_set") == 0) {
        _redraw_hl_group_set(args);
    } else {
        // LOG_DEBUG("Unhandled redraw operation: {}", operation);
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

void NvimWidget::_handle_nvim_gui_event(const char* event,
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
    uint32_t new_rows =
        std::max(1u, static_cast<uint32_t>(content_size.y / line_height));

    if (new_cols != m_state.col || new_rows != m_state.row) {
        LOG_TRACE("Resizing nvim widget.");
        resize(new_cols, new_rows);
    }
}

void NvimWidget::_handle_keyboard_input() {
    if (!ImGui::IsWindowFocused() || !m_nvim_attached) {
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

    ImVec2 cursor_screen_pos(grid_pos.x + m_state.cursor_x * char_width,
                             grid_pos.y + (m_state.cursor_y + 1) * eff_line_h);

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

    auto it = m_grids.find(m_current_grid);
    if (it == m_grids.end()) {
        return;
    }
    const Grid& grid = it->second;

    ImVec2 grid_pos = ImGui::GetCursorScreenPos();
    float char_width = ImGui::GetFontBaked()->GetCharAdvance('M');
    float line_height = ImGui::GetTextLineHeight();
    float eff_line_h = line_height + static_cast<float>(m_linespace);

    ImGuiIO& io = ImGui::GetIO();
    int col = static_cast<int>((io.MousePos.x - grid_pos.x) / char_width);
    int row = static_cast<int>((io.MousePos.y - grid_pos.y) / eff_line_h);

    // Bail if mouse is outside the grid rect
    if (io.MousePos.x < grid_pos.x || io.MousePos.y < grid_pos.y) {
        return;
    }
    float grid_right = grid_pos.x + grid.width * char_width;
    float grid_bot = grid_pos.y + grid.height * eff_line_h;
    if (io.MousePos.x >= grid_right || io.MousePos.y >= grid_bot) {
        return;
    }

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
        try {
            msgpack::unpack(result, msgpack_data.data(), len, off);
            LOG_TRACE("Parsed a complete nvim msgpack package (offset: {})",
                      off);
            msgpack::object obj(result.get());
            _dispatch(obj);
        } catch (const msgpack::insufficient_bytes&) { // Incomplete data - stop
                                                       // and wait for more
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
    _handle_nvim_request(msgid, method.c_str(), req.via.array.ptr[3].via.array);
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
    _handle_nvim_notification(event.c_str(), nt.via.array.ptr[2].via.array);
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
} // namespace ImNeovim
