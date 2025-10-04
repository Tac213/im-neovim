#pragma once

#include "im_app/graphics_context.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace ImApp {
class WGLContext : public GraphicsContext {
  public:
    explicit WGLContext(std::shared_ptr<Window> window);

    virtual void initialize() override;
    virtual void finalize() override;
    virtual void swap_buffers() override;

    bool create_device(HWND hwnd, HDC& hdc);
    static void cleanup_device(HWND hwnd, HDC& hdc);
    bool make_current(HDC& hdc);
    bool make_current();
    static bool swap_buffers(HDC& hdc);

    HWND get_hwnd() const { return m_hwnd; }
    HDC get_hdc() { return m_hdc; }

    static std::shared_ptr<WGLContext> get();

  private:
    int m_major_version;
    int m_minor_version;
    HWND m_hwnd;
    HDC m_hdc;
    HGLRC m_hrc = nullptr;
};
} // namespace ImApp
