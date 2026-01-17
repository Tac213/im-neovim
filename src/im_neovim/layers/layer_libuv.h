#pragma once

#include <im_app/layer.h>
#include <uv.h>

namespace ImNeovim {
class LayerLibuv final : public ImApp::Layer {
  public:
    void on_attach() override;
    void on_update() override;
    void on_detach() override;

  private:
    uv_loop_t* m_loop = nullptr;
};
} // namespace ImNeovim
