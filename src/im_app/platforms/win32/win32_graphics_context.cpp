#include "dx12_context.h"
#include "wgl_context.h"

namespace ImApp {
static std::shared_ptr<WGLContext> g_gl_instance = nullptr;
static std::shared_ptr<D3D12Context> g_dx12_instance = nullptr;

std::shared_ptr<WGLContext> WGLContext::get() { return g_gl_instance; }
std::shared_ptr<D3D12Context> D3D12Context::get() { return g_dx12_instance; }

std::shared_ptr<GraphicsContext>
GraphicsContext::create(std::shared_ptr<Window> window,
                        GraphicsBackend backend) {
    if (backend == CompatibilityFirst) {
        auto context = std::make_shared<WGLContext>(window);
        g_gl_instance = context;
        return context;
    }
    auto dx12_context = std::make_shared<D3D12Context>(window);
    g_dx12_instance = dx12_context;
    return dx12_context;
}
} // namespace ImApp
