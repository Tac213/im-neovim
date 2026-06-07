#pragma once

#include "im_app/ipc_channel.h"

#include <atomic>
#include <string>
#include <thread>

namespace ImApp {

class DarwinIpcChannel : public IpcChannel {
  public:
    DarwinIpcChannel();
    ~DarwinIpcChannel() override;

    bool bind(const std::string& key) override;
    void start_listening() override;
    bool send_message(const std::string& key,
                      const std::string& message) override;
    void set_message_handler(MessageHandler handler) override;

  private:
    int m_server_fd{-1};
    MessageHandler m_handler;
    std::thread m_listen_thread;
    std::atomic<bool> m_running{false};

    void _close_all();
    void _listen_loop();
};

} // namespace ImApp
