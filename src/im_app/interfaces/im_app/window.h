#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace ImApp {
struct WindowProps {
    std::string title;
    uint32_t width;
    uint32_t height;
    bool no_border;
    bool enable_dpi_awareness = true;
};

/*
 * Interface representing a desktop system based Window
 */
class Window {
  public:
    virtual ~Window() = default;
    virtual void on_update() = 0;
    virtual void minimize() = 0;
    virtual void set_titlebar_hovered(bool hovered) = 0;
    virtual uint32_t get_width() const = 0;
    virtual uint32_t get_height() const = 0;

    static std::shared_ptr<Window> create(const WindowProps& props);
};
} // namespace ImApp
