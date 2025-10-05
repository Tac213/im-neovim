#pragma once

#include "im_app/imgui_renderer.h"

namespace ImApp {
class D3D12ImGuiRenderer : public ImGuiRenderer {
  public:
    D3D12ImGuiRenderer();
    virtual ~D3D12ImGuiRenderer() override;
    virtual void new_frame() override;
    virtual void render(std::shared_ptr<Window>& window) override;

  private:
    void _initialize();
    void _finalize();
};
} // namespace ImApp
