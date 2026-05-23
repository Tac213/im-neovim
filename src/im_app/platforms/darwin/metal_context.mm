#include "metal_context.h"
#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_COCOA
#include "GLFW/glfw3native.h"
#include <spdlog/spdlog.h>

namespace ImApp {
MetalContext::MetalContext(std::shared_ptr<DarwinWindow> window)
    : m_glfw_window(window->get_glfw_window()) {}

void MetalContext::initialize() {
    NSWindow *ns_window = glfwGetCocoaWindow(m_glfw_window);
    m_device = MTLCreateSystemDefaultDevice();
    m_command_queue = [m_device newCommandQueue];

    m_layer = [CAMetalLayer layer];
    m_layer.device = m_device;
    m_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    ns_window.contentView.layer = m_layer;
    ns_window.contentView.wantsLayer = YES;
}

void MetalContext::finalize() {
    [m_device release];
    [m_command_queue release];
}

void MetalContext::swap_buffers() {
    int width, height;
    glfwGetFramebufferSize(m_glfw_window, &width, &height);
    m_layer.drawableSize = CGSizeMake(width, height);
}

void MetalContext::on_frame_buffer_size_changed(uint32_t width,
                                                uint32_t height) {
    m_layer.drawableSize = CGSizeMake(width, height);
}

uint64_t MetalContext::create_texture(const uint8_t *pixels, uint32_t width,
                                      uint32_t height) {
    if (width == 0 || height == 0 || !pixels) {
        spdlog::error("[MetalContext] Invalid texture parameters: {}x{}", width,
                      height);
        return 0;
    }
    MTLTextureDescriptor *td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:static_cast<NSUInteger>(width)
                                    height:static_cast<NSUInteger>(height)
                                 mipmapped:NO];
    id<MTLTexture> texture = [m_device newTextureWithDescriptor:td];
    if (!texture) {
        spdlog::error("[MetalContext] Failed to create MTLTexture");
        return 0;
    }
    [texture replaceRegion:MTLRegionMake2D(0, 0, static_cast<NSUInteger>(width),
                                           static_cast<NSUInteger>(height))
               mipmapLevel:0
                 withBytes:pixels
               bytesPerRow:static_cast<NSUInteger>(width) * 4];
    return reinterpret_cast<uint64_t>((__bridge void *)texture);
}

void MetalContext::destroy_texture(uint64_t texture_id) {
    if (texture_id == 0) {
        return;
    }
    id<MTLTexture> texture = (__bridge id<MTLTexture>)(void *)texture_id;
    [texture release];
}

static std::shared_ptr<MetalContext> g_mtl_context{nullptr};

std::shared_ptr<MetalContext> MetalContext::get() { return g_mtl_context; }

std::shared_ptr<GraphicsContext>
GraphicsContext::create(std::shared_ptr<Window> window,
                        GraphicsBackend backend) {
    auto darwin_window = std::static_pointer_cast<DarwinWindow>(window);
    auto context = std::make_shared<MetalContext>(darwin_window);
    g_mtl_context = context;
    return context;
}
} // namespace ImApp
