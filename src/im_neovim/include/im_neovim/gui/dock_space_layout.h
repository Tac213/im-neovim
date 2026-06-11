#pragma once

#include "im_neovim/signal.h"
#include <imgui.h>
#include <string>

namespace ImNeovim {

/**
 * @brief Manages the ImGui DockSpace layout for the main window.
 *
 * This class creates and manages a full-window DockSpace that provides
 * IDE-like docking behavior for the file tree, nvim editor, and terminal.
 * The layout supports persistence via ImGui's ini file system.
 */
class DockSpaceLayout {
  public:
    /**
     * @brief Dock zones for the three main panels.
     */
    enum class Zone {
        FileTree, ///< Left panel for file tree (20% width)
        Nvim,     ///< Top-right panel for nvim editor (70% of right side)
        Terminal  ///< Bottom-right panel for terminal (30% of right side)
    };

    DockSpaceLayout();
    ~DockSpaceLayout();

    /**
     * @brief Initializes the DockSpace with the given main viewport.
     * @param main_viewport The main viewport to host the dockspace.
     */
    void initialize(ImGuiViewport* main_viewport);

    /**
     * @brief Shuts down the DockSpace and cleans up resources.
     */
    void shutdown();

    /**
     * @brief Renders the DockSpace and handles layout.
     * Call this at the beginning of the main window render loop.
     */
    void render();

    /**
     * @brief Builds the default layout (20/80 vertical, 70/30 horizontal).
     * This should be called once after initialization if no persisted layout
     * exists.
     */
    void build_default_layout();

    /**
     * @brief Resets the layout to the default configuration.
     * Clears any user customizations and rebuilds the default layout.
     * @param clear_persistence If true, also clears the persisted layout data.
     */
    void reset_to_default(bool clear_persistence = false);

    /**
     * @brief Gets the dock ID for a specific zone.
     * @param zone The zone to get the dock ID for.
     * @return The ImGuiDockID for the zone.
     */
    ImGuiID get_dock_id_for_zone(Zone zone) const;

    /**
     * @brief Checks if the layout has been initialized.
     * @return True if initialized, false otherwise.
     */
    bool is_initialized() const { return m_initialized; }

    /**
     * @brief Gets the main dockspace ID.
     * @return The root dockspace ID.
     */
    ImGuiID get_dockspace_id() const { return m_dockspace_id; }

    /**
     * @brief Checks if a layout reset is pending.
     * @return True if a reset is pending, false otherwise.
     */
    bool is_reset_pending() const { return m_pending_reset; }

    /**
     * @brief Clears the pending reset flag.
     */
    void clear_reset_pending() { m_pending_reset = false; }

    /**
     * @brief Queues a layout reset for after the current frame.
     *
     * Unlike reset_to_default() which calls ImGui dock builder APIs
     * immediately, this is safe to call from outside the ImGui render
     * phase (e.g. from a native menu action). The reset is performed
     * in the next render call.
     */
    void queue_reset() { m_pending_reset = true; }

    // -- Menu action signals (connected by LayerMainWindow) --

    /// Emitted when File > New Window is clicked.
    Signal<> on_new_window;

    /// Emitted when File > Open Folder is clicked (replaces workspace).
    Signal<> on_open_folder;

    /// Emitted when File > Add Folder to Workspace is clicked.
    Signal<> on_add_folder_to_workspace;

    /// Emitted when File > Exit is clicked.
    Signal<> on_exit;

    /// Emitted when Help > About is clicked.
    Signal<> on_about;

    /// Emitted when View > Reset Layout is clicked.
    Signal<> on_reset_layout;

    /// Emitted when View > File Tree is toggled.
    Signal<> on_toggle_file_tree;

    /// Emitted when View > Terminal is toggled.
    Signal<> on_toggle_terminal;

    /// Checkmark state for the ImGui View menu (non-Darwin).
    /// Set by LayerMainWindow whenever visibility changes.
    bool file_tree_visible{true};

    /// Checkmark state for the ImGui View menu (non-Darwin).
    bool terminal_visible{true};

    /**
     * @brief Sets the window names used by DockBuilderDockWindow when
     * building the default layout. Call before build_default_layout() or
     * reset_to_default().
     */
    void set_window_names(const std::string& file_tree, const std::string& nvim,
                          const std::string& terminal) {
        m_file_tree_window_name = file_tree;
        m_nvim_window_name = nvim;
        m_terminal_window_name = terminal;
    }

    /**
     * @brief Builds a layout matching the given panel visibility.
     *
     * Combines single-panel, two-panel, and three-panel layouts
     * depending on which panels are currently visible.
     *
     * @param show_file_tree If true, include the file tree (left split).
     * @param show_terminal If true, include the terminal (bottom split).
     */
    void build_layout(bool show_file_tree, bool show_terminal);

    /**
     * @brief Queues an adaptive layout rebuild for after the current
     * frame. The rebuild chooses single-panel or 3-way-split based on
     * whether g_workspace has folders.
     */
    void queue_adaptive_rebuild() { m_pending_adaptive_rebuild = true; }
    bool is_adaptive_rebuild_pending() const {
        return m_pending_adaptive_rebuild;
    }
    void clear_adaptive_rebuild_pending() {
        m_pending_adaptive_rebuild = false;
    }

  private:
    bool m_initialized{false};
    bool m_pending_reset{false};
    bool m_pending_adaptive_rebuild{false};
    ImGuiID m_dockspace_id{0};
    ImGuiViewport* m_main_viewport{nullptr};

    // Dock node IDs for each zone
    ImGuiID m_dock_id_left{0};         // FileTree
    ImGuiID m_dock_id_right_top{0};    // Nvim
    ImGuiID m_dock_id_right_bottom{0}; // Terminal

    // Window names for docking (matching widget defaults)
    std::string m_file_tree_window_name{"File Tree"};
    std::string m_nvim_window_name{"nvim (no file)"};
    std::string m_terminal_window_name{"Terminal"};

    // Default layout ratios
    static constexpr float g_default_left_ratio = 0.20f;
    static constexpr float g_default_top_ratio = 0.70f;

    // Persistent dockspace window name
    static constexpr const char* g_dockspace_window_name = "MainDockSpace";

    void _build_default_layout_internal();
    void _clear_dock_nodes();
    bool _is_persisted_layout_available() const;

    /// Ensures ImGuiDockNodeFlags_NoCloseButton is set on the nvim dock node.
    /// Idempotent — safe to call every frame.
    void _ensure_nvim_no_close_button();
};

} // namespace ImNeovim
