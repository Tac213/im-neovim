#pragma once
// clang-format off
#include "im_neovim/gui/terminal.h"
#include "im_neovim/gui/nvim_widget.h"
#include "im_neovim/gui/file_tree_widget.h"
#include "im_neovim/gui/dock_space_layout.h"
#include "im_neovim/gui/about_panel.h"
// clang-format on
#include <im_app/layer.h>
#include <memory>
#include <optional>

namespace ImNeovim {
class LayerInstanceManager;

class LayerMainWindow : public ImApp::Layer {
  public:
    LayerMainWindow();

    void on_attach() override;
    void on_update() override;
    void on_imgui_render() override;

    /// Blocks exit if nvim has unsaved buffers and shows a save modal.
    bool on_exit_requested() override;

    /// Set the instance manager for per-folder single-instance locking.
    void set_instance_manager(std::weak_ptr<LayerInstanceManager> mgr) {
        m_instance_manager = std::move(mgr);
    }

  private:
    std::shared_ptr<Terminal> m_terminal{nullptr};
    std::shared_ptr<NvimWidget> m_nvim{nullptr};
    std::shared_ptr<FileTreeWidget> m_file_tree{nullptr};
    std::shared_ptr<DockSpaceLayout> m_dock_layout{nullptr};

    // Exit modal state
    bool m_exit_modal_active{false};

    // About panel
    std::shared_ptr<AboutPanel> m_about_panel;

    // Manual visibility overrides from View > File Tree / Terminal toggles.
    // nullopt = auto (follow workspace); true/false = forced.
    std::optional<bool> m_file_tree_forced_visible;
    std::optional<bool> m_terminal_forced_visible;

    // Per-folder single-instance manager (weak — owned by layer stack).
    std::weak_ptr<LayerInstanceManager> m_instance_manager;

    void _show_exit_modal();
    void _render_exit_modal();
    void _handle_exit_decision(bool save, bool discard);
    void _assign_dock_ids();

    // --- Signal handlers ---

    /// File tree: open a clicked file in nvim.
    void _on_file_clicked(const std::filesystem::path& path);

    /// File tree: remove a workspace folder (right-click action).
    void _on_remove_folder(const std::filesystem::path& path);

    /// File > Exit / native Close Window.
    void _on_exit();

    /// File > New Window: spawn a detached imnvim process.
    void _on_new_window();

    /// Help > About: show the about panel.
    void _on_about();

    /// File > Open Folder...: replace workspace with a picked folder.
    void _open_folder();

    /// File > Add Folder to Workspace...: append a picked folder.
    void _add_folder_to_workspace();

    /// Keep nvim's :cd in sync with the first workspace folder.
    void _sync_nvim_cwd();

    /// Update file-tree / terminal visibility and menu checkmarks.
    void _update_panel_visibility();

    /// View > File Tree toggle.
    void _toggle_file_tree();

    /// View > Terminal toggle.
    void _toggle_terminal();

    /// View > Reset Layout (native menu path).
    void _on_reset_layout();
};
} // namespace ImNeovim
