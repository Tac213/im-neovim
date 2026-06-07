#pragma once

#include "im_app/ipc_channel.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <string>

namespace ImApp {

class Win32IpcChannel : public IpcChannel {
  public:
    Win32IpcChannel();
    ~Win32IpcChannel() override;

    bool bind(const std::string& key) override;
    void start_listening() override;
    bool send_message(const std::string& key,
                      const std::string& message) override;
    void set_message_handler(MessageHandler handler) override;

  private:
    HWND m_hwnd{nullptr};
    MessageHandler m_handler;
    std::wstring m_class_name;

    void _create_hidden_window(const std::string& key);
    void _destroy_window();
    static LRESULT CALLBACK _window_proc(HWND hwnd, UINT message,
                                         WPARAM w_param, LPARAM l_param);
};

} // namespace ImApp
