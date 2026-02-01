#pragma once

#include <im_app/layer.h>

namespace ImNeovim {
class LayerLibuv final : public ImApp::Layer {
  public:
    ~LayerLibuv();
    void on_attach() override;
    void on_update() override;
};
} // namespace ImNeovim
