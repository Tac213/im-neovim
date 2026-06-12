#include "layer_main_window.h"
#include "im_neovim/globals.h"
#include "im_neovim/logging.h"
#include "layer_instance_manager.h"
#include <algorithm>
#include <im_app/application.h>
#include <im_app/file_system.h>
#include <im_app/ipc_channel.h>
#include <imgui.h>
#include <tinyfiledialogs.h>

#ifdef IM_APP_DARWIN
#include "im_neovim/platforms/darwin_menu_bridge.h"
#endif

#ifdef IM_APP_WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#elif defined(IM_APP_DARWIN)
#include <fcntl.h>
#include <spawn.h>
#include <unistd.h>
#else
#include <climits>
#include <fcntl.h>
#include <spawn.h>
#include <unistd.h>
#endif

#ifndef IM_APP_WIN32
extern char** environ;
#endif

namespace {

/// Spawn a detached imnvim process with no arguments.
/// Returns true on success.
bool spawn_new_window(const std::string& imnvim_path) {
#ifdef IM_APP_WIN32
    std::wstring wpath(imnvim_path.begin(), imnvim_path.end());
    std::wstring wargs = L"\"" + wpath + L"\"";

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    BOOL ok = ::CreateProcessW(nullptr, wargs.data(), nullptr, nullptr, FALSE,
                               CREATE_NEW_CONSOLE | CREATE_NEW_PROCESS_GROUP,
                               nullptr, nullptr, &si, &pi);

    if (ok) {
        ::CloseHandle(pi.hProcess);
        ::CloseHandle(pi.hThread);
        return true;
    }
    return false;
#else
    std::vector<const char*> argv;
    argv.push_back(imnvim_path.c_str());
    argv.push_back(nullptr);

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null",
                                     O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null",
                                     O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null",
                                     O_WRONLY, 0);

    pid_t pid;
    int rc = posix_spawn(&pid, imnvim_path.c_str(), &actions, &attr,
                         const_cast<char* const*>(argv.data()), environ);

    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);

    return rc == 0;
#endif
}

} // anonymous namespace

namespace ImNeovim {
LayerMainWindow::LayerMainWindow() {
    m_terminal = std::make_shared<Terminal>();
    m_file_tree = std::make_shared<FileTreeWidget>();
    m_nvim = std::make_shared<NvimWidget>();
    m_output_widget = std::make_shared<OutputWidget>();

    // Create the dock layout manager
    m_dock_layout = std::make_shared<DockSpaceLayout>();

    m_about_panel = std::make_shared<AboutPanel>();
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

    // File tree signals
    if (m_file_tree && m_nvim) {
        m_file_tree->file_clicked.connect(
            std::bind_front(&LayerMainWindow::_on_file_clicked, this));
    }
    if (m_file_tree) {
        m_file_tree->on_open_folder_requested.connect(
            std::bind_front(&LayerMainWindow::_add_folder_to_workspace, this));
        m_file_tree->on_remove_folder_requested.connect(
            std::bind_front(&LayerMainWindow::_on_remove_folder, this));
    }

    // Dock layout menu action signals
    if (m_dock_layout) {
        m_dock_layout->on_exit.connect(
            std::bind_front(&LayerMainWindow::_on_exit, this));
        m_dock_layout->on_new_window.connect(
            std::bind_front(&LayerMainWindow::_on_new_window, this));
        m_dock_layout->on_about.connect(
            std::bind_front(&LayerMainWindow::_on_about, this));
        m_dock_layout->on_open_folder.connect(
            std::bind_front(&LayerMainWindow::_open_folder, this));
        m_dock_layout->on_add_folder_to_workspace.connect(
            std::bind_front(&LayerMainWindow::_add_folder_to_workspace, this));
        m_dock_layout->on_toggle_file_tree.connect(
            std::bind_front(&LayerMainWindow::_toggle_file_tree, this));
        m_dock_layout->on_toggle_terminal.connect(
            std::bind_front(&LayerMainWindow::_toggle_terminal, this));
        m_dock_layout->on_toggle_output.connect(
            std::bind_front(&LayerMainWindow::_toggle_output, this));
    }

    // Keep nvim's working directory in sync with the first workspace folder.
    g_workspace.on_changed.connect(
        std::bind_front(&LayerMainWindow::_sync_nvim_cwd, this));

    // Workspace-aware panel visibility.
    // When no workspace folders are open, hide the file tree and
    // terminal by default to give nvim the full window.
    // Manual View > File Tree / Terminal menu toggles override auto-hide
    // via m_*_forced_visible.
    g_workspace.on_changed.connect(
        std::bind_front(&LayerMainWindow::_update_panel_visibility, this));
    _update_panel_visibility();

    // On macOS, wire up the native menu bar.
#ifdef IM_APP_DARWIN
    ImNeovim::darwin_setup_native_menus();

    ImNeovim::g_native_on_open_folder.connect(
        std::bind_front(&LayerMainWindow::_open_folder, this));
    ImNeovim::g_native_on_add_folder_to_workspace.connect(
        std::bind_front(&LayerMainWindow::_add_folder_to_workspace, this));
    ImNeovim::g_native_on_about.connect(
        std::bind_front(&LayerMainWindow::_on_about, this));
    ImNeovim::g_native_on_reset_layout.connect(
        std::bind_front(&LayerMainWindow::_on_reset_layout, this));
    ImNeovim::g_native_on_exit.connect(
        std::bind_front(&LayerMainWindow::_on_exit, this));
    ImNeovim::g_native_on_new_window.connect(
        std::bind_front(&LayerMainWindow::_on_new_window, this));
    ImNeovim::g_native_on_toggle_file_tree.connect(
        std::bind_front(&LayerMainWindow::_toggle_file_tree, this));
    ImNeovim::g_native_on_toggle_terminal.connect(
        std::bind_front(&LayerMainWindow::_toggle_terminal, this));
    ImNeovim::g_native_on_toggle_output.connect(
        std::bind_front(&LayerMainWindow::_toggle_output, this));
#endif
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
                // Tell the dock layout the current window names so that
                // DockBuilderDockWindow can place them correctly.
                m_dock_layout->set_window_names(
                    m_file_tree->window_title(), m_nvim->window_title(),
                    m_terminal->window_title(),
                    m_output_widget->window_title());

                // Build the appropriate layout based on visibility.
                bool has_folders = !g_workspace.empty();
                bool show_ft = m_file_tree_forced_visible.value_or(has_folders);
                bool show_t = m_terminal_forced_visible.value_or(has_folders);
                bool show_o = m_output_forced_visible.value_or(has_folders);
                m_dock_layout->build_layout(show_ft, show_t || show_o);

                m_file_tree->set_dock_id(m_dock_layout->get_dock_id_for_zone(
                    DockSpaceLayout::Zone::FileTree));
                m_nvim->set_dock_id(m_dock_layout->get_dock_id_for_zone(
                    DockSpaceLayout::Zone::Nvim));
                m_terminal->set_dock_id(m_dock_layout->get_dock_id_for_zone(
                    DockSpaceLayout::Zone::Terminal));
                m_output_widget->set_dock_id(
                    m_dock_layout->get_dock_id_for_zone(
                        DockSpaceLayout::Zone::Output));
                first_render = false;
            }
        }
    }

    // Render the widgets (they will dock to their assigned dock IDs)
    m_file_tree->render();
    m_nvim->render();
    m_terminal->render();
    m_output_widget->render();

    // Sync visibility: when the user closes a docked window via the dock
    // tab close button, the widget's internal m_is_visible becomes false
    // but the LayerMainWindow forced-visibility overrides and the
    // DockSpaceLayout menu checkmarks are not updated.  Detect the
    // discrepancy here so that menu checkmarks stay correct and the
    // widget does not re-appear on the next _update_panel_visibility() call.
    {
        bool has_folders = !g_workspace.empty();
        bool changed = false;

        auto sync_one = [&](bool widget_visible, std::optional<bool>& forced,
                            bool auto_vis) {
            bool expected = forced.value_or(auto_vis);
            if (widget_visible != expected) {
                forced = (widget_visible == auto_vis)
                             ? std::optional<bool>{}
                             : std::optional<bool>{widget_visible};
                return true;
            }
            return false;
        };

        changed |= sync_one(m_file_tree->is_visible(),
                            m_file_tree_forced_visible, has_folders);
        changed |= sync_one(m_terminal->is_visible(), m_terminal_forced_visible,
                            has_folders);
        changed |= sync_one(m_output_widget->is_visible(),
                            m_output_forced_visible, has_folders);

        if (changed) {
            _update_panel_visibility();
        }
    }

    // Process pending focus requests from toggle handlers (state 1:
    // show + focus).  Must happen after the widget's render() so the
    // ImGui window exists.
    if (m_focus_terminal_next_frame) {
        ImGui::SetWindowFocus(m_terminal->window_title().c_str());
        m_focus_terminal_next_frame = false;
    }
    if (m_focus_output_next_frame) {
        ImGui::SetWindowFocus(m_output_widget->window_title().c_str());
        m_focus_output_next_frame = false;
    }

    // Render exit confirmation modal when the user tries to close the app
    // with unsaved changes.
    if (m_exit_modal_active) {
        _render_exit_modal();
    }

    // Render about panel.
    m_about_panel->render(m_nvim ? m_nvim->nvim_version_string()
                                 : std::string{});

    // Handle pending layout reset after all rendering is done
    if (m_dock_layout && m_dock_layout->is_reset_pending()) {
        m_dock_layout->clear_reset_pending();
        // Update window names before rebuilding so that
        // DockBuilderDockWindow targets the current window titles.
        m_dock_layout->set_window_names(
            m_file_tree->window_title(), m_nvim->window_title(),
            m_terminal->window_title(), m_output_widget->window_title());
        // Reset also clears any manual visibility overrides.
        m_file_tree_forced_visible.reset();
        m_terminal_forced_visible.reset();

        // Choose layout based on current visibility state.
        bool has_folders = !g_workspace.empty();
        bool show_ft = m_file_tree_forced_visible.value_or(has_folders);
        bool show_t = m_terminal_forced_visible.value_or(has_folders);
        bool show_o = m_output_forced_visible.value_or(has_folders);
        m_dock_layout->build_layout(show_ft, show_t || show_o);

        // Re-assign dock IDs after reset
        _assign_dock_ids();
    }

    // Handle pending adaptive rebuild (workspace / visibility changes)
    if (m_dock_layout && m_dock_layout->is_adaptive_rebuild_pending()) {
        m_dock_layout->clear_adaptive_rebuild_pending();
        // Update window names before rebuilding.
        m_dock_layout->set_window_names(
            m_file_tree->window_title(), m_nvim->window_title(),
            m_terminal->window_title(), m_output_widget->window_title());

        bool has_folders = !g_workspace.empty();
        bool show_ft = m_file_tree_forced_visible.value_or(has_folders);
        bool show_t = m_terminal_forced_visible.value_or(has_folders);
        bool show_o = m_output_forced_visible.value_or(has_folders);
        m_dock_layout->build_layout(show_ft, show_t || show_o);

        _assign_dock_ids();
    }
}
void LayerMainWindow::_assign_dock_ids() {
    m_file_tree->set_dock_id(
        m_dock_layout->get_dock_id_for_zone(DockSpaceLayout::Zone::FileTree));
    m_nvim->set_dock_id(
        m_dock_layout->get_dock_id_for_zone(DockSpaceLayout::Zone::Nvim));
    m_terminal->set_dock_id(
        m_dock_layout->get_dock_id_for_zone(DockSpaceLayout::Zone::Terminal));
    m_output_widget->set_dock_id(
        m_dock_layout->get_dock_id_for_zone(DockSpaceLayout::Zone::Output));
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

// --- Signal handlers ---

void LayerMainWindow::_on_file_clicked(const std::filesystem::path& path) {
    m_nvim->open_file(path);
}

void LayerMainWindow::
    _on_remove_folder( // NOLINT(readability-convert-member-functions-to-static)
        const std::filesystem::path& path) {
    g_workspace.remove_folder(path);
}

void LayerMainWindow::
    _on_exit() { // NOLINT(readability-convert-member-functions-to-static)
    IM_APP.request_exit();
}

void LayerMainWindow::
    _on_new_window() { // NOLINT(readability-convert-member-functions-to-static)
    std::string exe_path =
        ImApp::path_to_string(ImApp::FileSystem::executable_path());
    if (!spawn_new_window(exe_path)) {
        LOG_ERROR("Failed to spawn new window: {}", exe_path);
    }
}

void LayerMainWindow::_on_about() { m_about_panel->show(); }

void LayerMainWindow::
    _open_folder() { // NOLINT(readability-convert-member-functions-to-static)
#ifdef IM_APP_WIN32
    std::filesystem::path start_dir = g_workspace.first_folder_or_home();
    const wchar_t* selected_w =
        tinyfd_selectFolderDialogW(L"Open Folder", start_dir.c_str());
    if (selected_w == nullptr) {
        return;
    }
    std::filesystem::path selected_path{selected_w};
#else
    std::filesystem::path start_dir = g_workspace.first_folder_or_home();
    std::string start_str = ImApp::path_to_string(start_dir);
    const char* selected =
        tinyfd_selectFolderDialog("Open Folder", start_str.c_str());
    if (selected == nullptr) {
        return;
    }
    std::filesystem::path selected_path{selected};
#endif
    // If another instance already has this folder as its sole
    // workspace folder, activate it instead.
    std::filesystem::path abs_path =
        std::filesystem::weakly_canonical(selected_path);
    std::string key = "ImNeovim:" + ImApp::path_to_string(abs_path);
    key = std::to_string(std::hash<std::string>{}(key));

    auto ipc = ImApp::IpcChannel::create();
    if (ipc->send_message(key, "")) {
        return; // Existing instance activated.
    }

    // Replace the workspace with the selected folder.
    g_workspace.replace_with(selected_path);
}

void LayerMainWindow::
    _add_folder_to_workspace() { // NOLINT(readability-convert-member-functions-to-static)
#ifdef IM_APP_WIN32
    // Start the dialog at the first workspace folder (or home).
    std::filesystem::path start_dir = g_workspace.first_folder_or_home();
    const wchar_t* selected_w = tinyfd_selectFolderDialogW(
        L"Add Folder to Workspace", start_dir.c_str());
    if (selected_w == nullptr) {
        return; // User cancelled
    }
    std::filesystem::path selected_path{selected_w};
#else
    std::filesystem::path start_dir = g_workspace.first_folder_or_home();
    std::string start_str = ImApp::path_to_string(start_dir);
    const char* selected =
        tinyfd_selectFolderDialog("Add Folder to Workspace", start_str.c_str());
    if (selected == nullptr) {
        return; // User cancelled
    }
    std::filesystem::path selected_path{selected};
#endif
    g_workspace.add_folder(selected_path);
}

void LayerMainWindow::_sync_nvim_cwd() {
    if (!m_nvim) {
        return;
    }

    std::filesystem::path cwd = g_workspace.first_folder_or_home();
    std::string path = ImApp::path_to_string(cwd);
    std::replace(path.begin(), path.end(), '\\', '/');
    std::string cmd = "cd " + path;

    auto request = m_nvim->start_nvim_request(
        "nvim_command", 1,
        [](msgpack::object&) {
            // :cd succeeded — Neovim will emit a chdir redraw
            // event that updates the internal cwd tracking.
        },
        [](int32_t error_code, const std::string& error_msg) {
            LOG_ERROR("Failed to change directory: {} - {}", error_code,
                      error_msg);
        });

    if (request) {
        request->arg_str(cmd.size());
        request->arg_str_body(cmd.data(), cmd.size());
    }
}

void LayerMainWindow::_update_panel_visibility() {
    bool has_folders = !g_workspace.empty();

    // Manual override takes priority; fall back to workspace state.
    bool ft_vis = m_file_tree_forced_visible.value_or(has_folders);
    bool t_vis = m_terminal_forced_visible.value_or(has_folders);
    bool o_vis = m_output_forced_visible.value_or(has_folders);

    if (m_file_tree) {
        m_file_tree->set_visible(ft_vis);
    }
    if (m_terminal) {
        m_terminal->set_visible(t_vis);
    }
    if (m_output_widget) {
        m_output_widget->set_visible(o_vis);
    }

    // Keep the dock layout checkmark bools in sync for the
    // non-Darwin ImGui menu bar.
    m_dock_layout->file_tree_visible = ft_vis;
    m_dock_layout->terminal_visible = t_vis;
    m_dock_layout->output_visible = o_vis;
    m_dock_layout->queue_adaptive_rebuild();

#ifdef IM_APP_DARWIN
    // Keep the native macOS menu item checkmarks in sync.
    darwin_update_file_tree_menu_state(ft_vis);
    darwin_update_terminal_menu_state(t_vis);
    darwin_update_output_menu_state(o_vis);
#endif
}

void LayerMainWindow::_toggle_file_tree() {
    bool has_folders = !g_workspace.empty();
    bool current = m_file_tree_forced_visible.value_or(has_folders);
    bool next = !current;
    m_file_tree_forced_visible = (next == has_folders)
                                     ? std::optional<bool>{}
                                     : std::optional<bool>{next};
    _update_panel_visibility();
}

void LayerMainWindow::_toggle_terminal() {
    bool has_folders = !g_workspace.empty();
    bool currently_visible = m_terminal_forced_visible.value_or(has_folders);

    if (!currently_visible) {
        // State 1: not visible → show the terminal in the dock area and
        // focus it once the window has been created.
        bool next = true;
        m_terminal_forced_visible = (next == has_folders)
                                        ? std::optional<bool>{}
                                        : std::optional<bool>{next};
        m_focus_terminal_next_frame = true;
        _update_panel_visibility();
        return;
    }

    // Visible — check if it's the active tab in the bottom dock node.
    if (m_dock_layout->is_active_tab_in_bottom_dock(
            m_terminal->window_title())) {
        // State 3: visible AND active tab → hide the entire bottom dock
        // area (both terminal and output).
        m_terminal_forced_visible = false;
        m_output_forced_visible = false;
        _update_panel_visibility();
        return;
    }

    // State 2: visible but not the active tab → focus it.
    ImGui::SetWindowFocus(m_terminal->window_title().c_str());
    // Checkmark stays on; no layout rebuild needed.
    m_dock_layout->terminal_visible = true;
#ifdef IM_APP_DARWIN
    darwin_update_terminal_menu_state(true);
#endif
}

void LayerMainWindow::_toggle_output() {
    bool has_folders = !g_workspace.empty();
    bool currently_visible = m_output_forced_visible.value_or(has_folders);

    if (!currently_visible) {
        // State 1: not visible → show the output widget in the dock area
        // and focus it once the window has been created.
        bool next = true;
        m_output_forced_visible = (next == has_folders)
                                      ? std::optional<bool>{}
                                      : std::optional<bool>{next};
        m_focus_output_next_frame = true;
        _update_panel_visibility();
        return;
    }

    // Visible — check if it's the active tab in the bottom dock node.
    if (m_dock_layout->is_active_tab_in_bottom_dock(
            m_output_widget->window_title())) {
        // State 3: visible AND active tab → hide the entire bottom dock
        // area (both terminal and output).
        m_terminal_forced_visible = false;
        m_output_forced_visible = false;
        _update_panel_visibility();
        return;
    }

    // State 2: visible but not the active tab → focus it.
    ImGui::SetWindowFocus(m_output_widget->window_title().c_str());
    // Checkmark stays on; no layout rebuild needed.
    m_dock_layout->output_visible = true;
#ifdef IM_APP_DARWIN
    darwin_update_output_menu_state(true);
#endif
}

void LayerMainWindow::_on_reset_layout() { m_dock_layout->queue_reset(); }

} // namespace ImNeovim
