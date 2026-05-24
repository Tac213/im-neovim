#pragma once

namespace ImApp {
class Layer {
  public:
    virtual ~Layer() = default;

    virtual void on_attach() {}
    virtual void on_detach() {}
    virtual void on_update() {}
    virtual void on_imgui_render() {}

    /// Called when a graceful exit is requested via
    /// Application::request_exit(). Return false to block the exit (e.g. to
    /// show a save dialog). The layer is responsible for eventually calling
    /// Application::exit() or Application::cancel_exit() to resolve the pending
    /// exit.
    virtual bool on_exit_requested() { return true; }
};
} // namespace ImApp
