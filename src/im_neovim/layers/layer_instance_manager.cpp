#include "layer_instance_manager.h"

#include "im_neovim/logging.h"
#include <im_app/instance_lock.h>
#include <im_app/ipc_channel.h>

namespace ImNeovim {

std::string
LayerInstanceManager::derive_key(const std::filesystem::path& path) {
    if (path.empty()) {
        return "__no_folder__";
    }
    auto abs_path = std::filesystem::absolute(path);
    // Include the app identity in the hash so different apps using
    // im_app don't collide on the same folder path.
    std::string input = "ImNeovim:" + abs_path.string();
    auto hash = std::hash<std::string>{}(input);
    return std::to_string(hash);
}

LayerInstanceManager::LayerInstanceManager(std::filesystem::path folder_path)
    : m_folder_path(std::move(folder_path)),
      m_instance_key(derive_key(m_folder_path)) {

    // Try to acquire the instance lock for this folder.
    m_lock = ImApp::InstanceLock::create(m_instance_key);
    m_is_primary = m_lock->acquired();

    if (!m_is_primary) {
        // Another instance exists — send activate message and report
        // non-primary so the caller can exit.
        auto ipc = ImApp::IpcChannel::create();
        ipc->send_message(m_instance_key, "");
        LOG_INFO("Activating existing instance for folder: {}",
                 m_folder_path.string());
    }
}

LayerInstanceManager::~LayerInstanceManager() = default;

void LayerInstanceManager::on_attach() {
    if (!m_is_primary) {
        return;
    }

    // Create IPC channel and bind to our instance key.
    m_ipc = ImApp::IpcChannel::create();
    if (!m_ipc->bind(m_instance_key)) {
        LOG_ERROR("Failed to bind IPC channel for key: {}", m_instance_key);
        return;
    }

    m_ipc->set_message_handler(
        [this](const std::string& message) { _handle_ipc_message(message); });

    m_ipc->start_listening();
    LOG_INFO("IPC listener started for folder: {}", m_folder_path.string());
}

void LayerInstanceManager::on_update() {
    // Check for a pending remote-activate request that arrived on the
    // IPC background thread.  We must call activate_window() from the
    // main thread because the platform window activation APIs (GLFW /
    // Win32 / Metal) are not thread-safe.
    if (m_pending_activate.exchange(false)) {
        LOG_INFO("Processing deferred remote activate for folder: {}",
                 m_folder_path.string());
        on_remote_activate.emit();
    }
}

void LayerInstanceManager::on_detach() {
    if (m_ipc) {
        m_ipc.reset();
    }
    if (m_lock) {
        m_lock.reset();
    }
}

void LayerInstanceManager::_handle_ipc_message(const std::string& /*message*/) {
    LOG_INFO("Received remote activate request for folder: {}",
             m_folder_path.string());
    // Set the flag — on_update() will emit on_remote_activate from the
    // main thread, where GLFW / platform window calls are safe.
    m_pending_activate.store(true);
}

} // namespace ImNeovim
