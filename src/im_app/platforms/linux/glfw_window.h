#pragma once

#include "im_app/window.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace ImApp {
class GlfwWindow : public Window {
  public:
    explicit GlfwWindow(const WindowProps& props);
    virtual ~GlfwWindow() override;
    virtual void on_update() override;
    virtual void minimize() override;
    virtual void set_titlebar_hovered(bool hovered) override;
    virtual uint32_t get_width() const override;
    virtual uint32_t get_height() const override;

    ::GLFWwindow* get_glfw_window() const { return m_window; }

  private:
    ::GLFWwindow* m_window;

    void _initialize(const WindowProps& props);
    void _finalize();
};
} // namespace ImApp
