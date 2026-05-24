#include "darwin_window.h"
#include "im_app/application.h"
#include <imgui_impl_glfw.h>

namespace ImApp {

void DarwinWindow::_on_window_close(GLFWwindow* window) {
    auto* self = static_cast<DarwinWindow*>(glfwGetWindowUserPointer(window));
    if (self) {
        self->m_close_requested = true;
    }
}

DarwinWindow::DarwinWindow(const WindowProps& props) { _initialize(props); }

DarwinWindow::~DarwinWindow() { _finalize(); }

void DarwinWindow::on_update() {
    glfwPollEvents();
    if (m_close_requested) {
        m_close_requested = false;
        IM_APP.request_exit();
    }
}

void DarwinWindow::minimize() {
    if (m_window) {
        glfwIconifyWindow(m_window);
    }
}

void DarwinWindow::set_titlebar_hovered(bool hovered) {}

uint32_t DarwinWindow::get_width() const {
    if (!m_window) {
        return 0;
    }
    int width, height;
    glfwGetFramebufferSize(m_window, &width, &height);
    return static_cast<uint32_t>(width);
}

uint32_t DarwinWindow::get_height() const {
    if (!m_window) {
        return 0;
    }
    int width, height;
    glfwGetFramebufferSize(m_window, &width, &height);
    return static_cast<uint32_t>(height);
}

void DarwinWindow::_initialize(const WindowProps& props) {
    float main_scale =
        ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    m_window = glfwCreateWindow(static_cast<int>(props.width * main_scale),
                                static_cast<int>(props.height * main_scale),
                                props.title.c_str(), nullptr, nullptr);
    if (m_window == nullptr) {
        throw std::runtime_error("Failed to create glfw window.");
    }
    glfwSetWindowUserPointer(m_window, this);
    glfwSetWindowCloseCallback(m_window, _on_window_close);
}

void DarwinWindow::_finalize() {
    if (m_window) {
        glfwDestroyWindow(m_window);
    }
}

std::shared_ptr<Window> Window::create(const WindowProps& props) {
    auto win = std::make_shared<DarwinWindow>(props);
    return win;
}
} // namespace ImApp
