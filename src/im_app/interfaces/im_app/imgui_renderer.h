#pragma once

#include <memory>

namespace ImApp {
class Window;
class ImGuiRenderer {
  public:
    ImGuiRenderer() = default;
    virtual ~ImGuiRenderer() = default;
    virtual void new_frame() = 0;
    virtual void render(std::shared_ptr<Window>& window) = 0;

    static std::shared_ptr<ImGuiRenderer> create();
};
} // namespace ImApp