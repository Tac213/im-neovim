#include "im_neovim/gui/dock_space_layout.h"

#include "im_neovim/logging.h"
#include <imgui_internal.h>

namespace {

/// Flags applied to the nvim dock node: no close button, no undocking,
/// no tab bar. The tabline is rendered inside the nvim widget via ImGui's
/// BeginTabBar (ext_tabline UI extension).
constexpr ImGuiDockNodeFlags g_nvim_lock_flags =
    static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_NoCloseButton) |
    static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_NoUndocking) |
    static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_NoTabBar);

} // namespace

namespace ImNeovim {

DockSpaceLayout::DockSpaceLayout() = default;

DockSpaceLayout::~DockSpaceLayout() {
    if (m_initialized) {
        shutdown();
    }
}

void DockSpaceLayout::initialize(ImGuiViewport* main_viewport) {
    if (m_initialized) {
        LOG_WARN("DockSpaceLayout already initialized");
        return;
    }

    m_main_viewport = main_viewport;

    // Generate a stable dockspace ID based on the window name
    m_dockspace_id = ImGui::GetID(g_dockspace_window_name);

    LOG_INFO("DockSpaceLayout initialized with dockspace ID: {}",
             m_dockspace_id);

    m_initialized = true;
}

void DockSpaceLayout::shutdown() {
    if (!m_initialized) {
        return;
    }

    LOG_INFO("DockSpaceLayout shutting down");

    // ImGui's dock context is managed by ImGui itself, so we just clear our
    // state
    m_dockspace_id = 0;
    m_dock_id_left = 0;
    m_dock_id_right_top = 0;
    m_dock_id_right_bottom = 0;
    m_main_viewport = nullptr;
    m_initialized = false;
}

void DockSpaceLayout::render() {
    if (!m_initialized || m_main_viewport == nullptr) {
        return;
    }

    // Create the dockspace window that covers the entire viewport
    // We use NoDocking to prevent this window itself from being docked
    // and NoNavFocus to prevent it from stealing focus
    ImGuiWindowFlags window_flags =
#ifndef IM_APP_DARWIN
        ImGuiWindowFlags_MenuBar |
#endif
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    // Set the window to fill the entire main viewport
    const ImVec2 viewport_pos = m_main_viewport->WorkPos;
    const ImVec2 viewport_size = m_main_viewport->WorkSize;

    ImGui::SetNextWindowPos(viewport_pos);
    ImGui::SetNextWindowSize(viewport_size);
    ImGui::SetNextWindowViewport(m_main_viewport->ID);

    // Create the dockspace window
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin(g_dockspace_window_name, nullptr, window_flags);
    ImGui::PopStyleVar();

    // Render menu bar (macOS uses native menus instead)
#ifndef IM_APP_DARWIN
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Window", "Ctrl+Shift+N")) {
                on_new_window.emit();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Open Folder...")) {
                on_open_folder.emit();
            }
            if (ImGui::MenuItem("Add Folder to Workspace...")) {
                on_add_folder_to_workspace.emit();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                on_exit.emit();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Reset Layout")) {
                // Queue a reset for after the frame completes
                m_pending_reset = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("File Tree", "Ctrl+Shift+E",
                                &file_tree_visible)) {
                on_toggle_file_tree.emit();
            }
            if (ImGui::MenuItem("Terminal", "Ctrl+`", &terminal_visible)) {
                on_toggle_terminal.emit();
            }
            if (ImGui::MenuItem("Output", "Ctrl+Shift+U", &output_visible)) {
                on_toggle_output.emit();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) {
                on_about.emit();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    // Keyboard shortcuts for menu items (macOS uses native menus instead).
    // Use RouteGlobal+RouteOverFocused so app shortcuts take priority over
    // the focused nvim widget, but still yield to active ImGui items.
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_N,
                        ImGuiInputFlags_RouteGlobal |
                            ImGuiInputFlags_RouteOverFocused)) {
        on_new_window.emit();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_E,
                        ImGuiInputFlags_RouteGlobal |
                            ImGuiInputFlags_RouteOverFocused)) {
        on_toggle_file_tree.emit();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_GraveAccent,
                        ImGuiInputFlags_RouteGlobal |
                            ImGuiInputFlags_RouteOverFocused)) {
        on_toggle_terminal.emit();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_U,
                        ImGuiInputFlags_RouteGlobal |
                            ImGuiInputFlags_RouteOverFocused)) {
        on_toggle_output.emit();
    }
#endif

    // Create the actual dockspace
    // ImGuiDockNodeFlags_PassthruCentralNode allows the dockspace to be
    // transparent to input when no windows are docked there
    ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;

    // Submit the dockspace
    ImGui::DockSpace(m_dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

    ImGui::End();

    // Ensure the nvim dock node never shows a close button, even when
    // the layout was loaded from persistence (ini) rather than built
    // by DockBuilder.
    _ensure_nvim_no_close_button();

    // Build default layout on first frame if needed
    // We do this after the dockspace is submitted so ImGui has initialized the
    // dock nodes
    if (!_is_persisted_layout_available()) {
        static bool first_frame = true;
        if (first_frame) {
            build_default_layout();
            first_frame = false;
        }
    }
}

void DockSpaceLayout::build_default_layout() {
    if (!m_initialized) {
        LOG_ERROR(
            "Cannot build default layout - DockSpaceLayout not initialized");
        return;
    }

    LOG_INFO("Building default dock layout");
    _build_default_layout_internal();
}

void DockSpaceLayout::_build_default_layout_internal() {
    // Get the central node of our dockspace
    ImGuiDockNode* root_node = ImGui::DockBuilderGetNode(m_dockspace_id);
    if (root_node == nullptr) {
        LOG_ERROR("Failed to get dockspace root node");
        return;
    }

    // Clear any existing layout
    ImGui::DockBuilderRemoveNodeChildNodes(m_dockspace_id);

    // Split the root node vertically: left (20%) and right (80%)
    ImGuiID left_id, right_id;
    ImGui::DockBuilderSplitNode(m_dockspace_id, ImGuiDir_Left,
                                g_default_left_ratio, &left_id, &right_id);

    // Split the right node horizontally: top (70%) and bottom (30%)
    ImGuiID top_right_id, bottom_right_id;
    ImGui::DockBuilderSplitNode(right_id, ImGuiDir_Up, g_default_top_ratio,
                                &top_right_id, &bottom_right_id);

    // Store the dock IDs for each zone
    m_dock_id_left = left_id;
    m_dock_id_right_top = top_right_id;
    m_dock_id_right_bottom = bottom_right_id;

    // Dock each window into its assigned node so that the split layout
    // is populated even when ImGuiCond_FirstUseEver won't re-trigger
    // (e.g. after a reset that doesn't clear .ini persistence).
    ImGui::DockBuilderDockWindow(m_file_tree_window_name.c_str(), left_id);
    ImGui::DockBuilderDockWindow(m_nvim_window_name.c_str(), top_right_id);
    ImGui::DockBuilderDockWindow(m_terminal_window_name.c_str(),
                                 bottom_right_id);
    ImGui::DockBuilderDockWindow(m_output_window_name.c_str(), bottom_right_id);

    // Finish building the dock layout
    ImGui::DockBuilderFinish(m_dockspace_id);

    // Lock the nvim dock node: hide the close button and prevent undocking.
    if (m_dock_id_right_top != 0) {
        ImGuiDockNode* nvim_node =
            ImGui::DockBuilderGetNode(m_dock_id_right_top);
        if (nvim_node != nullptr) {
            nvim_node->LocalFlags |= g_nvim_lock_flags;
            nvim_node->UpdateMergedFlags();
        }
    }

    LOG_INFO("Default layout built - Left: {}, TopRight: {}, BottomRight: {}",
             m_dock_id_left, m_dock_id_right_top, m_dock_id_right_bottom);
}

void DockSpaceLayout::build_layout(bool show_file_tree, bool show_bottom_dock) {
    if (!m_initialized) {
        LOG_ERROR("Cannot build layout - DockSpaceLayout not initialized");
        return;
    }

    // Clear any existing layout first.
    ImGui::DockBuilderRemoveNodeChildNodes(m_dockspace_id);

    if (!show_file_tree && !show_bottom_dock) {
        // --- Nvim only (no splits) ---
        ImGui::DockBuilderDockWindow(m_nvim_window_name.c_str(),
                                     m_dockspace_id);
        ImGui::DockBuilderFinish(m_dockspace_id);
        m_dock_id_left = 0;
        m_dock_id_right_top = m_dockspace_id;
        m_dock_id_right_bottom = 0;

        // Lock the nvim dock node: hide close button, prevent undocking.
        if (m_dock_id_right_top != 0) {
            ImGuiDockNode* nvim_node =
                ImGui::DockBuilderGetNode(m_dock_id_right_top);
            if (nvim_node != nullptr) {
                nvim_node->LocalFlags |= g_nvim_lock_flags;
                nvim_node->UpdateMergedFlags();
            }
        }

        LOG_INFO("Layout built: nvim-only");

    } else if (show_file_tree && !show_bottom_dock) {
        // --- File tree (left) + nvim (right) ---
        ImGuiID left_id, right_id;
        ImGui::DockBuilderSplitNode(m_dockspace_id, ImGuiDir_Left,
                                    g_default_left_ratio, &left_id, &right_id);
        ImGui::DockBuilderDockWindow(m_file_tree_window_name.c_str(), left_id);
        ImGui::DockBuilderDockWindow(m_nvim_window_name.c_str(), right_id);
        ImGui::DockBuilderFinish(m_dockspace_id);
        m_dock_id_left = left_id;
        m_dock_id_right_top = right_id;
        m_dock_id_right_bottom = 0;

        // Lock the nvim dock node: hide close button, prevent undocking.
        if (m_dock_id_right_top != 0) {
            ImGuiDockNode* nvim_node =
                ImGui::DockBuilderGetNode(m_dock_id_right_top);
            if (nvim_node != nullptr) {
                nvim_node->LocalFlags |= g_nvim_lock_flags;
                nvim_node->UpdateMergedFlags();
            }
        }

        LOG_INFO("Layout built: file-tree + nvim");

    } else if (!show_file_tree && show_bottom_dock) {
        // --- Nvim (top) + terminal / output (bottom, tabbed) ---
        ImGuiID top_id, bottom_id;
        ImGui::DockBuilderSplitNode(m_dockspace_id, ImGuiDir_Up,
                                    g_default_top_ratio, &top_id, &bottom_id);
        ImGui::DockBuilderDockWindow(m_nvim_window_name.c_str(), top_id);
        ImGui::DockBuilderDockWindow(m_terminal_window_name.c_str(), bottom_id);
        ImGui::DockBuilderDockWindow(m_output_window_name.c_str(), bottom_id);
        ImGui::DockBuilderFinish(m_dockspace_id);
        m_dock_id_left = 0;
        m_dock_id_right_top = top_id;
        m_dock_id_right_bottom = bottom_id;

        // Lock the nvim dock node: hide close button, prevent undocking.
        if (m_dock_id_right_top != 0) {
            ImGuiDockNode* nvim_node =
                ImGui::DockBuilderGetNode(m_dock_id_right_top);
            if (nvim_node != nullptr) {
                nvim_node->LocalFlags |= g_nvim_lock_flags;
                nvim_node->UpdateMergedFlags();
            }
        }

        LOG_INFO("Layout built: nvim + bottom dock");

    } else {
        // --- Full 3-way split ---
        _build_default_layout_internal();
    }
}

void DockSpaceLayout::_clear_dock_nodes() {
    if (m_dockspace_id != 0) {
        ImGui::DockBuilderRemoveNodeChildNodes(m_dockspace_id);
    }
    m_dock_id_left = 0;
    m_dock_id_right_top = 0;
    m_dock_id_right_bottom = 0;
}

bool DockSpaceLayout::_is_persisted_layout_available() const {
    // Check if ImGui has any docking settings for our dockspace
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (ctx == nullptr) {
        return false;
    }

    // Look for our dockspace in the node table
    for (int i = 0; i < ctx->DockContext.Nodes.Data.Size; i++) {
        ImGuiDockNode* node =
            static_cast<ImGuiDockNode*>(ctx->DockContext.Nodes.Data[i].val_p);
        if (node != nullptr && node->ID == m_dockspace_id) {
            // Check if this node has children (meaning a layout was built)
            return node->ChildNodes[0] != nullptr ||
                   node->ChildNodes[1] != nullptr;
        }
    }

    return false;
}

void DockSpaceLayout::reset_to_default(bool clear_persistence) {
    LOG_INFO("Resetting layout to default (clear_persistence={})",
             clear_persistence);

    if (clear_persistence) {
        // Clear ImGui's docking settings from the ini file
        ImGui::ClearIniSettings();
        LOG_INFO("Cleared ImGui ini settings");
    }

    // Clear current dock nodes and rebuild default layout
    _clear_dock_nodes();
    _build_default_layout_internal();
}

ImGuiID DockSpaceLayout::get_dock_id_for_zone(Zone zone) const {
    switch (zone) {
    case Zone::FileTree:
        return m_dock_id_left;
    case Zone::Nvim:
        return m_dock_id_right_top;
    case Zone::Terminal:
        return m_dock_id_right_bottom;
    case Zone::Output:
        return m_dock_id_right_bottom;
    default:
        return 0;
    }
}

bool DockSpaceLayout::is_active_tab_in_bottom_dock(
    const std::string& window_name) const {
    if (m_dock_id_right_bottom == 0) {
        return false;
    }

    ImGuiDockNode* node = ImGui::DockBuilderGetNode(m_dock_id_right_bottom);
    if (node == nullptr) {
        return false;
    }

    ImGuiWindow* win = ImGui::FindWindowByName(window_name.c_str());
    if (win == nullptr) {
        return false;
    }

    return node->SelectedTabId == win->TabId;
}

void DockSpaceLayout::_ensure_nvim_no_close_button() {
    ImGuiID node_id = m_dock_id_right_top;

    // If we don't have a cached nvim node ID (e.g. layout loaded from
    // a persisted ini that predates this feature), try to discover it
    // by finding the nvim window via its last-known name.
    if (node_id == 0) {
        ImGuiWindow* nvim_win =
            ImGui::FindWindowByName(m_nvim_window_name.c_str());
        if (nvim_win != nullptr && nvim_win->DockNode != nullptr) {
            node_id = nvim_win->DockNode->ID;
            m_dock_id_right_top = node_id; // Cache for subsequent frames
        }
    }

    if (node_id == 0) {
        return; // Nvim window not yet docked — try again next frame
    }

    ImGuiDockNode* node = ImGui::DockBuilderGetNode(node_id);
    if (node != nullptr) {
        node->LocalFlags |= g_nvim_lock_flags;
        node->UpdateMergedFlags();
    }
}

} // namespace ImNeovim
