#pragma once

#include "im_app/layer.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ImApp {
enum GraphicsBackend : uint8_t {
    PerformanceFirst,
    CompatibilityFirst,
};

struct AppSpec {
    std::string name = "ImApp";
    uint32_t main_window_width = 1280;
    uint32_t main_window_height = 720;
    bool main_window_no_border = true;
    GraphicsBackend graphics_backend = PerformanceFirst;
};

class ImGuiRenderer;
class GraphicsContext;
class Window;
class Application {
  public:
    explicit Application(const AppSpec& app_spec);
    ~Application();
    Application(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(const Application&) = delete;
    Application& operator=(Application&&) = delete;

    int exec();
    void exit();

    static Application& get() { return *_s_application; }

    template <typename T> void push_layer() {
        static_assert(std::is_base_of<Layer, T>::value,
                      "Pushed type is not subclass of Layer!");
        std::shared_ptr<T> layer = std::make_shared<T>();
        m_layer_stack.emplace_back(layer);
        layer->on_attach();
    }

    void push_layer(const std::shared_ptr<Layer>& layer) {
        m_layer_stack.emplace_back(layer);
        layer->on_attach();
    }

  private:
    AppSpec m_app_spec;
    std::vector<std::shared_ptr<Layer>> m_layer_stack;
    bool m_is_running = false;
    std::shared_ptr<Window> m_window = nullptr;
    std::shared_ptr<GraphicsContext> m_graphics_context = nullptr;
    std::shared_ptr<ImGuiRenderer> m_imgui_renderer = nullptr;
    static Application* _s_application;

    void _initialize();
    void _finalize();
};

extern Application* create_im_app(int argc, char** argv);
} // namespace ImApp

#define IM_APP ::ImApp::Application::get()
