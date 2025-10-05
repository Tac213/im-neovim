#include "dx12_imgui_renderer.h"
#include "win32_opengl_imgui_renderer.h"


namespace ImApp {
std::shared_ptr<ImGuiRenderer>
ImGuiRenderer::create(std::shared_ptr<Window> window, GraphicsBackend backend) {
    if (backend == CompatibilityFirst) {
        auto renderer = std::make_shared<Win32OpenGLImGuiRenderer>();
        return renderer;
    }
    auto dx12_renderer = std::make_shared<D3D12ImGuiRenderer>();
    return dx12_renderer;
}
} // namespace ImApp
