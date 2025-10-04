#pragma once

#include "im_app/window.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace ImApp {
class Win32Window : public Window {
  public:
    explicit Win32Window(const WindowProps& props);
    virtual ~Win32Window() override;
    virtual void on_update() override;
    virtual void minimize() override;
    virtual void set_titlebar_hovered(bool hovered) override;
    virtual uint32_t get_width() const override { return m_window_data.width; }
    virtual uint32_t get_height() const override {
        return m_window_data.height;
    };

    HWND get_hwnd() const { return m_hwnd; }

  private:
    HWND m_hwnd;
    WNDCLASSEX m_window_class = {};
    bool m_is_titlebar_hovered = false;

    struct WindowData {
        std::string title;
        uint32_t width;
        uint32_t height;
    };

    WindowData m_window_data;

    void _initialize(const WindowProps& props);
    void _finalize();
    static LRESULT CALLBACK _window_proc(HWND hwnd, uint32_t message,
                                         WPARAM w_param, LPARAM l_param);
};
} // namespace ImApp