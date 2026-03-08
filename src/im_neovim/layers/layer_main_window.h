#pragma once
// clang-format off
#include "im_neovim/gui/terminal.h"
#include "im_neovim/gui/nvim_widget.h"
#include "im_neovim/gui/file_tree_widget.h"
// clang-format on
#include <im_app/layer.h>
#include <memory>

namespace ImNeovim {
class LayerMainWindow : public ImApp::Layer {
  public:
    LayerMainWindow();

    void on_attach() override;
    void on_imgui_render() override;

  private:
    std::shared_ptr<Terminal> m_terminal{nullptr};
    std::shared_ptr<NvimWidget> m_nvim{nullptr};
    std::shared_ptr<FileTreeWidget> m_file_tree{nullptr};
};
} // namespace ImNeovim
