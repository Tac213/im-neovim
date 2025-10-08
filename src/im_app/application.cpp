#include "im_app/application.h"
#include "im_app/graphics_context.h"
#include "im_app/imgui_renderer.h"
#include "im_app/window.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace ImApp {
Application* Application::_s_application = nullptr;

static void initialize_spdlog();
static void finalize_spdlog();

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
        if (m_window) {
            m_window->on_update();
        }
        for (auto& layer : m_layer_stack) {
            layer->on_update();
        }
        m_imgui_renderer->new_frame();
        for (auto& layer : m_layer_stack) {
            layer->on_imgui_render();
        }
        m_imgui_renderer->render(m_window);
        m_graphics_context->swap_buffers();
    }
    return 0;
}

void Application::exit() { m_is_running = false; }

void Application::_initialize() {
    initialize_spdlog();
    WindowProps window_props = {m_app_spec.name, m_app_spec.main_window_width,
                                m_app_spec.main_window_height,
                                m_app_spec.main_window_no_border};
#if defined(_WIN32)
    if (m_app_spec.graphics_backend == CompatibilityFirst) {
        // Doesn't work on win32 + opengl currently.
        window_props.enable_dpi_awareness = false;
    }
#endif
    m_window = Window::create(window_props);
    m_graphics_context =
        GraphicsContext::create(m_window, m_app_spec.graphics_backend);
    m_graphics_context->initialize();
    m_imgui_renderer =
        ImGuiRenderer::create(m_window, m_app_spec.graphics_backend);
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
    finalize_spdlog();
}

void initialize_spdlog() {
    spdlog::flush_every(std::chrono::seconds(5));
    auto stdout_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
#if defined(IM_APP_DEBUG)
    stdout_sink->set_level(spdlog::level::debug);
#else
    stdout_sink->set_level(spdlog::level::info);
#endif
#if !defined(_WIN32)
    stdout_sink->set_color(spdlog::level::warn, stdout_sink->yellow);
    stdout_sink->set_color(spdlog::level::err, stdout_sink->red);
#endif
    std::vector<spdlog::sink_ptr> sinks{stdout_sink};
    auto logger =
        std::make_shared<spdlog::logger>("ImApp", sinks.begin(), sinks.end());
#if defined(IM_APP_DEBUG)
    logger->set_level(spdlog::level::debug);
#else
    logger->set_level(spdlog::level::info);
#endif
    spdlog::set_default_logger(logger);
}

void finalize_spdlog() {
    spdlog::apply_all(
        [](std::shared_ptr<spdlog::logger> logger) { logger->flush(); });
    spdlog::drop_all();
}
} // namespace ImApp
