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

  private:
    std::shared_ptr<GlfwWindow> m_window;
    int m_major_version;
    int m_minor_version;
};
} // namespace ImApp
