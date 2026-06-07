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

/// Manages per-folder single-instance behavior and inter-process
/// communication.
class LayerInstanceManager : public ImApp::Layer {
  public:
    /// @param folder_path  The folder this instance is associated with.
    explicit LayerInstanceManager(std::filesystem::path folder_path);
    ~LayerInstanceManager() override;

    void on_attach() override;
    void on_update() override;
    void on_detach() override;

    /// False if another instance already owns this folder. The caller
    /// (create_im_app) should return nullptr and exit immediately.
    bool is_primary() const { return m_is_primary; }

    /// Release the current instance lock and IPC binding, then acquire
    /// new ones keyed to `new_path`. Returns false if another instance
    /// already owns `new_path` (in which case an activate message is
    /// sent to that instance). Returns true on success.
    bool change_folder(const std::filesystem::path& new_path);

    /// Derive a unique key from a folder path (hashed absolute path).
    static std::string derive_key(const std::filesystem::path& path);

    /// Emitted when another instance sends an activate request.
    Signal<> on_remote_activate;

  private:
    std::filesystem::path m_folder_path;
    std::string m_instance_key;
    bool m_is_primary{false};
    std::atomic<bool> m_pending_activate{false};
    std::unique_ptr<ImApp::InstanceLock> m_lock;
    std::unique_ptr<ImApp::IpcChannel> m_ipc;

    void _handle_ipc_message(const std::string& message);
};

} // namespace ImNeovim
