#include "layer_instance_manager.h"

#include "im_neovim/globals.h"
#include "im_neovim/logging.h"
#include <im_app/instance_lock.h>
#include <im_app/ipc_channel.h>

namespace ImNeovim {

LayerInstanceManager::LayerInstanceManager() {
    // Derive the instance key from the global workspace.
    m_instance_key = g_workspace.derive_instance_key();

    if (m_instance_key.empty()) {
        // Empty workspace — no instance locking, always primary.
        m_is_primary = true;
        return;
    }

    _acquire_lock(m_instance_key);
}

LayerInstanceManager::~LayerInstanceManager() { _release_lock(); }

void LayerInstanceManager::on_attach() {
    if (!m_is_primary) {
        return;
    }

    _rebind_ipc();

    // Listen for workspace changes so we can rebind the lock/IPC.
    m_workspace_changed_conn = g_workspace.on_changed.connect([this]() {
        std::string new_key = g_workspace.derive_instance_key();

        if (new_key == m_instance_key) {
            return; // No change.
        }

        LOG_INFO("Workspace changed - rebinding instance lock from '{}' to "
                 "'{}'",
                 m_instance_key, new_key);

        // Release old resources.
        m_ipc.reset();
        m_lock.reset();

        m_instance_key = new_key;

        if (m_instance_key.empty()) {
            // Workspace became empty — no locking needed.
            m_is_primary = true;
            return;
        }

        _acquire_lock(m_instance_key);
        if (m_is_primary) {
            _rebind_ipc();
        }
        // If !m_is_primary, the lock acquisition already sent an
        // activate message to the existing instance.  This instance
        // should probably exit — but for now we continue as primary
        // since we already have the window open.
        m_is_primary = true;
    });
}

void LayerInstanceManager::on_update() {
    if (m_pending_activate.exchange(false)) {
        LOG_INFO("Processing deferred remote activate for workspace key: {}",
                 m_instance_key);
        on_remote_activate.emit();
    }
}

void LayerInstanceManager::on_detach() {
    if (m_workspace_changed_conn != 0) {
        g_workspace.on_changed.disconnect(m_workspace_changed_conn);
        m_workspace_changed_conn = 0;
    }
    m_ipc.reset();
    m_lock.reset();
}

void LayerInstanceManager::_acquire_lock(const std::string& key) {
    m_lock = ImApp::InstanceLock::create(key);
    m_is_primary = m_lock->acquired();

    if (!m_is_primary) {
        // Another instance exists — send activate message.
        auto ipc = ImApp::IpcChannel::create();
        ipc->send_message(key, "");
        LOG_INFO("Activating existing instance for key: {}", key);
    }
}

void LayerInstanceManager::_release_lock() {
    m_ipc.reset();
    m_lock.reset();
}

void LayerInstanceManager::_rebind_ipc() {
    if (m_instance_key.empty()) {
        return;
    }

    m_ipc = ImApp::IpcChannel::create();
    if (!m_ipc->bind(m_instance_key)) {
        LOG_ERROR("Failed to bind IPC channel for key: {}", m_instance_key);
        return;
    }

    m_ipc->set_message_handler(
        [this](const std::string& message) { _handle_ipc_message(message); });

    m_ipc->start_listening();
    LOG_INFO("IPC listener started for key: {}", m_instance_key);
}

void LayerInstanceManager::_handle_ipc_message(const std::string& message) {
    // Empty message or just "FILES" with no paths — activate only.
    if (message.empty() || message == "FILES") {
        LOG_INFO("Received remote activate request for key: {}",
                 m_instance_key);
        m_pending_activate.store(true);
        return;
    }

    // "FILES\n<path>\n<path>..." — open files in existing instance.
    if (message.starts_with("FILES\n")) {
        std::vector<std::filesystem::path> files;
        std::string_view body{message};
        body.remove_prefix(6); // skip "FILES\n"

        size_t pos = 0;
        while (pos < body.size()) {
            auto nl = body.find('\n', pos);
            auto line = body.substr(pos, nl == std::string_view::npos
                                             ? body.size() - pos
                                             : nl - pos);
            if (!line.empty()) {
                files.emplace_back(line);
            }
            if (nl == std::string_view::npos) {
                break;
            }
            pos = nl + 1;
        }

        if (!files.empty()) {
            LOG_INFO("Received remote open-files request ({} file(s)) "
                     "for key: {}",
                     files.size(), m_instance_key);
            m_pending_activate.store(true);
            on_remote_open_files.emit(std::move(files));
            return;
        }
    }

    // Fallback: treat as plain activate.
    LOG_INFO("Received remote activate request for key: {}", m_instance_key);
    m_pending_activate.store(true);
}

} // namespace ImNeovim
