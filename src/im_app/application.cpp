#include "im_app/application.h"
#include "im_app/graphics_context.h"
#include "im_app/imgui_renderer.h"
#include "im_app/window.h"
#include <stdexcept>

namespace ImApp {
Application* Application::_s_application = nullptr;

Application::Application(const AppSpec& app_spec)
    : m_app_spec(app_spec), m_is_running(true) {
    if (_s_application) {
        throw std::runtime_error("Application already exists!");
    }
    _s_application = this;
    _initialize();
}

Application::~Application() {
    _finalize();
    _s_application = nullptr;
}

int Application::exec() {
    m_is_running = true;
    // Main loop
    while (m_is_running) {
        for (auto& layer : m_layer_stack) {
            layer->on_update();
        }
        m_imgui_renderer->new_frame();
        for (auto& layer : m_layer_stack) {
            layer->on_imgui_render();
        }
        m_imgui_renderer->render(m_window);
        m_graphics_context->swap_buffers();
        if (m_window) {
            m_window->on_update();
        }
    }
    return 0;
}

void Application::exit() { m_is_running = false; }

void Application::_initialize() {
    WindowProps window_props = {m_app_spec.name, m_app_spec.main_window_width,
                                m_app_spec.main_window_height,
                                m_app_spec.main_window_no_border};
    m_window = Window::create(window_props);
    m_graphics_context = GraphicsContext::create(m_window);
    m_graphics_context->initialize();
    m_imgui_renderer = ImGuiRenderer::create(m_window);
}

void Application::_finalize() {
    for (auto& layer : m_layer_stack) {
        layer->on_detach();
    }
    m_layer_stack.clear();
    m_imgui_renderer.reset();
    m_graphics_context->finalize();
    m_graphics_context.reset();
    m_window.reset();
    m_is_running = false;
}
} // namespace ImApp
