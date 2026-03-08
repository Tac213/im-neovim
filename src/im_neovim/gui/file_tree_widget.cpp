#include "im_neovim/gui/file_tree_widget.h"
#include "im_neovim/gui/nvim_widget.h"
#include "im_neovim/logging.h"
#include <algorithm>
#include <imgui.h>

namespace ImNeovim {

FileTreeWidget::FileTreeWidget()
    : m_needs_refresh(true), m_watch_handle(nullptr) {
    m_current_dir = std::filesystem::current_path();
    m_root_entry.path = m_current_dir;
    m_root_entry.is_directory = true;
    m_root_entry.is_expanded = true;
}

FileTreeWidget::~FileTreeWidget() {
    _stop_watching();
    m_nvim_widget.reset();
}

void FileTreeWidget::set_current_directory(const std::filesystem::path& path) {
    if (std::filesystem::is_directory(path)) {
        m_current_dir = path;
        m_root_entry.path = path;
        m_root_entry.children.clear();
        m_needs_refresh = true;
        _stop_watching();
        _start_watching();
    }
}

void FileTreeWidget::_scan_directory(DirectoryEntry& entry) {
    entry.children.clear();

    try {
        if (!std::filesystem::is_directory(entry.path)) {
            return;
        }

        // First pass: collect directories and files
        std::vector<DirectoryEntry> directories;
        std::vector<DirectoryEntry> files;

        for (const auto& dir_entry :
             std::filesystem::directory_iterator(entry.path)) {
            try {
                DirectoryEntry child;
                child.path = dir_entry.path();
                child.is_directory = dir_entry.is_directory();
                child.is_expanded = false;

                if (child.is_directory) {
                    directories.push_back(child);
                } else {
                    files.push_back(child);
                }
            } catch (const std::filesystem::filesystem_error& e) {
                LOG_WARN("Error accessing file: {}", e.what());
                // Continue with other entries
            }
        }

        // Sort directories and files alphabetically by filename
        auto compare_by_filename = [](const DirectoryEntry& a,
                                      const DirectoryEntry& b) {
            return a.path.filename().string() < b.path.filename().string();
        };

        std::sort(directories.begin(), directories.end(), compare_by_filename);
        std::sort(files.begin(), files.end(), compare_by_filename);

        // Add directories first, then files
        entry.children.reserve(directories.size() + files.size());
        entry.children.insert(entry.children.end(), directories.begin(),
                              directories.end());
        entry.children.insert(entry.children.end(), files.begin(), files.end());

    } catch (const std::filesystem::filesystem_error& e) {
        LOG_ERROR("Error scanning directory '{}': {}", entry.path.string(),
                  e.what());
    } catch (const std::exception& e) {
        LOG_ERROR("Unexpected error scanning directory '{}': {}",
                  entry.path.string(), e.what());
    }
}

void FileTreeWidget::_refresh_current_directory() {
    _scan_directory(m_root_entry);
    m_needs_refresh = false;
}

void FileTreeWidget::_start_watching() {
    // Platform-specific implementation would go here
    // For now, we'll use periodic polling as a fallback
    m_watch_handle = nullptr;
}

void FileTreeWidget::_stop_watching() { m_watch_handle = nullptr; }

void FileTreeWidget::_handle_file_system_changes() {
    // Check if directory has changed
    if (m_needs_refresh) {
        _refresh_current_directory();
    }
}

void FileTreeWidget::_render_entry(DirectoryEntry& entry, int depth) {
    if (entry.is_directory) {
        _render_directory_node(entry, depth);
    } else {
        _render_file_node(entry, depth);
    }
}

void FileTreeWidget::_render_directory_node(DirectoryEntry& entry, int depth) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth;

    if (entry.is_expanded) {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }

    std::string filename = entry.path.filename().string();
    if (filename.empty()) {
        filename = entry.path.string(); // For root, use full path
    }

    bool node_open = ImGui::TreeNodeEx(filename.c_str(), flags);

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        _on_item_clicked(entry.path, true);
    }

    if (node_open) {
        if (!entry.is_expanded) {
            // First time expanding - scan the directory
            _scan_directory(entry);
            entry.is_expanded = true;
        }

        // Render children
        for (auto& child : entry.children) {
            _render_entry(child, depth + 1);
        }

        ImGui::TreePop();
    } else if (entry.is_expanded) {
        // Collapsed - clear children to save memory
        entry.is_expanded = false;
        entry.children.clear();
    }
}

void FileTreeWidget::_render_file_node(DirectoryEntry& entry, int depth) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf |
                               ImGuiTreeNodeFlags_NoTreePushOnOpen |
                               ImGuiTreeNodeFlags_SpanAvailWidth;

    std::string filename = entry.path.filename().string();

    ImGui::TreeNodeEx(filename.c_str(), flags);

    if (ImGui::IsItemClicked()) {
        _on_item_clicked(entry.path, false);
    }
}

void FileTreeWidget::_on_item_clicked(const std::filesystem::path& path,
                                      bool is_directory) {
    if (is_directory) {
        _toggle_expand(m_root_entry); // This will be called from within
                                      // _render_directory_node
    } else {
        _open_file_in_nvim(path);
    }
}

void FileTreeWidget::_toggle_expand(DirectoryEntry& entry) {
    entry.is_expanded = !entry.is_expanded;
    if (entry.is_expanded && entry.children.empty()) {
        _scan_directory(entry);
    }
}

void FileTreeWidget::_open_file_in_nvim(const std::filesystem::path& path) {
    if (m_nvim_widget) {
        // Convert path to string
        std::string path_str = path.string();

        // Use NvimWidget to open the file
        m_nvim_widget->open_file(path_str);
        LOG_INFO("Opening file in nvim: {}", path_str);
    }
}

void FileTreeWidget::render() {
    if (!m_is_visible) {
        return;
    }

    // Handle any file system changes
    _handle_file_system_changes();

    // Initial scan if needed
    if (m_needs_refresh) {
        _refresh_current_directory();
    }

    // Set up window
    ImGui::SetNextWindowPos(m_window_pos, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(m_window_size, ImGuiCond_FirstUseEver);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin(m_window_title.c_str(), &m_is_visible, window_flags)) {
        // Update window position and size for persistence
        m_window_pos = ImGui::GetWindowPos();
        m_window_size = ImGui::GetWindowSize();

        // Render the file tree
        if (!std::filesystem::exists(m_current_dir)) {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f),
                               "Directory not accessible: %s",
                               m_current_dir.string().c_str());
        } else if (!m_root_entry.children.empty() || m_root_entry.is_expanded) {
            // Render children of root
            for (auto& child : m_root_entry.children) {
                _render_entry(child, 0);
            }
        } else {
            ImGui::Text("Empty directory");
        }
    }

    ImGui::End();
}

} // namespace ImNeovim
