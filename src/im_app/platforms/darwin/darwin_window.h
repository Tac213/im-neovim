#pragma once

#include "im_app/window.h"
#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>

namespace ImApp {
class DarwinWindow : public Window {
  public:
    explicit DarwinWindow(const WindowProps& props);
    virtual ~DarwinWindow() override;
    virtual void on_update() override;
    virtual void minimize() override;
    virtual void set_titlebar_hovered(bool hovered) override;
    virtual uint32_t get_width() const override;
    virtual uint32_t get_height() const override;

    ::GLFWwindow* get_glfw_window() const { return m_window; }

  private:
    ::GLFWwindow* m_window{nullptr};

    void _initialize(const WindowProps& props);
    void _finalize();
};
} // namespace ImApp
