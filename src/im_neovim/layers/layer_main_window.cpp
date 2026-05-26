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
        m_dock_layout->on_exit.connect([]() { IM_APP.request_exit(); });

        // Help > About
        m_dock_layout->on_about.connect([this]() { m_about_panel.show(); });

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

    // Render exit confirmation modal when the user tries to close the app
    // with unsaved changes.
    if (m_exit_modal_active) {
        _render_exit_modal();
    }

    // Render about panel.
    m_about_panel.render(m_nvim ? m_nvim->nvim_version_string()
                                : std::string{});

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
// --- Exit confirmation modal ---

bool LayerMainWindow::on_exit_requested() {
    if (m_nvim && m_nvim->has_modified_buffers()) {
        _show_exit_modal();
        return false;
    }
    return true;
}

void LayerMainWindow::_show_exit_modal() {
    // Only set the flag — do NOT call ImGui here.
    // on_exit_requested() runs before ImGui::NewFrame(), so any ImGui
    // function (IsPopupOpen, OpenPopup, etc.) will crash.
    // _render_exit_modal() handles the actual ImGui popup calls during
    // the render phase.
    m_exit_modal_active = true;
}

void LayerMainWindow::_handle_exit_decision(bool save, bool discard) {
    if (save) {
        // Send :wa (write all) to Neovim, then exit.
        auto req = m_nvim->start_nvim_request(
            "nvim_command", 1, [](msgpack::object&) { IM_APP.exit(); },
            [](int32_t error_code, const std::string& error_msg) {
                LOG_ERROR("Failed to save all files: {} - {}", error_code,
                          error_msg);
                // Still exit on error to avoid getting stuck.
                IM_APP.exit();
            });
        if (req) {
            const std::string cmd{"wa"};
            req->arg_str(cmd.size());
            req->arg_str_body(cmd.data(), cmd.size());
        }
    } else if (discard) {
        // Send bdelete! to force-delete the modified buffer, then exit.
        // Follows the same pattern as NvimWidget::_handle_save_decision.
        auto req = m_nvim->start_nvim_request(
            "nvim_command", 1, [](msgpack::object&) { IM_APP.exit(); },
            [](int32_t error_code, const std::string& error_msg) {
                LOG_ERROR("Failed to discard changes: {} - {}", error_code,
                          error_msg);
                // Still exit on error to avoid getting stuck.
                IM_APP.exit();
            });
        if (req) {
            const std::string cmd{"bdelete!"};
            req->arg_str(cmd.size());
            req->arg_str_body(cmd.data(), cmd.size());
        }
    }
    // Cancel: do nothing, dismiss dialog

    m_exit_modal_active = false;
    ImGui::CloseCurrentPopup();
}

void LayerMainWindow::_render_exit_modal() {
    // ImGui modal pattern: OpenPopup must be called every frame before
    // BeginPopupModal.
    if (!ImGui::IsPopupOpen("##ExitModified")) {
        ImGui::OpenPopup("##ExitModified");
    }

    // Center the modal on screen
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("##ExitModified", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("You have unsaved changes. Do you want to save before "
                    "exiting?");
        ImGui::Spacing();

        float button_width = ImGui::GetFontSize() * 7.0f;

        if (ImGui::Button("Save", ImVec2(button_width, 0))) {
            _handle_exit_decision(true, false);
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(button_width, 0))) {
            _handle_exit_decision(false, true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(button_width, 0))) {
            _handle_exit_decision(false, false);
        }

        // Also allow closing with Escape key
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            _handle_exit_decision(false, false);
        }

        ImGui::EndPopup();
    }
}

} // namespace ImNeovim
