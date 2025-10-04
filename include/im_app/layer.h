#pragma once

namespace ImApp {
class Layer {
  public:
    virtual ~Layer() = default;

    virtual void on_attach() {}
    virtual void on_detach() {}
    virtual void on_update() {}
    virtual void on_imgui_render() {}
};
} // namespace ImApp
