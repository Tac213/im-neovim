#pragma once

#include "im_app/application.h"
#include <cstdint>
#include <memory>

namespace ImApp {
class Window;
class GraphicsContext {
  public:
    virtual ~GraphicsContext() = default;

    virtual void initialize() = 0;
    virtual void finalize() = 0;
    virtual void swap_buffers() = 0;
    virtual void on_frame_buffer_size_changed(uint32_t width,
                                              uint32_t height) = 0;

    /// Create a GPU texture from raw RGBA8 pixel data.
    /// @param pixels  RGBA8 pixel buffer (4 bytes per pixel, row-major).
    /// @param width   Texture width in pixels.
    /// @param height  Texture height in pixels.
    /// @return An opaque texture handle (0 on failure).
    virtual uint64_t create_texture(const uint8_t* pixels, uint32_t width,
                                    uint32_t height) = 0;

    /// Destroy a GPU texture previously created by create_texture().
    /// @param texture_id  Handle returned by create_texture().
    virtual void destroy_texture(uint64_t texture_id) = 0;

    /// Human-readable name of the active graphics backend.
    /// E.g. "DirectX 12", "OpenGL 4.6 (WGL)", "Metal".
    virtual const char* get_backend_name() const = 0;

    static std::shared_ptr<GraphicsContext>
    create(std::shared_ptr<Window> window, GraphicsBackend backend);
};
} // namespace ImApp