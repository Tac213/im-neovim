#pragma once
#include "GL/glew.h"
#include "glfw_window.h"
#include "im_app/graphics_context.h"

namespace ImApp {
class GlfwContext : public GraphicsContext {
  public:
    explicit GlfwContext(std::shared_ptr<GlfwWindow> window);
    virtual void initialize() override;
    virtual void finalize() override;
    virtual void swap_buffers() override;
    virtual void on_frame_buffer_size_changed(uint32_t width,
                                              uint32_t height) override {}

  private:
    std::shared_ptr<GlfwWindow> m_window;
    int m_major_version;
    int m_minor_version;
};
} // namespace ImApp
