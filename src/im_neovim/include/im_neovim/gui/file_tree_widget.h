#pragma once

#include <filesystem>
#include <functional>
#include <imgui.h>
#include <memory>
#include <string>
#include <vector>

namespace ImNeovim {

// Forward declaration
class NvimWidget;

class FileTreeWidget {
  public:
    FileTreeWidget();
    ~FileTreeWidget();

    void render();

    const std::string& window_title() const { return m_window_title; }
    void set_window_title(const std::string& title) { m_window_title = title; }
    bool is_visible() const { return m_is_visible; }
    void set_visible(bool visible) { m_is_visible = visible; }

    void set_nvim_widget(std::shared_ptr<NvimWidget> nvim) {
        m_nvim_widget = nvim;
    }
    void set_current_directory(const std::filesystem::path& path);

    // Docking support
    void set_dock_id(ImGuiID dock_id) { m_dock_id = dock_id; }
    ImGuiID get_dock_id() const { return m_dock_id; }

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
    void _refresh_current_directory();

    // File system watching
    void _start_watching();
    void _stop_watching();
    void _handle_file_system_changes();

    // Rendering helpers
    void _render_entry(DirectoryEntry& entry, int depth);
    void _render_directory_node(DirectoryEntry& entry, int depth);
    void _render_file_node(DirectoryEntry& entry, int depth);

    // Event handlers
    void _on_item_clicked(const std::filesystem::path& path, bool is_directory);
    void _toggle_expand(DirectoryEntry& entry);
    void _open_file_in_nvim(const std::filesystem::path& path);

    // Data
    std::filesystem::path m_current_dir;
    DirectoryEntry m_root_entry;
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

    // Reference to NvimWidget for opening files
    std::shared_ptr<NvimWidget> m_nvim_widget;
};

} // namespace ImNeovim
