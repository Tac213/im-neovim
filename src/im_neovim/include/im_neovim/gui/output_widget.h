#pragma once

#include <deque>
#include <im_app/output_capture.h>
#include <imgui.h>
#include <string>
#include <vector>

namespace ImNeovim {

/**
 * @brief Dockable widget that displays captured application log output.
 *
 * Polls ImApp::OutputCapture::get_entries() every frame and renders
 * colour-coded log lines in a scrollable region with an optional text
 * filter and auto-scroll behaviour.
 *
 * Usage pattern (same as FileTreeWidget):
 *   1. Instantiate via std::make_shared<OutputWidget>()
 *   2. Call set_dock_id() with the ID from DockSpaceLayout::Zone::Output
 *   3. Call render() every frame inside on_imgui_render()
 */
class OutputWidget {
  public:
    OutputWidget();
    ~OutputWidget();

    /**
     * @brief Render the output window.
     *
     * Must be called every frame during the ImGui render phase.
     */
    void render();

    // -- Window identity (used by DockSpaceLayout) --

    const std::string& window_title() const { return m_window_title; }
    void set_window_title(const std::string& title) { m_window_title = title; }

    // -- Visibility --

    bool is_visible() const { return m_is_visible; }
    void set_visible(bool visible) { m_is_visible = visible; }

    // -- Docking --

    void set_dock_id(ImGuiID dock_id) { m_dock_id = dock_id; }
    ImGuiID get_dock_id() const { return m_dock_id; }

  private:
    // --- Rendering sub-steps ---
    void _render_toolbar();
    void _render_log_entries();
    static ImVec4 _color_for_level(int level);

    // --- Window state ---
    std::string m_window_title{"Output"};
    bool m_is_visible{true};
    ImGuiID m_dock_id{0};

    // --- Log buffer (local copy of entries collected from OutputCapture) ---
    std::deque<ImApp::LogEntry> m_entries;

    // --- Filter ---
    char m_filter_buffer[256] = {};

    // --- Auto-scroll ---
    bool m_auto_scroll{true};

    // --- Logger filter (combo box) ---
    std::vector<std::string> m_logger_names;
    int m_selected_logger_index{0};
    bool m_logger_names_populated{false};

    void _populate_logger_names();
};

} // namespace ImNeovim
