#include "im_neovim/gui/nvim_widget.h"
#include "im_neovim/globals.h"
#include "im_neovim/logging.h"
#include <algorithm>
#include <cmath>
#include <im_app/file_system.h>

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
        uv_process_kill(&m_nvim_proc, SIGKILL);
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

void NvimWidget::open_file() {}

void NvimWidget::render() {
    if (m_nvim_proc.pid == 0) {
        _spawn_nvim();
    }

    _check_font_size_changed();
    bool window_created = TextWidget::setup_window();

    // Only render content if window is open and not collapsed
    if (window_created && (m_is_embedded || !m_embedded_window_collapsed)) {
        _handle_nvim_resize();

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float char_width = ImGui::GetFontBaked()->GetCharAdvance('M');
        float line_height = ImGui::GetTextLineHeight();

        _render_grid(draw_list, pos, char_width, line_height);
    }

    // Only call End() if Begin() was actually called and succeeded
    if (window_created && !m_is_embedded) {
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

    // Draw all cells
    for (uint32_t y = 0; y < grid.height; y++) {
        for (uint32_t x = 0; x < grid.width; x++) {
            ImVec2 char_pos(pos.x + x * char_width, pos.y + y * line_height);
            TextWidget::render_cell(draw_list, grid.cells[y][x], char_pos,
                                    char_width, line_height);
        }
    }

    // Draw cursor
    if (ImGui::IsWindowFocused() && m_nvim_attached) {
        ImVec2 cursor_pos(pos.x + m_state.cursor_x * char_width,
                          pos.y + m_state.cursor_y * line_height);
        float alpha = (sin(ImGui::GetTime() * 3.14159f) * 0.3f) + 0.5f;

        ScreenCell cursor_cell;
        if (m_state.cursor_y < grid.height && m_state.cursor_x < grid.width) {
            cursor_cell = grid.cells[m_state.cursor_y][m_state.cursor_x];
        }

        // Override cursor color with alpha
        ImVec4 cursor_color{m_dark_mode ? 0.7f : 0.3f,
                            m_dark_mode ? 0.7f : 0.3f,
                            m_dark_mode ? 0.7f : 0.3f, alpha};

        if (cursor_cell.chars[0] != '\0') {
            // Draw cursor background
            draw_list->AddRectFilled(
                cursor_pos,
                ImVec2(cursor_pos.x + char_width, cursor_pos.y + line_height),
                ImGui::ColorConvertFloat4ToU32(cursor_color));

            // Draw the character
            char text[TextWidget::g_utf_size] = {0};
            size_t len = 0;
            for (int i = 0; i < cursor_cell.width && i < 4; i++) {
                len +=
                    TextWidget::utf8_encode(cursor_cell.chars[i], &text[len]);
            }
            draw_list->AddText(cursor_pos,
                               ImGui::ColorConvertFloat4ToU32(cursor_cell.fg),
                               text);
        } else {
            // Just draw cursor
            draw_list->AddRectFilled(
                cursor_pos,
                ImVec2(cursor_pos.x + char_width, cursor_pos.y + line_height),
                ImGui::ColorConvertFloat4ToU32(cursor_color));
        }
    }
}

void NvimWidget::resize(uint32_t cols, uint32_t rows) {
    // Get actual content area size
    ImVec2 content_size = ImGui::GetContentRegionAvail();
    float char_width = ImGui::GetFontBaked()->GetCharAdvance('M');
    float line_height = ImGui::GetTextLineHeight();

    // Calculate new dimensions based on actual font metrics
    uint32_t new_cols =
        std::max(static_cast<uint32_t>(1),
                 static_cast<uint32_t>(content_size.x / char_width));
    uint32_t new_rows =
        std::max(static_cast<uint32_t>(1),
                 static_cast<uint32_t>(content_size.y / line_height));

    // Only resize if dimensions actually changed
    if (new_cols == m_state.col && new_rows == m_state.row) {
        return;
    }

    // Update nvim state
    m_state.row = rows;
    m_state.col = cols;

    // Resize grid
    auto it = m_grids.find(m_current_grid);
    if (it != m_grids.end()) {
        it->second.resize(cols, rows);
    }

    // Ensure cursor stays within bounds
    m_state.cursor_x = std::min(m_state.cursor_x, cols - 1);
    m_state.cursor_y = std::min(m_state.cursor_y, rows - 1);

    LOG_DEBUG("Nvim widget resized to {}x{}", cols, rows);
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
    req->arg_map(1);
    {
        std::string rgb_key{"rgb"};
        req->arg_str(rgb_key.size());
        req->arg_str_body(rgb_key.c_str(), rgb_key.size());
        req->arg_true();
    }
}

void NvimWidget::_set_nvim_attached(bool attached) {
    m_nvim_attached = attached;
}

void NvimWidget::_handle_nvim_request(const uint32_t& msgid, const char* method,
                                      msgpack::object_array& args) {}

void NvimWidget::_handle_nvim_notification(const char* event,
                                           msgpack::object_array& args) {
    if (strcmp(event, "redraw") == 0) {
        LOG_DEBUG("Nvim redraw event.");
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
    } else if (strcmp(operation, "highlight_set") == 0) {
        _redraw_highlight_set(args);
    } else if (strcmp(operation, "flush") == 0) {
        _redraw_flush(args);
    } else if (strcmp(operation, "option_set") == 0) {
        _redraw_option_set(args);
    } else if (strcmp(operation, "set_title") == 0) {
        _redraw_set_title(args);
    } else if (strcmp(operation, "default_colors_set") == 0) {
        _redraw_default_colors_set(args);
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

void NvimWidget::_handle_nvim_gui_event(const char* event,
                                        msgpack::object_array& /*args*/) {}

void NvimWidget::_check_font_size_changed() {
    float current_font_size = ImGui::GetFontBaked()->Size;
    if (current_font_size != m_last_font_size) {
        m_last_font_size = current_font_size;
        resize(m_state.col, m_state.row);
    }
}

void NvimWidget::_handle_nvim_resize() {
    ImVec2 content_size = ImGui::GetContentRegionAvail();
    float char_width = ImGui::GetFontBaked()->GetCharAdvance('M');
    float line_height = ImGui::GetTextLineHeight();

    int new_cols = std::max(1, static_cast<int>(content_size.x / char_width));
    int new_rows = std::max(1, static_cast<int>(content_size.y / line_height));

    if (new_cols != m_state.col || new_rows != m_state.row) {
        LOG_DEBUG("Resizing nvim widget.");

        resize(new_cols, new_rows);
    }
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

void NvimWidget::_handle_nvim_rpc(const std::vector<char>& msgpack_data) {
    if (msgpack_data.empty())
        return;

    msgpack::unpacked result;
    std::size_t len = msgpack_data.size();
    std::size_t off = 0;
    while (off != len) {
        msgpack::unpacked result;
        msgpack::unpack(result, msgpack_data.data(), len, off);
        LOG_DEBUG("Parsed a complete nvim msgpack package (offset: {})", off);
        msgpack::object obj(result.get());
        _dispatch(obj);
    }
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
    uv_close(reinterpret_cast<uv_handle_t*>(nvim_proc), nullptr);
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
        self->_handle_nvim_rpc(self->m_nvim_resp_buf);
        self->m_nvim_resp_buf.clear();
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
        LOG_DEBUG("Send nvim RPC '{}'[msgid:{}] ({} bytes)", m_method, msgid,
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
        if (!m_container_stack.empty()) {                                      \
            m_container_stack.top() -= 1;                                      \
            if (m_container_stack.top() == 0) {                                \
                m_container_stack.pop();                                       \
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
    TRY_TO_SEND();
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
    TRY_TO_SEND();
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
    TRY_TO_SEND();
}

void NvimRequest::arg_ext_body(const char* b, size_t l) {
    if (!m_packer)
        return;
    m_packer->pack_ext_body(b, l);
    TRY_TO_SEND();
}

#undef TRY_TO_SEND
} // namespace ImNeovim
