#pragma once

#include "im_neovim/signal.h"
#include <atomic>
#include <filesystem>
#include <im_app/layer.h>
#include <memory>
#include <string>

namespace ImApp {
class InstanceLock;
class IpcChannel;
} // namespace ImApp

namespace ImNeovim {

/// Manages per-workspace single-instance behavior and inter-process
/// communication.  Derives its instance key from the global workspace.
class LayerInstanceManager : public ImApp::Layer {
  public:
    LayerInstanceManager();
    ~LayerInstanceManager() override;

    void on_attach() override;
    void on_update() override;
    void on_detach() override;

    /// False if another instance already owns this workspace. The caller
    /// (create_im_app) should return nullptr and exit immediately.
    bool is_primary() const { return m_is_primary; }

    /// Emitted when another instance sends an activate request.
    Signal<> on_remote_activate;

    /// Emitted when another instance sends file paths to open.
    Signal<std::vector<std::filesystem::path>> on_remote_open_files;

  private:
    std::string m_instance_key;
    bool m_is_primary{false};
    std::atomic<bool> m_pending_activate{false};
    std::unique_ptr<ImApp::InstanceLock> m_lock;
    std::unique_ptr<ImApp::IpcChannel> m_ipc;
    uint64_t m_workspace_changed_conn{0};

    void _handle_ipc_message(const std::string& message);
    void _acquire_lock(const std::string& key);
    void _release_lock();
    void _rebind_ipc();
};

} // namespace ImNeovim
