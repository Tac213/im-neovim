#include "im_neovim/gui/dock_space_layout.h"

#include "im_neovim/logging.h"
#include <imgui_internal.h>

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
        ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBringToFrontOnFocus;

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

    // Render menu bar
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Reset Layout")) {
                // Queue a reset for after the frame completes
                m_pending_reset = true;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    // Create the actual dockspace
    // ImGuiDockNodeFlags_PassthruCentralNode allows the dockspace to be
    // transparent to input when no windows are docked there
    ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;

    // Submit the dockspace
    ImGui::DockSpace(m_dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

    ImGui::End();

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

    // Set window class for each zone to guide windows to dock there by default
    // This helps with initial docking behavior
    ImGuiWindowClass window_class;
    window_class.DockingAllowUnclassed = true;

    // Finish building the dock layout
    ImGui::DockBuilderFinish(m_dockspace_id);

    LOG_INFO("Default layout built - Left: {}, TopRight: {}, BottomRight: {}",
             m_dock_id_left, m_dock_id_right_top, m_dock_id_right_bottom);
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
    default:
        return 0;
    }
}

} // namespace ImNeovim
