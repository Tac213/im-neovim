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

namespace ImNeovim {
class LayerMainWindow : public ImApp::Layer {
  public:
    LayerMainWindow();

    void on_attach() override;
    void on_update() override;
    void on_imgui_render() override;

    /// Blocks exit if nvim has unsaved buffers and shows a save modal.
    bool on_exit_requested() override;

  private:
    std::shared_ptr<Terminal> m_terminal{nullptr};
    std::shared_ptr<NvimWidget> m_nvim{nullptr};
    std::shared_ptr<FileTreeWidget> m_file_tree{nullptr};
    std::shared_ptr<DockSpaceLayout> m_dock_layout{nullptr};

    // Exit modal state
    bool m_exit_modal_active{false};

    // About panel
    AboutPanel m_about_panel;

    void _show_exit_modal();
    void _render_exit_modal();
    void _handle_exit_decision(bool save, bool discard);
};
} // namespace ImNeovim
