#pragma once

#include "im_app/imgui_renderer.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace ImApp {
class Win32OpenGLImGuiRenderer : public ImGuiRenderer {
  public:
    Win32OpenGLImGuiRenderer();
    virtual ~Win32OpenGLImGuiRenderer() override;
    virtual void new_frame() override;
    virtual void render(std::shared_ptr<Window>& window) override;

  private:
    struct WGLWindowData {
        HDC hdc;
    };

    void _initialize();
    void _finalize();
};
} // namespace ImApp