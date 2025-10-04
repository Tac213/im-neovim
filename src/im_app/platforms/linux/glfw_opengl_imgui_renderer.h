#pragma once
#include "GL/glew.h"
#include "glfw_window.h"
#include "im_app/imgui_renderer.h"

namespace ImApp {
class GlfwOpenGLImGuiRenderer : public ImGuiRenderer {
  public:
    explicit GlfwOpenGLImGuiRenderer(std::shared_ptr<GlfwWindow> window);
    virtual ~GlfwOpenGLImGuiRenderer() override;
    virtual void new_frame() override;
    virtual void render(std::shared_ptr<Window>& window) override;

  private:
    void _initialize(std::shared_ptr<GlfwWindow> window);
    void _finalize();
};
} // namespace ImApp
