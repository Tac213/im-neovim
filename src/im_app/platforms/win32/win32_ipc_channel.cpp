#include "win32_ipc_channel.h"

#include <string>

namespace ImApp {

// Custom message ID for folder-open IPC.
#define WM_IMNVIM_IPC_ACTIVATE (WM_APP + 1)

Win32IpcChannel::Win32IpcChannel() = default;

Win32IpcChannel::~Win32IpcChannel() { _destroy_window(); }

bool Win32IpcChannel::bind(const std::string& key) {
    _create_hidden_window(key);
    return m_hwnd != nullptr;
}

void Win32IpcChannel::start_listening() {
    // Hidden message-only window already created in _create_hidden_window.
    // Just pump messages — the window proc handles WM_COPYDATA.
}

bool Win32IpcChannel::send_message(const std::string& key,
                                   const std::string& message) {
    // Build the class name from the key.
    std::wstring wide_key(key.begin(), key.end());
    std::wstring class_name = L"ImAppIPC_" + wide_key;

    HWND target = ::FindWindowW(class_name.c_str(), nullptr);
    if (target == nullptr) {
        return false; // No listener exists
    }

    // Send the message via WM_COPYDATA.
    COPYDATASTRUCT cds = {};
    cds.dwData = WM_IMNVIM_IPC_ACTIVATE;
    cds.cbData = static_cast<DWORD>(message.size() + 1);
    cds.lpData = const_cast<char*>(message.c_str());

    ::SendMessageW(target, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds));
    return true;
}

void Win32IpcChannel::set_message_handler(MessageHandler handler) {
    m_handler = std::move(handler);
}

void Win32IpcChannel::_create_hidden_window(const std::string& key) {
    std::wstring wide_key(key.begin(), key.end());
    m_class_name = L"ImAppIPC_" + wide_key;

    HINSTANCE instance = ::GetModuleHandleW(nullptr);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = _window_proc;
    wc.hInstance = instance;
    wc.lpszClassName = m_class_name.c_str();
    ::RegisterClassExW(&wc);

    m_hwnd = ::CreateWindowExW(0, m_class_name.c_str(), L"", 0, 0, 0, 0, 0,
                               HWND_MESSAGE, nullptr, instance, this);

    if (m_hwnd != nullptr) {
        ::SetWindowLongPtrW(m_hwnd, GWLP_USERDATA,
                            reinterpret_cast<LONG_PTR>(this));
    }
}

void Win32IpcChannel::_destroy_window() {
    if (m_hwnd != nullptr) {
        ::DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (!m_class_name.empty()) {
        ::UnregisterClassW(m_class_name.c_str(), ::GetModuleHandleW(nullptr));
        m_class_name.clear();
    }
}

LRESULT CALLBACK Win32IpcChannel::_window_proc(HWND hwnd, UINT message,
                                               WPARAM w_param, LPARAM l_param) {
    auto* self = reinterpret_cast<Win32IpcChannel*>(
        ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_COPYDATA) {
        auto* cds = reinterpret_cast<COPYDATASTRUCT*>(l_param);
        if (cds != nullptr && cds->dwData == WM_IMNVIM_IPC_ACTIVATE &&
            self != nullptr && self->m_handler) {
            std::string msg(static_cast<const char*>(cds->lpData),
                            cds->cbData > 0 ? cds->cbData - 1 : 0);
            self->m_handler(msg);
        }
        return 0;
    }

    return ::DefWindowProcW(hwnd, message, w_param, l_param);
}

std::unique_ptr<IpcChannel> IpcChannel::create() {
    return std::make_unique<Win32IpcChannel>();
}

} // namespace ImApp
