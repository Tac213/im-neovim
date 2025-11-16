#pragma once
#include "darwin_window.h"
#include "im_app/imgui_renderer.h"
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

namespace ImApp {
class DarwinMetalImGuiRenderer : public ImGuiRenderer {
  public:
    explicit DarwinMetalImGuiRenderer(std::shared_ptr<DarwinWindow> window);
    virtual ~DarwinMetalImGuiRenderer() override;
    virtual void new_frame() override;
    virtual void render(std::shared_ptr<Window>& window) override;

  private:
    MTLRenderPassDescriptor* m_render_pass_descriptor;
    std::shared_ptr<DarwinWindow> m_window;

    id<CAMetalDrawable> m_drawable;
    id<MTLCommandBuffer> m_command_buffer;
    id<MTLRenderCommandEncoder> m_render_encoder;

    void _initialize();
    void _finalize();
};
} // namespace ImApp
