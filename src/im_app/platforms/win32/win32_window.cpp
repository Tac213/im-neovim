#include "win32_window.h"
#include "im_app/application.h"
#include <imgui_impl_win32.h>
#include <winuser.h>

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, // NOLINT(readability-identifier-naming)
    LPARAM lParam);                     // NOLINT(readability-identifier-naming)

namespace ImApp {
LRESULT CALLBACK Win32Window::_window_proc(HWND hwnd, uint32_t message,
                                           WPARAM w_param, LPARAM l_param) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, message, w_param, l_param)) {
        return true;
    }

    Win32Window* self =
        reinterpret_cast<Win32Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    switch (message) {
    case WM_CREATE: {
        LPCREATESTRUCT create_struct =
            reinterpret_cast<LPCREATESTRUCT>(l_param);
        SetWindowLongPtr(
            hwnd, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(create_struct->lpCreateParams));
    }
        return 0;
    case WM_SIZE:
        if (w_param != SIZE_MINIMIZED) {
            self->m_window_data.width = static_cast<uint32_t>(LOWORD(l_param));
            self->m_window_data.height = static_cast<uint32_t>(HIWORD(l_param));
        }
        return 0;
    case WM_NCHITTEST:
        if (self->m_is_titlebar_hovered) {
            return HTCAPTION;
        }
        return HTCLIENT;
    case WM_SYSCOMMAND:
        if ((w_param & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        IM_APP.exit();
        return 0;
    }
    // Handle any messages the switch statement didn't.
    return DefWindowProc(hwnd, message, w_param, l_param);
}

Win32Window::Win32Window(const WindowProps& props) { _initialize(props); }

Win32Window::~Win32Window() { _finalize(); }

void Win32Window::on_update() {
    MSG msg = {};
    if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void Win32Window::minimize() { ::ShowWindow(m_hwnd, SW_MINIMIZE); }

void Win32Window::set_titlebar_hovered(bool hovered) {
    m_is_titlebar_hovered = hovered;
}

void Win32Window::_initialize(const WindowProps& props) {
    m_window_data.title = props.title;
    m_window_data.width = props.width;
    m_window_data.height = props.height;
    HINSTANCE instance = GetModuleHandle(nullptr);
    m_window_class.cbSize = sizeof(WNDCLASSEX);
    m_window_class.style = CS_HREDRAW | CS_VREDRAW;
    m_window_class.lpfnWndProc = _window_proc;
    m_window_class.hInstance = instance;
    m_window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    m_window_class.lpszClassName = "ImAppWindowClass";
    RegisterClassEx(&m_window_class);

    size_t window_style = props.no_border ? WS_BORDER : WS_OVERLAPPEDWINDOW;
    RECT window_rect = {0, 0, static_cast<LONG>(props.width),
                        static_cast<LONG>(props.height)};
    AdjustWindowRect(&window_rect, window_style, FALSE);
    // SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // ImGui_ImplWin32_EnableDpiAwareness();

    // Create the window and store a handle to it.
    m_hwnd = ::CreateWindowA(m_window_class.lpszClassName, props.title.c_str(),
                             window_style, CW_USEDEFAULT, CW_USEDEFAULT,
                             window_rect.right - window_rect.left,
                             window_rect.bottom - window_rect.top,
                             nullptr, // We have no parent window.
                             nullptr, // We aren't using menuus.
                             instance, this);

    // Set window position at screen center
    int x_pos = (::GetSystemMetrics(SM_CXSCREEN) - props.width) / 2;
    int y_pos = (::GetSystemMetrics(SM_CYSCREEN) - props.height) / 2;
    ::SetWindowPos(m_hwnd, nullptr, x_pos, y_pos, 0, 0,
                   SWP_NOZORDER | SWP_NOSIZE);

    if (props.no_border) {
        ::SetWindowLong(m_hwnd, GWL_STYLE, 0);
    }
    ::ShowWindow(m_hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(m_hwnd);
}

void Win32Window::_finalize() {
    ::DestroyWindow(m_hwnd);
    ::UnregisterClass(m_window_class.lpszClassName, m_window_class.hInstance);
}

std::shared_ptr<Window> Window::create(const WindowProps& props) {
    auto win = std::make_shared<Win32Window>(props);
    return win;
}
} // namespace ImApp