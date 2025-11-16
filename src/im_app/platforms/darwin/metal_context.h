#pragma once

#include "darwin_window.h"
#include "im_app/graphics_context.h"
#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_COCOA
#include "GLFW/glfw3.h"
#include "GLFW/glfw3native.h"
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

namespace ImApp {
class MetalContext : public GraphicsContext {
  public:
    explicit MetalContext(std::shared_ptr<DarwinWindow> window);
    virtual void initialize() override;
    virtual void finalize() override;
    virtual void swap_buffers() override;
    virtual void on_frame_buffer_size_changed(uint32_t width,
                                              uint32_t height) override;

    id<MTLDevice> get_device() const { return m_device; }
    id<MTLCommandQueue> get_command_queue() const { return m_command_queue; }
    CAMetalLayer* get_layer() const { return m_layer; }

    static std::shared_ptr<MetalContext> get();

  private:
    ::GLFWwindow* m_glfw_window{nullptr};
    id<MTLDevice> m_device;
    id<MTLCommandQueue> m_command_queue;
    CAMetalLayer* m_layer;
};
} // namespace ImApp
