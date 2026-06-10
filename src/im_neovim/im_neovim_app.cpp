// clang-format off
#include "layers/layer_main_window.h"
// clang-format on
#include "im_neovim/globals.h"
#include "im_neovim/logging.h"
#include "imnvim_assets/imnvim_assets.h"
#include "layers/layer_instance_manager.h"
#include "layers/layer_libuv.h"
#include <im_app/application.h>
#include <im_app/file_system.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace ImNeovim {
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
    ImNeovim::initialize_logger();

    // Parse argv[1..] for workspace folders and files to edit.
    // Directories go to the workspace; regular files (or paths whose
    // parent directory exists) are queued for opening once the nvim
    // API connection is ready.
    for (int i = 1; i < argc; ++i) {
        std::string_view arg{argv[i]};

        // Skip flags (imnvim doesn't define any yet, but be
        // forward-compatible).
        if (arg.starts_with("--")) {
            continue;
        }

        std::filesystem::path p{arg};
        std::error_code ec;
        auto status = std::filesystem::status(p, ec);
        // Note: ec may be set to ENOENT on some platforms for
        // non-existent paths.  The subsequent checks handle all
        // cases correctly regardless.

        if (std::filesystem::is_directory(status)) {
            ImNeovim::g_workspace.add_folder(p);
        } else if (std::filesystem::is_regular_file(status)) {
            ImNeovim::g_pending_startup_files.push_back(
                std::filesystem::weakly_canonical(p));
        } else if (!std::filesystem::exists(status)) {
            // File does not exist yet — check if the parent
            // directory exists.  If so, treat as a new file
            // that nvim will create on :edit.
            // An empty parent means a bare filename relative to
            // CWD, which always exists.
            auto parent = p.parent_path();
            if (parent.empty() || std::filesystem::is_directory(parent, ec)) {
                ImNeovim::g_pending_startup_files.push_back(
                    std::filesystem::weakly_canonical(p));
            }
        }
    }

    // Single-instance check keyed to the entire workspace.
    auto instance_mgr = std::make_shared<ImNeovim::LayerInstanceManager>();
    if (!instance_mgr->is_primary()) {
        // Another instance already owns this workspace.
        return nullptr;
    }

    IMNVIM_REGISTER_EMBEDDED_ASSETS();

    AppSpec app_spec{.name = "ImNeovim", .main_window_no_border = false};
    auto* app = new Application(app_spec);

    app->push_layer<ImNeovim::LayerLibuv>();
    app->push_layer(instance_mgr);

    auto main_layer = std::make_shared<ImNeovim::LayerMainWindow>();
    main_layer->set_instance_manager(instance_mgr);
    // Connect remote activate signal to window activation.
    instance_mgr->on_remote_activate.connect(
        []() { IM_APP.activate_window(); });

    // Connect remote file-open signal: push files to the global
    // startup queue so NvimWidget picks them up on the next frame.
    instance_mgr->on_remote_open_files.connect(
        [](const std::vector<std::filesystem::path>& files) {
            ImNeovim::g_pending_startup_files.insert(
                ImNeovim::g_pending_startup_files.end(), files.begin(),
                files.end());
        });

    app->push_layer(main_layer);

    return app;
}
} // namespace ImApp
