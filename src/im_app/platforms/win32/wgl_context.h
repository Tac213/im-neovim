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
    virtual void on_frame_buffer_size_changed(uint32_t width,
                                              uint32_t height) override {}
    virtual uint64_t create_texture(const uint8_t* pixels, uint32_t width,
                                    uint32_t height) override;
    virtual void destroy_texture(uint64_t texture_id) override;

    virtual const char* get_backend_name() const override;

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
    mutable std::string m_backend_name;
    HWND m_hwnd;
    HDC m_hdc;
    HGLRC m_hrc = nullptr;
};
} // namespace ImApp
