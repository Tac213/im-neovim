#include "im_neovim/gui/file_tree_widget.h"
#include "im_neovim/globals.h"
#include "im_neovim/logging.h"
#include <algorithm>
#include <im_app/file_system.h>
#include <imgui.h>

namespace ImNeovim {
using ImApp::path_to_string;

FileTreeWidget::FileTreeWidget()
    : m_needs_refresh(true), m_watch_handle(nullptr) {
    _rebuild_root_entries();

    // React to workspace changes — defer rebuild to avoid
    // iterator invalidation during rendering.
    m_workspace_conn = g_workspace.on_changed.connect(
        std::bind_front(&FileTreeWidget::_on_workspace_changed, this));
}

FileTreeWidget::~FileTreeWidget() {
    _stop_watching();
    if (m_workspace_conn != 0) {
        g_workspace.on_changed.disconnect(m_workspace_conn);
    }
}

void FileTreeWidget::_rebuild_root_entries() {
    m_root_entries.clear();
    for (const auto& folder : g_workspace.folders()) {
        DirectoryEntry entry;
        entry.path = folder;
        entry.is_directory = true;
        entry.is_expanded = true;
        _scan_directory(entry);
        m_root_entries.push_back(std::move(entry));
    }
    m_needs_refresh = false;
}

void FileTreeWidget::
    _scan_directory( // NOLINT(readability-convert-member-functions-to-static)
        DirectoryEntry& entry) {
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
            }
        }

        // Sort directories and files alphabetically by filename
        auto compare_by_filename = [](const DirectoryEntry& a,
                                      const DirectoryEntry& b) {
            return path_to_string(a.path.filename()) <
                   path_to_string(b.path.filename());
        };

        std::sort(directories.begin(), directories.end(), compare_by_filename);
        std::sort(files.begin(), files.end(), compare_by_filename);

        // Add directories first, then files
        entry.children.reserve(directories.size() + files.size());
        entry.children.insert(entry.children.end(), directories.begin(),
                              directories.end());
        entry.children.insert(entry.children.end(), files.begin(), files.end());

    } catch (const std::filesystem::filesystem_error& e) {
        LOG_ERROR("Error scanning directory '{}': {}",
                  path_to_string(entry.path), e.what());
    } catch (const std::exception& e) {
        LOG_ERROR("Unexpected error scanning directory '{}': {}",
                  path_to_string(entry.path), e.what());
    }
}

void FileTreeWidget::_start_watching() { m_watch_handle = nullptr; }

void FileTreeWidget::_stop_watching() { m_watch_handle = nullptr; }

void FileTreeWidget::_handle_file_system_changes() {
    if (m_needs_refresh) {
        _rebuild_root_entries();
    }
}

void FileTreeWidget::_render_entry(DirectoryEntry& entry, int depth,
                                   bool is_root) {
    if (entry.is_directory) {
        _render_directory_node(entry, depth, is_root);
    } else {
        _render_file_node(entry, depth);
    }
}

void FileTreeWidget::_render_directory_node(DirectoryEntry& entry, int depth,
                                            bool is_root) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth;

    if (entry.is_expanded) {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }

    std::string filename;
    if (is_root) {
        auto base = path_to_string(entry.path.filename());
        // Only show full path when basenames collide across roots.
        bool duplicate = false;
        for (const auto& other : m_root_entries) {
            if (&other != &entry &&
                path_to_string(other.path.filename()) == base) {
                duplicate = true;
                break;
            }
        }
        filename = duplicate ? path_to_string(entry.path) : base;
        if (filename.empty()) {
            filename = path_to_string(entry.path);
        }
    } else {
        filename = path_to_string(entry.path.filename());
        if (filename.empty()) {
            filename = path_to_string(entry.path);
        }
    }

    bool node_open = ImGui::TreeNodeEx(filename.c_str(), flags);

    // Context menu for root nodes when there are multiple workspace folders.
    if (is_root && m_root_entries.size() > 1) {
        ImGui::PushID(path_to_string(entry.path).c_str());
        if (ImGui::BeginPopupContextItem("RootFolderContextMenu")) {
            if (ImGui::MenuItem("Remove Folder from Workspace")) {
                on_remove_folder_requested.emit(entry.path);
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        _on_item_clicked(entry.path, true);
    }

    if (node_open) {
        if (!entry.is_expanded) {
            _scan_directory(entry);
            entry.is_expanded = true;
        }

        for (auto& child : entry.children) {
            _render_entry(child, depth + 1);
        }

        ImGui::TreePop();
    } else if (entry.is_expanded) {
        entry.is_expanded = false;
        entry.children.clear();
    }
}

void FileTreeWidget::_render_file_node(DirectoryEntry& entry, int depth) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf |
                               ImGuiTreeNodeFlags_NoTreePushOnOpen |
                               ImGuiTreeNodeFlags_SpanAvailWidth;

    std::string filename = path_to_string(entry.path.filename());

    ImGui::TreeNodeEx(filename.c_str(), flags);

    if (ImGui::IsItemClicked()) {
        _on_item_clicked(entry.path, false);
    }
}

void FileTreeWidget::_on_item_clicked(const std::filesystem::path& path,
                                      bool is_directory) {
    if (is_directory) {
        // Find and toggle the entry (search all roots and their children).
        for (auto& root : m_root_entries) {
            if (root.path == path) {
                _toggle_expand(root);
                return;
            }
        }
    } else {
        file_clicked.emit(path);
    }
}

void FileTreeWidget::_toggle_expand(DirectoryEntry& entry) {
    entry.is_expanded = !entry.is_expanded;
    if (entry.is_expanded && entry.children.empty()) {
        _scan_directory(entry);
    }
}

void FileTreeWidget::render() {
    if (!m_is_visible) {
        return;
    }

    _handle_file_system_changes();

    // Set up window flags for docking
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoCollapse;

    if (m_dock_id != 0) {
        ImGui::SetNextWindowDockID(m_dock_id, ImGuiCond_FirstUseEver);
    } else {
        ImGui::SetNextWindowPos(m_window_pos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(m_window_size, ImGuiCond_FirstUseEver);
    }

    if (ImGui::Begin(m_window_title.c_str(), &m_is_visible, window_flags)) {
        m_window_pos = ImGui::GetWindowPos();
        m_window_size = ImGui::GetWindowSize();

        if (m_root_entries.empty()) {
            // Empty workspace state.
            ImGui::Text("You have not yet opened a folder.");
            ImGui::Spacing();
            if (ImGui::Button("Open Folder")) {
                on_open_folder_requested.emit();
            }
        } else if (m_root_entries.size() == 1) {
            // Single folder — render children directly (no root node).
            auto& root_entry = m_root_entries[0];
            for (auto& child : root_entry.children) {
                _render_entry(child, 0);
            }
        } else {
            // Multiple folders — show each as a top-level tree node.
            for (auto& root_entry : m_root_entries) {
                _render_entry(root_entry, 0, /*is_root=*/true);
            }
        }
    }

    ImGui::End();
}

void FileTreeWidget::_on_workspace_changed() { m_needs_refresh = true; }

} // namespace ImNeovim
