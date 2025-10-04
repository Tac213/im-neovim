#pragma once

#include <memory>

namespace ImApp {
class Window;
class GraphicsContext {
  public:
    virtual ~GraphicsContext() = default;

    virtual void initialize() = 0;
    virtual void finalize() = 0;
    virtual void swap_buffers() = 0;

    static std::shared_ptr<GraphicsContext>
    create(std::shared_ptr<Window> window);
};
} // namespace ImApp