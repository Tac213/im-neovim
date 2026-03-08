#include "layer_main_window.h"
#include <imgui.h>

namespace ImNeovim {
LayerMainWindow::LayerMainWindow() {
    m_terminal = std::make_shared<Terminal>();
    m_nvim = std::make_shared<NvimWidget>();
    m_file_tree = std::make_shared<FileTreeWidget>();
    m_file_tree->set_nvim_widget(m_nvim);

    // Create the dock layout manager
    m_dock_layout = std::make_shared<DockSpaceLayout>();
}

void LayerMainWindow::on_attach() {
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigWindowsMoveFromTitleBarOnly = true;
}

void LayerMainWindow::on_imgui_render() {
    // Render the dockspace first (this creates the main dockspace window)
    if (m_dock_layout) {
        static bool first_render = true;
        if (first_render) {
            ImGuiViewport* main_viewport = ImGui::GetMainViewport();
            if (main_viewport) {
                m_dock_layout->initialize(main_viewport);
            }
        }
        if (m_dock_layout->is_initialized()) {
            m_dock_layout->render();

            // On first render, set up the dock IDs for each widget
            if (first_render) {
                m_file_tree->set_dock_id(m_dock_layout->get_dock_id_for_zone(
                    DockSpaceLayout::Zone::FileTree));
                m_nvim->set_dock_id(m_dock_layout->get_dock_id_for_zone(
                    DockSpaceLayout::Zone::Nvim));
                m_terminal->set_dock_id(m_dock_layout->get_dock_id_for_zone(
                    DockSpaceLayout::Zone::Terminal));
                first_render = false;
            }
        }
    }

    // Render the widgets (they will dock to their assigned dock IDs)
    m_file_tree->render();
    m_nvim->render();
    m_terminal->render();

    // Handle pending layout reset after all rendering is done
    if (m_dock_layout && m_dock_layout->is_reset_pending()) {
        m_dock_layout->clear_reset_pending();
        m_dock_layout->reset_to_default(false);
        // Re-assign dock IDs after reset
        m_file_tree->set_dock_id(m_dock_layout->get_dock_id_for_zone(
            DockSpaceLayout::Zone::FileTree));
        m_nvim->set_dock_id(
            m_dock_layout->get_dock_id_for_zone(DockSpaceLayout::Zone::Nvim));
        m_terminal->set_dock_id(m_dock_layout->get_dock_id_for_zone(
            DockSpaceLayout::Zone::Terminal));
    }
}
} // namespace ImNeovim
