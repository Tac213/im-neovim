#pragma once

#include <imgui.h>

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

  private:
    bool m_initialized{false};
    bool m_pending_reset{false};
    ImGuiID m_dockspace_id{0};
    ImGuiViewport* m_main_viewport{nullptr};

    // Dock node IDs for each zone
    ImGuiID m_dock_id_left{0};         // FileTree
    ImGuiID m_dock_id_right_top{0};    // Nvim
    ImGuiID m_dock_id_right_bottom{0}; // Terminal

    // Default layout ratios
    static constexpr float g_default_left_ratio = 0.20f;
    static constexpr float g_default_top_ratio = 0.70f;

    // Persistent dockspace window name
    static constexpr const char* g_dockspace_window_name = "MainDockSpace";

    void _build_default_layout_internal();
    void _clear_dock_nodes();
    bool _is_persisted_layout_available() const;
};

} // namespace ImNeovim
