#include "im_neovim/workspace.h"
#include "im_neovim/logging.h"
#include <algorithm>
#include <im_app/file_system.h>

namespace ImNeovim {

void Workspace::add_folder(const std::filesystem::path& path) {
    if (path.empty()) {
        return;
    }

    std::filesystem::path abs_path = std::filesystem::weakly_canonical(path);

    // Validate that the path exists and is a directory.
    std::error_code ec;
    if (!std::filesystem::is_directory(abs_path, ec)) {
        LOG_WARN("Workspace: ignoring non-directory path: {}",
                 ImApp::path_to_string(abs_path));
        return;
    }

    // Skip duplicates.
    auto it = std::find(m_folders.begin(), m_folders.end(), abs_path);
    if (it != m_folders.end()) {
        return;
    }

    m_folders.push_back(abs_path);
    LOG_INFO("Workspace: added folder: {}", ImApp::path_to_string(abs_path));
    on_changed.emit();
}

void Workspace::remove_folder(const std::filesystem::path& path) {
    std::filesystem::path abs_path = std::filesystem::weakly_canonical(path);

    auto it = std::find(m_folders.begin(), m_folders.end(), abs_path);
    if (it == m_folders.end()) {
        return;
    }

    m_folders.erase(it);
    LOG_INFO("Workspace: removed folder: {}", ImApp::path_to_string(abs_path));
    on_changed.emit();
}

void Workspace::replace_with(const std::filesystem::path& path) {
    if (path.empty()) {
        return;
    }

    std::filesystem::path abs_path = std::filesystem::weakly_canonical(path);

    std::error_code ec;
    if (!std::filesystem::is_directory(abs_path, ec)) {
        LOG_WARN("Workspace: ignoring non-directory path: {}",
                 ImApp::path_to_string(abs_path));
        return;
    }

    // If the workspace already contains exactly this single folder,
    // there is nothing to do.
    if (m_folders.size() == 1 && m_folders.front() == abs_path) {
        return;
    }

    m_folders.clear();
    m_folders.push_back(abs_path);
    LOG_INFO("Workspace: replaced with folder: {}",
             ImApp::path_to_string(abs_path));
    on_changed.emit();
}

std::filesystem::path Workspace::first_folder_or_home() const {
    if (!m_folders.empty()) {
        return m_folders.front();
    }
    return ImApp::FileSystem::home_directory();
}

std::string Workspace::derive_instance_key() const {
    if (m_folders.empty()) {
        return {};
    }

    // Concatenate all absolute folder paths with "ImNeovim:" prefix,
    // then hash the result to produce a stable instance key.
    std::string input = "ImNeovim:";
    for (const auto& f : m_folders) {
        input += ImApp::path_to_string(f);
    }

    auto hash = std::hash<std::string>{}(input);
    return std::to_string(hash);
}

} // namespace ImNeovim
