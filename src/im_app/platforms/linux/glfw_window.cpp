#include "glfw_window.h"
#include "im_app/application.h"
#include <imgui_impl_glfw.h>
#include <stdexcept>

namespace ImApp {
GlfwWindow::GlfwWindow(const WindowProps& props) { _initialize(props); }

GlfwWindow::~GlfwWindow() { _finalize(); }

void GlfwWindow::on_update() {
    if (glfwWindowShouldClose(m_window)) {
        IM_APP.exit();
        return;
    }
    glfwPollEvents();
}

void GlfwWindow::minimize() {
    if (m_window) {
        glfwIconifyWindow(m_window);
    }
}

void GlfwWindow::set_titlebar_hovered(bool hovered) {}

uint32_t GlfwWindow::get_width() const {
    if (!m_window) {
        return 0;
    }
    int width, height;
    glfwGetFramebufferSize(m_window, &width, &height);
    return static_cast<uint32_t>(width);
}

uint32_t GlfwWindow::get_height() const {
    if (!m_window) {
        return 0;
    }
    int width, height;
    glfwGetFramebufferSize(m_window, &width, &height);
    return static_cast<uint32_t>(height);
}

void GlfwWindow::_initialize(const WindowProps& props) {
    float main_scale =
        ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
    m_window = glfwCreateWindow(static_cast<int>(props.width * main_scale),
                                static_cast<int>(props.height * main_scale),
                                props.title.c_str(), nullptr, nullptr);
    if (m_window == nullptr) {
        throw std::runtime_error("Failed to create glfw window.");
    }
}

void GlfwWindow::_finalize() {
    if (m_window) {
        glfwDestroyWindow(m_window);
    }
}

std::shared_ptr<Window> Window::create(const WindowProps& props) {
    auto win = std::make_shared<GlfwWindow>(props);
    return win;
}
} // namespace ImApp
