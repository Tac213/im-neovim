#include "layer_main_window.h"
#include "im_neovim/logging.h"
#include <algorithm>
#include <im_app/application.h>
#include <im_app/file_system.h>
#include <imgui.h>
#include <tinyfiledialogs.h>

namespace ImNeovim {
LayerMainWindow::LayerMainWindow() {
    m_terminal = std::make_shared<Terminal>();
    m_nvim = std::make_shared<NvimWidget>();
    m_file_tree = std::make_shared<FileTreeWidget>();

    // Create the dock layout manager
    m_dock_layout = std::make_shared<DockSpaceLayout>();
}

void LayerMainWindow::on_update() {
    // Process pending font reloads between frames.
    // Font atlas Clear()+Load must happen BEFORE ImGui::NewFrame().
    if (m_nvim) {
        m_nvim->process_pending_font_reload();
    }
}

void LayerMainWindow::on_attach() {
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    // Connect signals.
    if (m_file_tree && m_nvim) {
        std::weak_ptr<NvimWidget> weak_nvim{m_nvim};
        m_file_tree->file_clicked.connect(
            [weak_nvim](const std::filesystem::path& path) {
                if (auto nvim = weak_nvim.lock()) {
                    nvim->open_file(path);
                }
            });
    }

    // Connect menu action signals from the dock layout.
    if (m_dock_layout) {
        // File > Exit
        m_dock_layout->on_exit.connect(
            []() { ImApp::Application::get().exit(); });

        // File > Open Folder...
        std::weak_ptr<FileTreeWidget> weak_file_tree{m_file_tree};
        std::weak_ptr<NvimWidget> weak_nvim_for_cd{m_nvim};
        m_dock_layout->on_open_folder.connect(
            [weak_file_tree, weak_nvim_for_cd]() {
                auto file_tree = weak_file_tree.lock();
                if (!file_tree) {
                    return;
                }

#ifdef IM_APP_WIN32
                // Use wide-char (UTF-16) API on Windows for proper Unicode
                // support. The char* API returns UTF-8 which
                // std::filesystem::path (wchar_t-based on Windows) does not
                // understand.
                const wchar_t* selected_w = tinyfd_selectFolderDialogW(
                    L"Open Folder", file_tree->current_directory().c_str());
                if (selected_w == nullptr) {
                    return; // User cancelled
                }
                std::filesystem::path selected_path{selected_w};
#else
                std::string current_dir =
                    file_tree->current_directory().string();
                const char* selected = tinyfd_selectFolderDialog(
                    "Open Folder", current_dir.c_str());
                if (selected == nullptr) {
                    return; // User cancelled
                }
                std::filesystem::path selected_path{selected};
#endif

                // Update the file tree.
                file_tree->set_current_directory(selected_path);

                // Send :cd to Neovim to keep the working directory in sync.
                auto nvim = weak_nvim_for_cd.lock();
                if (!nvim) {
                    return;
                }

                std::string path = ImApp::path_to_string(selected_path);
                std::replace(path.begin(), path.end(), '\\', '/');
                std::string cmd = "cd " + path;

                auto request = nvim->start_nvim_request(
                    "nvim_command", 1,
                    [](msgpack::object&) {
                        // :cd succeeded — Neovim will emit a chdir redraw
                        // event that updates the internal cwd tracking.
                    },
                    [](int32_t error_code, const std::string& error_msg) {
                        LOG_ERROR("Failed to change directory: {} - {}",
                                  error_code, error_msg);
                    });

                if (request) {
                    request->arg_str(cmd.size());
                    request->arg_str_body(cmd.data(), cmd.size());
                }
            });
    }
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
