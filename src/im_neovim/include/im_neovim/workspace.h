#pragma once

#include "im_neovim/signal.h"
#include <filesystem>
#include <string>
#include <vector>

namespace ImNeovim {

/// Manages the workspace — a list of zero or more folder paths.
///
/// Declared as a global so every component can access it directly
/// without constructor threading or setter injection.
class Workspace {
  public:
    /// Add a folder to the workspace.
    /// The path is resolved to an absolute path.  Duplicates and
    /// non-existent / non-directory paths are silently ignored.
    /// Emits on_changed when the list actually changes.
    void add_folder(const std::filesystem::path& path);

    /// Remove a folder from the workspace by exact path match.
    /// Emits on_changed when the list actually changes.
    void remove_folder(const std::filesystem::path& path);

    /// Replace the entire workspace with a single folder.
    /// Emits on_changed at most once.
    void replace_with(const std::filesystem::path& path);

    /// Returns the ordered list of workspace folders (always absolute).
    const std::vector<std::filesystem::path>& folders() const {
        return m_folders;
    }

    /// True when the workspace contains no folders.
    bool empty() const { return m_folders.empty(); }

    /// Number of folders in the workspace.
    size_t size() const { return m_folders.size(); }

    /// First workspace folder, or the user's home directory if empty.
    std::filesystem::path first_folder_or_home() const;

    /// Derive a unique instance-lock key from all folder paths.
    /// Returns an empty string when the workspace is empty (no locking).
    std::string derive_instance_key() const;

    /// Emitted whenever a folder is added or removed.
    Signal<> on_changed;

  private:
    std::vector<std::filesystem::path> m_folders;
};

} // namespace ImNeovim
