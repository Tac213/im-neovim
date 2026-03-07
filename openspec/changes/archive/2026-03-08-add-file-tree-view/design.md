# File Tree View Design

## Overview

The file tree view is a new UI component that displays the contents of the current directory in a floating, resizable window. It supports basic navigation (expand/collapse folders) and file opening functionality.

## Architecture

### Widget Structure

The file tree widget will be a standalone class following the existing widget patterns:

```cpp
class FileTreeWidget {
public:
    FileTreeWidget();
    ~FileTreeWidget();

    void render();

    const std::string& window_title() const { return m_window_title; }
    void set_window_title(const std::string& title) { m_window_title = title; }
    bool is_visible() const { return m_is_visible; }
    void set_visible(bool visible) { m_is_visible = visible; }

private:
    // Directory traversal
    void _scan_current_directory();
    void _toggle_directory(const std::filesystem::path& path);

    // File system watching
    void _start_watching();
    void _stop_watching();
    void _handle_file_system_changes();

    // Rendering helpers
    void _render_directory_item(const std::filesystem::path& path, bool is_expanded);
    void _render_file_item(const std::filesystem::path& path);

    // Event handlers
    void _on_item_clicked(const std::filesystem::path& path);

    // Window management
    bool _setup_window();

    // Data structures
    struct DirectoryNode {
        std::filesystem::path path;
        bool is_expanded;
        std::vector<std::filesystem::path> children;
    };

    std::filesystem::path m_current_dir;
    std::vector<DirectoryNode> m_directory_nodes;
    std::vector<std::filesystem::path> m_files;

    // File system watcher (platform-specific)
    void* m_watch_handle;

    // State tracking
    bool m_needs_refresh;

    // Window state
    std::string m_window_title;
    bool m_is_visible;
    bool m_is_embedded;
    ImVec2 m_embedded_window_pos;
    ImVec2 m_embedded_window_size;
    bool m_embedded_window_collapsed;
};
```

### Integration with Neovim

The widget will interact with NvimWidget to open files:

```cpp
void FileTreeWidget::_on_item_clicked(const std::filesystem::path& path) {
    if (std::filesystem::is_regular_file(path)) {
        // Open file in Neovim
        m_nvim_widget->open_file(path.string());
    } else if (std::filesystem::is_directory(path)) {
        // Toggle expand/collapse
        _toggle_directory(path);
    }
}
```

## Implementation Details

### Directory Traversal

Use `std::filesystem` to scan the current directory and sort entries:

```cpp
void FileTreeWidget::_scan_current_directory() {
    m_directory_nodes.clear();
    m_files.clear();

    for (const auto& entry : std::filesystem::directory_iterator(m_current_dir)) {
        if (entry.is_directory()) {
            m_directory_nodes.push_back({entry.path(), false, {}});
        } else if (entry.is_regular_file()) {
            m_files.push_back(entry.path());
        }
    }

    // Sort entries: directories first, then files, alphabetically
    std::sort(m_directory_nodes.begin(), m_directory_nodes.end(),
        [](const DirectoryNode& a, const DirectoryNode& b) {
            return a.path.filename().string() < b.path.filename().string();
        });

    std::sort(m_files.begin(), m_files.end(),
        [](const std::filesystem::path& a, const std::filesystem::path& b) {
            return a.filename().string() < b.filename().string();
        });
}
```

### File System Watching

Implement platform-specific watching:

- **Windows**: Use ReadDirectoryChangesW
- **Linux**: Use inotify
- **macOS**: Use FSEvents

### Rendering

Use ImGui's TreeNode to display expandable folders:

```cpp
void FileTreeWidget::_render_directory_item(const std::filesystem::path& path, bool is_expanded) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (is_expanded) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    bool expanded = ImGui::TreeNodeEx(path.filename().string().c_str(), flags);

    if (ImGui::IsItemClicked()) {
        _on_item_clicked(path);
    }

    if (expanded) {
        _render_children(path);
        ImGui::TreePop();
    }
}
```

### Error Handling

Handle common file system errors:

```cpp
void FileTreeWidget::_scan_current_directory() {
    try {
        // Directory scanning code here
    } catch (const std::filesystem::filesystem_error& e) {
        LOG_ERROR("Error scanning directory: {}", e.what());
        m_directory_nodes.clear();
        m_files.clear();
    }
}
```

## Cross-Platform Considerations

- Always use `std::filesystem::path` for file operations to ensure portability
- Implement platform-specific file watching using conditional compilation
- Handle path separators correctly using `std::filesystem` utilities

## Dependencies

- ImGui for UI rendering
- `std::filesystem` for directory traversal
- Platform-specific APIs for file system watching
