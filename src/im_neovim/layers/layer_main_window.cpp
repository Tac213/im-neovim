#include "layer_main_window.h"
#include <imgui.h>

namespace ImNeovim {
LayerMainWindow::LayerMainWindow() {
    m_terminal = std::make_shared<Terminal>();
    m_nvim = std::make_shared<NvimWidget>();
    m_file_tree = std::make_shared<FileTreeWidget>();
    m_file_tree->set_nvim_widget(m_nvim);
}

void LayerMainWindow::on_attach() {
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigWindowsMoveFromTitleBarOnly = true;
}

void LayerMainWindow::on_imgui_render() {
    // ImGui::ShowDemoWindow();
    m_terminal->render();
    m_nvim->render();
    m_file_tree->render();
}
} // namespace ImNeovim
