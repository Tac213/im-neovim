#pragma once

#include "im_neovim/signal.h"
#include <filesystem>
#include <imgui.h>
#include <string>
#include <vector>

namespace ImNeovim {

class FileTreeWidget {
  public:
    FileTreeWidget();
    ~FileTreeWidget();

    void render();

    const std::string& window_title() const { return m_window_title; }
    void set_window_title(const std::string& title) { m_window_title = title; }
    bool is_visible() const { return m_is_visible; }
    void set_visible(bool visible) { m_is_visible = visible; }

    // Docking support
    void set_dock_id(ImGuiID dock_id) { m_dock_id = dock_id; }
    ImGuiID get_dock_id() const { return m_dock_id; }

    // Signals
    Signal<const std::filesystem::path&> file_clicked;

    /// Emitted when the user clicks "Open Folder" in the empty state.
    Signal<> on_open_folder_requested;

    /// Emitted when the user right-clicks a root folder and chooses
    /// "Remove Folder from Workspace".
    Signal<const std::filesystem::path&> on_remove_folder_requested;

  private:
    // Directory entry structure
    struct DirectoryEntry {
        std::filesystem::path path;
        bool is_directory;
        bool is_expanded;
        std::vector<DirectoryEntry> children;
    };

    // Directory traversal
    void _scan_directory(DirectoryEntry& entry);
    void _rebuild_root_entries();

    // File system watching
    void _start_watching();
    void _stop_watching();
    void _handle_file_system_changes();

    // Rendering helpers
    void _render_entry(DirectoryEntry& entry, int depth, bool is_root = false);
    void _render_directory_node(DirectoryEntry& entry, int depth,
                                bool is_root = false);
    void _render_file_node(DirectoryEntry& entry, int depth);

    // Event handlers
    void _on_item_clicked(const std::filesystem::path& path, bool is_directory);
    void _toggle_expand(DirectoryEntry& entry);

    // Data
    std::vector<DirectoryEntry> m_root_entries;
    bool m_needs_refresh;

    // File system watcher (platform-specific handle)
    void* m_watch_handle;

    // Window state
    std::string m_window_title{"File Tree"};
    bool m_is_visible{true};
    ImVec2 m_window_pos{50.0f, 50.0f};
    ImVec2 m_window_size{250.0f, 400.0f};

    // Docking state
    ImGuiID m_dock_id{0};

    /// React to workspace changes: mark tree for refresh.
    void _on_workspace_changed();

    // Workspace change connection
    uint64_t m_workspace_conn{0};
};

} // namespace ImNeovim
