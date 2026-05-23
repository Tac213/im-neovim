#pragma once
#include "im_neovim/gui/text_widget.h"
#include <atomic>
#include <functional>
#include <imgui.h>
#include <memory>
#include <msgpack.hpp>
#include <sstream>
#include <stack>
#include <string>
#include <unordered_map>
#include <uv.h>
#include <vector>

namespace ImNeovim {
class NvimRequest;

/// Parsed representation of a Neovim guifont / guifontwide string.
/// Format: "FamilyName:hNN[:b][:i]"  (e.g. "Fira Code:h12:b")
struct ParsedFont {
    std::string family;
    float size_pt{14.0f};
    bool bold{false};
    bool italic{false};

    bool operator==(const ParsedFont& other) const {
        return family == other.family && size_pt == other.size_pt &&
               bold == other.bold && italic == other.italic;
    }
    bool operator!=(const ParsedFont& other) const { return !(*this == other); }
};

class NvimWidget : public TextWidget,
                   public std::enable_shared_from_this<NvimWidget> {
  public:
    NvimWidget();
    ~NvimWidget();

    void open_file(const std::string& path);
    void render() override;
    void resize(uint32_t cols, uint32_t rows);

    /// Process any pending font reload (Clear atlas + load new fonts).
    /// Must be called between frames, before ImGui::NewFrame().
    void process_pending_font_reload();

    // Docking support
    void set_dock_id(ImGuiID dock_id) { m_dock_id = dock_id; }
    ImGuiID get_dock_id() const { return m_dock_id; }

    std::shared_ptr<NvimRequest> start_nvim_request(
        const std::string& method, uint8_t param_count,
        std::function<void(msgpack::object&)>&& on_result,
        std::function<void(int32_t, const std::string&)>&& on_error);

  private:
    friend class NvimRequest;
    void _spawn_nvim();
    void _initialize();
    void _set_nvim_attached(bool attached);
    void _handle_nvim_request(const uint32_t& msgid, const char* method,
                              msgpack::object_array& args);
    void _handle_nvim_notification(const char* event,
                                   msgpack::object_array& args);

    void _handle_nvim_redraw(const char* operation,
                             msgpack::object_array& args);
    void _handle_nvim_gui_event(const char* event, msgpack::object_array& args);

    /* GUI-related methods */
    void _check_font_size_changed();
    void _handle_nvim_resize();
    void _handle_keyboard_input();
    void _handle_mouse_input();
    void _update_ime_position();
    void _flush_pending_input();
    void _notify_nvim_resize(uint32_t cols, uint32_t rows);
    void _render_grid(ImDrawList* draw_list, const ImVec2& pos,
                      float char_width, float line_height);

    /* Font management */
    static ParsedFont _parse_guifont(const std::string& guifont_str);
    void _check_font_reload_needed();
    void _execute_font_reload();

    /* Redraw operation handlers */
    void _redraw_resize(msgpack::object_array& args);
    void _redraw_clear(msgpack::object_array& args);
    void _redraw_cursor_goto(msgpack::object_array& args);
    void _redraw_put(msgpack::object_array& args);
    void _redraw_highlight_set(msgpack::object_array& args);
    void _redraw_flush(msgpack::object_array& args);
    void _redraw_option_set(msgpack::object_array& args);
    void _redraw_set_title(msgpack::object_array& args);
    void _redraw_default_colors_set(msgpack::object_array& args);
    void _redraw_set_scroll_region(msgpack::object_array& args);
    void _redraw_scroll(msgpack::object_array& args);
    void _redraw_eol_clear(msgpack::object_array& args);
    void _redraw_mode_info_set(msgpack::object_array& args);
    void _redraw_mode_change(msgpack::object_array& args);
    void _redraw_busy_start(msgpack::object_array& args);
    void _redraw_busy_stop(msgpack::object_array& args);
    void _redraw_mouse_on(msgpack::object_array& args);
    void _redraw_mouse_off(msgpack::object_array& args);
    void _redraw_bell(msgpack::object_array& args);
    void _redraw_suspend(msgpack::object_array& args);
    void _redraw_chdir(msgpack::object_array& args);
    void _redraw_popupmenu_show(msgpack::object_array& args);
    void _redraw_popupmenu_select(msgpack::object_array& args);
    void _redraw_popupmenu_hide(msgpack::object_array& args);
    void _render_popup_menu(ImDrawList* draw_list, const ImVec2& pos,
                            float char_width, float line_height);

    /* Multigrid redraw handlers */
    void _redraw_grid_resize(msgpack::object_array& args);
    void _redraw_grid_line(msgpack::object_array& args);
    void _redraw_grid_clear(msgpack::object_array& args);
    void _redraw_grid_cursor_goto(msgpack::object_array& args);
    void _redraw_grid_scroll(msgpack::object_array& args);
    void _redraw_grid_destroy(msgpack::object_array& args);
    void _redraw_hl_attr_define(msgpack::object_array& args);
    void _redraw_hl_group_set(msgpack::object_array& args);

    /* Callbacks by libuv */
    // Called by libuv when nvim exits/
    static void _on_nvim_exit(uv_process_t* nvim_proc, int64_t exit_status,
                              int term_signal);
    // Called by libuv to allocate memory for reading.
    static void _uv_alloc_cb(uv_handle_t* handle, size_t suggested,
                             uv_buf_t* buf);
    // Callback invoked by lib uv after it copies the data into the buffer
    // provided by `_uv_alloc_cb`. This is also called on EOF or when
    // `_uv_alloc_cb` returns a 0-lenth buffer.
    static void _uv_read_cb(uv_stream_t* stream, ssize_t nread,
                            const uv_buf_t* buf);
    static void _uv_write_cb(uv_write_t* req, int status);

    /* RPC-related methods */
    void _send_nvim_error(const msgpack::object& req, const std::string& msg);
    void _send_nvim_error(uint32_t msgid, const std::string& msg);
    std::size_t _handle_nvim_rpc(const std::vector<char>& msgpack_data);
    void _dispatch(msgpack::object& req);
    void _dispatch_request(msgpack::object& req);
    void _dispatch_response(msgpack::object& resp);
    void _dispatch_notification(msgpack::object& nt);

    // Highlight attributes structure
    struct HighlightAttr {
        ImVec4 fg;
        ImVec4 bg;
        ImVec4 sp;
        bool bold : 1;
        bool italic : 1;
        bool underline : 1;
        bool undercurl : 1;
        bool reverse : 1;

        HighlightAttr();
    };

    // Scroll region within a grid
    struct ScrollRegion {
        uint32_t top{0};
        uint32_t bot{0};
        uint32_t left{0};
        uint32_t right{0};
    };

    // Grid structure
    struct Grid {
        uint32_t id;
        uint32_t width;
        uint32_t height;
        std::vector<std::vector<ScreenCell>> cells;
        ScrollRegion m_scroll_region;

        Grid();
        void clear();
        void resize(uint32_t w, uint32_t h);
        void scroll_region(int count);
    };

    struct NvimState {
        uint32_t cursor_x{0};
        uint32_t cursor_y{0};
        uint32_t row{0};
        uint32_t col{0};
    } m_state;

    // Cursor shape for different modes
    enum class CursorShape { Block, Horizontal, Vertical };

    // Mode info entry from mode_info_set
    struct ModeInfoEntry {
        std::string cursor_shape{"block"};
        uint32_t cell_percentage{100};
        uint32_t blinkwait{0};
        uint32_t blinkon{0};
        uint32_t blinkoff{0};
        int attr_id{0};
    };

    // Popup menu entry (completion item)
    struct PopupMenuEntry {
        std::string text;
        std::string kind;
        std::string extra;
        std::string info;
    };

    // Grid and highlight state
    std::unordered_map<uint32_t, Grid> m_grids;
    uint32_t m_current_grid{1};
    std::unordered_map<int, HighlightAttr> m_hl_attrs;
    HighlightAttr m_current_hl;
    ImVec4 m_default_fg;
    ImVec4 m_default_bg;
    ImVec4 m_default_sp;

    ImVec2 m_window_size{800.0f, 400.0f};
    bool m_dark_mode{true};
    bool m_needs_render{true};
    std::string m_pending_input;

    // Mouse tracking state
    struct {
        uint32_t was_down{0};
        int last_drag_cell_x{-1};
        int last_drag_cell_y{-1};
        float scroll_rem_y{0.0f};
        float scroll_rem_x{0.0f};
    } m_mouse;

    // Mode and cursor state
    std::vector<ModeInfoEntry> m_mode_info;
    bool m_cursor_style_enabled{false};
    CursorShape m_cursor_shape{CursorShape::Block};
    uint32_t m_cursor_cell_percentage{100};
    uint32_t m_cursor_blinkwait{0};
    uint32_t m_cursor_blinkon{0};
    uint32_t m_cursor_blinkoff{0};
    std::string m_current_mode_name;
    bool m_busy{false};
    double m_last_blink_time{0.0};
    bool m_cursor_visible{true};

    // Mouse, bell, and option state
    bool m_mouse_enabled{true};
    bool m_bell_pending{false};
    double m_bell_timestamp{0.0};
    std::string m_requested_font;
    std::string m_requested_font_wide;
    int32_t m_linespace{0};
    bool m_suspend_pending{false};

    // Font reload state
    ParsedFont m_current_font;
    ParsedFont m_current_font_wide;
    ParsedFont m_pending_font;      // Font to load in next reload cycle
    ParsedFont m_pending_font_wide; // Wide font to load in next reload cycle
    bool m_font_reload_pending{false};

    // Popup menu state
    std::vector<PopupMenuEntry> m_popup_items;
    int32_t m_popup_selected{-1};
    int32_t m_popup_anchor_row{0};
    int32_t m_popup_anchor_col{0};
    bool m_popup_visible{false};

    // Multigrid protocol state
    bool m_multigrid_enabled{false};
    std::unordered_map<std::string, int> m_hl_group_map;

    uv_process_t m_nvim_proc;
    uv_pipe_t m_in_pipe;
    uv_pipe_t m_out_pipe;
    std::string m_nvim_exe;
    std::string m_nvim_cwd;
    uint64_t m_nvim_channel{0};
    uint64_t m_nvim_api_compatible{0};
    uint64_t m_nvim_api_level{0};
    std::vector<std::string> m_nvim_ui_options;
    bool m_nvim_attached{false};
    bool m_nvim_exited{false};

    /* msgpack-related */
    std::atomic<uint32_t> m_nvim_msgid{1};
    std::vector<char> m_nvim_resp_buf;
    std::unordered_map<uint32_t, std::shared_ptr<NvimRequest>> m_requests;
};

class NvimRequest {
  public:
    explicit NvimRequest(
        uint32_t msgid, const std::string& method, uint8_t param_count,
        std::weak_ptr<NvimWidget> nvim,
        std::function<void(msgpack::object&)>&& on_result,
        std::function<void(int32_t, const std::string&)>&& on_error);

    const uint32_t msgid;

    void arg_uint8(uint8_t d);
    void arg_uint16(uint16_t d);
    void arg_uint32(uint32_t d);
    void arg_uint64(uint64_t d);
    void arg_int8(int8_t d);
    void arg_int16(int16_t d);
    void arg_int32(int32_t d);
    void arg_int64(int64_t d);

    void arg_fix_uint8(uint8_t d);
    void arg_fix_uint16(uint16_t d);
    void arg_fix_uint32(uint32_t d);
    void arg_fix_uint64(uint64_t d);
    void arg_fix_int8(int8_t d);
    void arg_fix_int16(int16_t d);
    void arg_fix_int32(int32_t d);
    void arg_fix_int64(int64_t d);

    void arg_char(char d);
    void arg_signed_char(signed char d);
    void arg_short(int16_t d);
    void arg_int(int d);
    void arg_long(int32_t d);
    void arg_long_long(int64_t d);
    void arg_unsigned_char(uint8_t d);
    void arg_unsigned_short(uint16_t d);
    void arg_unsigned_int(uint32_t d);
    void arg_unsigned_long(uint32_t d);
    void arg_unsigned_long_long(uint64_t d);

    void arg_float(float d);
    void arg_double(double d);

    void arg_nil();
    void arg_true();
    void arg_false();

    void arg_array(size_t n);

    void arg_map(size_t n);

    void arg_str(size_t l);
    void arg_str_body(const char* b, size_t l);

    void arg_bin(size_t l);
    void arg_bin_body(const char* b, size_t l);

    void arg_ext(size_t l, int8_t type);
    void arg_ext_body(const char* b, size_t l);

  private:
    friend class NvimWidget;
    void _send();

    std::weak_ptr<NvimWidget> m_nvim;
    const std::string m_method;
    const uint8_t m_param_count;
    uint8_t m_arg_count = 0;
    std::stringstream m_buffer;
    std::unique_ptr<msgpack::packer<std::stringstream>> m_packer = nullptr;
    std::function<void(msgpack::object&)> m_on_result;
    std::function<void(int32_t, const std::string&)> m_on_error;

    std::stack<size_t> m_container_stack;
};
} // namespace ImNeovim
