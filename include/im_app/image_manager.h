#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ImApp {

/// Holds a GPU texture handle along with its pixel dimensions.
/// Cast texture_id to ImTextureID at the ImGui call site, e.g.:
///   ImGui::Image(ImTextureRef((ImTextureID)img.texture_id), ImVec2(img.width,
///   img.height));
struct Image {
    uint64_t texture_id = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

/// Cross-platform image loader.
/// Decodes PNG/JPEG/BMP via stb_image and SVG via nanosvg,
/// then uploads pixel data to a GPU texture through the current
/// GraphicsContext.
///
/// Supports both file-system loading and compile-time embedded (bin2c) images
/// registered into a global dictionary keyed by virtual path.
struct ImageManager {
    // ---- Embedded image registry (bin2c arrays) ----

    /// Register compiled-in image data keyed by a virtual path
    /// (e.g. "assets/my_icon.png"). Call during app init, before any load().
    /// Thread-safe.
    static void register_embedded_image(const std::string& path,
                                        const unsigned char* data, size_t size);

    /// Call once during startup to control vertical flipping of raster images.
    /// Required for OpenGL (texture origin is bottom-left, stb_image outputs
    /// top-first). Not needed for Metal or D3D12 (texture origin is top-left).
    /// Wraps stbi_set_flip_vertically_on_load().
    static void set_flip_vertically_on_load(bool flip);

    // ---- Unified load (recommended entry point) ----

    /// Load an image with automatic dispatch:
    ///   1. Check the embedded registry → route to the matching memory loader.
    ///   2. If the path ends with ".svg" → load as SVG (rasterize at given
    ///      size; defaults to 32×32 if svg_width/svg_height are 0).
    ///   3. Otherwise → load as a raster image from file.
    /// Returns an Image with texture_id == 0 on failure.
    static Image load(const std::string& path, uint32_t svg_width = 0,
                      uint32_t svg_height = 0);

    // ---- Direct load methods (bypass unified dispatch) ----

    /// Load a raster image from a file. Auto-detects format (PNG/JPEG/BMP).
    static Image load_image(const std::string& path);

    /// Load a raster image from an in-memory buffer (e.g. bin2c data).
    static Image load_image_from_memory(const unsigned char* data, size_t size);

    /// Load and rasterize an SVG file at the requested pixel size.
    static Image load_svg(const std::string& path, uint32_t width, uint32_t height);

    /// Load and rasterize an SVG from an in-memory buffer.
    static Image load_svg_from_memory(const unsigned char* data, size_t size,
                                      uint32_t width, uint32_t height);

    // ---- Texture lifecycle ----

    /// Destroy the GPU texture owned by this Image and reset the struct.
    static void free_image(Image& image);
};

} // namespace ImApp
