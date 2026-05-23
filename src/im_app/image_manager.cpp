#include "im_app/image_manager.h"
#include "im_app/application.h"
#include "im_app/graphics_context.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define NANOSVG_IMPLEMENTATION
#include <nanosvg.h>
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvgrast.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <spdlog/spdlog.h>

namespace ImApp {

// ---------------------------------------------------------------------------
// Embedded image registry
// ---------------------------------------------------------------------------

struct EmbeddedImageEntry {
    const unsigned char* data;
    size_t size;
};

static std::unordered_map<std::string, EmbeddedImageEntry>&
get_embedded_registry() {
    static std::unordered_map<std::string, EmbeddedImageEntry> registry;
    return registry;
}

static std::mutex& get_registry_mutex() {
    static std::mutex mtx;
    return mtx;
}

void ImageManager::register_embedded_image(const std::string& path,
                                           const unsigned char* data,
                                           size_t size) {
    std::lock_guard<std::mutex> lock(get_registry_mutex());
    get_embedded_registry()[path] = {data, size};
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static bool ends_with(const std::string& str, const std::string& suffix) {
    if (suffix.size() > str.size()) {
        return false;
    }
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        spdlog::error("[ImageManager] Failed to open file: {}", path);
        return {};
    }
    std::streamsize sz = file.tellg();
    if (sz <= 0) {
        spdlog::error("[ImageManager] Empty or invalid file: {}", path);
        return {};
    }
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(static_cast<size_t>(sz));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), sz)) {
        spdlog::error("[ImageManager] Failed to read file: {}", path);
        return {};
    }
    return buffer;
}

static std::shared_ptr<GraphicsContext> get_ctx() {
    auto ctx = Application::get().get_graphics_context();
    if (!ctx) {
        spdlog::error("[ImageManager] No graphics context available");
    }
    return ctx;
}

// ---------------------------------------------------------------------------
// Unified load
// ---------------------------------------------------------------------------

Image ImageManager::load(const std::string& path, uint32_t svg_width,
                         uint32_t svg_height) {
    // 1. Check the embedded registry first.
    {
        std::lock_guard<std::mutex> lock(get_registry_mutex());
        auto& registry = get_embedded_registry();
        auto it = registry.find(path);
        if (it != registry.end()) {
            if (ends_with(path, ".svg")) {
                uint32_t w = svg_width > 0 ? svg_width : 32;
                uint32_t h = svg_height > 0 ? svg_height : 32;
                return load_svg_from_memory(it->second.data, it->second.size,
                                            w, h);
            }
            return load_image_from_memory(it->second.data, it->second.size);
        }
    }

    // 2. If the path ends with ".svg", treat as SVG.
    if (ends_with(path, ".svg")) {
        uint32_t w = svg_width > 0 ? svg_width : 32;
        uint32_t h = svg_height > 0 ? svg_height : 32;
        return load_svg(path, w, h);
    }

    // 3. Default: load as a raster image file.
    return load_image(path);
}

// ---------------------------------------------------------------------------
// Raster image loading (stb_image)
// ---------------------------------------------------------------------------

Image ImageManager::load_image(const std::string& path) {
    auto file_data = read_file(path);
    if (file_data.empty()) {
        return {};
    }
    return load_image_from_memory(file_data.data(), file_data.size());
}

Image ImageManager::load_image_from_memory(const unsigned char* data,
                                           size_t size) {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load_from_memory(
        data, static_cast<int>(size), &width, &height, &channels,
        4); // Force RGBA

    if (!pixels) {
        spdlog::error("[ImageManager] Failed to decode image: {}",
                      stbi_failure_reason());
        return {};
    }

    auto ctx = get_ctx();
    if (!ctx) {
        stbi_image_free(pixels);
        return {};
    }

    Image result;
    auto w = static_cast<uint32_t>(width);
    auto h = static_cast<uint32_t>(height);
    result.texture_id = ctx->create_texture(pixels, w, h);
    result.width = w;
    result.height = h;

    stbi_image_free(pixels);

    if (result.texture_id == 0) {
        spdlog::error("[ImageManager] Failed to create GPU texture");
    }
    return result;
}

// ---------------------------------------------------------------------------
// SVG loading (nanosvg + nanosvgrast)
// ---------------------------------------------------------------------------

void ImageManager::set_flip_vertically_on_load(bool flip) {
    stbi_set_flip_vertically_on_load(flip ? 1 : 0);
}

Image ImageManager::load_svg(const std::string& path, uint32_t width, uint32_t height) {
    auto file_data = read_file(path);
    if (file_data.empty()) {
        return {};
    }
    // nanosvg requires a null-terminated C string.
    std::string svg_str(reinterpret_cast<const char*>(file_data.data()),
                        file_data.size());
    return load_svg_from_memory(
        reinterpret_cast<const unsigned char*>(svg_str.c_str()),
        svg_str.size(), width, height);
}

Image ImageManager::load_svg_from_memory(const unsigned char* data,
                                         size_t size, uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        spdlog::error("[ImageManager] Invalid SVG dimensions: {}x{}", width,
                      height);
        return {};
    }

    // nanosvg requires a mutable null-terminated C string (parses in-place).
    std::string svg_str(reinterpret_cast<const char*>(data), size);
    NSVGimage* svg_image = nsvgParse(svg_str.data(), "px", 96.0f);
    if (!svg_image) {
        spdlog::error("[ImageManager] Failed to parse SVG");
        return {};
    }

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) {
        nsvgDelete(svg_image);
        spdlog::error("[ImageManager] Failed to create SVG rasterizer");
        return {};
    }

    // Calculate scale to fit the requested dimensions (uniform scale).
    float scale_x = static_cast<float>(width) / svg_image->width;
    float scale_y = static_cast<float>(height) / svg_image->height;
    float scale = std::min(scale_x, scale_y);

    int raster_w = static_cast<int>(svg_image->width * scale);
    int raster_h = static_cast<int>(svg_image->height * scale);
    if (raster_w <= 0) {
        raster_w = static_cast<int>(width);
    }
    if (raster_h <= 0) {
        raster_h = static_cast<int>(height);
    }

    std::vector<unsigned char> pixels(
        static_cast<size_t>(raster_w) * static_cast<size_t>(raster_h) * 4);
    nsvgRasterize(rast, svg_image, 0, 0, scale, pixels.data(), raster_w,
                  raster_h, raster_w * 4);

    nsvgDeleteRasterizer(rast);
    nsvgDelete(svg_image);

    auto ctx = get_ctx();
    if (!ctx) {
        return {};
    }

    Image result;
    auto uw = static_cast<uint32_t>(raster_w);
    auto uh = static_cast<uint32_t>(raster_h);
    result.texture_id = ctx->create_texture(pixels.data(), uw, uh);
    result.width = uw;
    result.height = uh;

    if (result.texture_id == 0) {
        spdlog::error(
            "[ImageManager] Failed to create GPU texture from SVG");
    }
    return result;
}

// ---------------------------------------------------------------------------
// Texture lifecycle
// ---------------------------------------------------------------------------

void ImageManager::free_image(Image& image) {
    if (image.texture_id != 0) {
        auto ctx = get_ctx();
        if (ctx) {
            ctx->destroy_texture(image.texture_id);
        }
        image.texture_id = 0;
        image.width = 0;
        image.height = 0;
    }
}

} // namespace ImApp
