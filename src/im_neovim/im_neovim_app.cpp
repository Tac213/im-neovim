#include "im_neovim/logging.h"
#include <im_app/application.h>
#include <im_app/file_system.h>
#include <im_app/layer.h>
#include <imgui.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace ImNeovim {
class MyLayer : public ImApp::Layer {
  public:
    void on_imgui_render() override { ImGui::ShowDemoWindow(); }
};

static void initialize_logger() {
    auto stdout_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
#if defined(IM_NVIM_DEBUG)
    stdout_sink->set_level(spdlog::level::debug);
#else
    stdout_sink->set_level(spdlog::level::info);
#endif
#if !defined(_WIN32)
    stdout_sink->set_color(spdlog::level::warn, stdout_sink->yellow);
    stdout_sink->set_color(spdlog::level::err, stdout_sink->red);
#endif
    auto local_app_data_path = ImApp::FileSystem::local_app_data_path();
    auto logs_dir = local_app_data_path / "ImNeovim" / "Logs";
    if (!std::filesystem::is_directory(logs_dir)) {
        std::filesystem::create_directories(logs_dir);
    }
    auto log_file_path = logs_dir / "ImNeovim.log";
    // Create a daily file sink - a new file is created every day at 2:30 am.
#if defined(_WIN32)
    auto file_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
        log_file_path.wstring(), 2, 30);
#else
    auto file_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
        log_file_path.string(), 2, 30);
#endif
    std::vector<spdlog::sink_ptr> sinks{stdout_sink, file_sink};
    auto logger = std::make_shared<spdlog::logger>(IM_NVIM_LOGGER_NAME,
                                                   sinks.begin(), sinks.end());
#if defined(IM_NVIM_DEBUG)
    logger->set_level(spdlog::level::debug);
#else
    logger->set_level(spdlog::level::info);
#endif
    spdlog::register_logger(logger);
}
} // namespace ImNeovim

namespace ImApp {
Application* create_im_app(int argc, char** argv) {
    AppSpec app_spec{.main_window_no_border = false};
    auto* app = new Application(app_spec);
    ImNeovim::initialize_logger();
    app->push_layer<ImNeovim::MyLayer>();
    return app;
}
} // namespace ImApp
