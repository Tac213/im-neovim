# copilot-instructions.md

This file provides guidance to GitHub Copilot when working with code in this repository.

## Project Overview

ImNeovim is an ImGui-based graphical user interface for Neovim. It embeds Neovim and provides a modern, customizable UI using ImGui, with support for Windows, Linux, and macOS, and graphics backends: OpenGL, DirectX 12, and Metal.

## Build System

- **CMake 3.24+**, **C++23** (MSVC / GCC / Clang)
- **Use the `Build_CMakeTools` tool** to build — never run raw `cmake --build` in the terminal.
- Target: `im_neovim` (main executable). The library is `im_app` (static).
- Build directory: `build/Debug` (all platforms).
- First build requires: `git submodule update --init --recursive`

## Repository Layout

```
include/im_app/          — Public API headers (application.h, layer.h, font_manager.h, image_manager.h, …)
src/im_app/
  application.cpp         — Application lifecycle, main loop
  font_manager.cpp        — Cross-platform font discovery/loading
  image_manager.cpp       — Image decoding (stb_image, nanosvg) + GPU upload dispatch
  interfaces/im_app/      — Abstract interfaces: graphics_context.h, imgui_renderer.h, window.h
  platforms/
    linux/                — GLFW + OpenGL (glfw_context, glfw_opengl_imgui_renderer, …)
    win32/                — WGL/OpenGL + D3D12 (wgl_context, dx12_context, dx12_imgui_renderer, …)
    darwin/               — Metal (metal_context, darwin_metal_imgui_renderer, …)
src/im_neovim/
  im_neovim_app.cpp       — Entry point: creates Application, pushes layers
  gui/                    — Widgets: nvim_widget, terminal, file_tree_widget, dock_space_layout
  layers/                 — Layer implementations: layer_libuv, layer_main_window
  include/                — Private headers: signal.h, logging.h, globals.h
thirdparty/               — Git submodules: imgui, glfw, glew, spdlog, fmt, libvterm, stb, nanosvg, …
```

## Code Style

### Formatting (clang-format)
- **Base**: LLVM · **Indent**: 4 spaces (no tabs) · **Pointer alignment**: left (`Type* ptr`)

### Naming Conventions

| Entity | Convention | Example |
|--------|-----------|---------|
| Classes / Structs / Enums / Namespaces | `CamelCase` | `MyClass`, `MyNamespace` |
| Methods (public/protected) | `lower_case` | `my_method()` |
| Private methods | `lower_case` with `_` prefix | `_private_method()` |
| Local variables / Parameters | `lower_case` | `local_var`, `param_name` |
| Private members | `lower_case` with `m_` prefix | `m_member_var` |
| Static constants | `lower_case` with `s_` prefix | `s_static_const` |
| Global constants | `lower_case` with `g_` prefix | `g_global_const` |
| Enum constants | `CamelCase` | `EnumValue` |

### Header Guards
Use `#pragma once` (not `#ifndef` guards).

## Key Architecture Patterns

### Adding a Platform Abstraction
1. Define the abstract interface in `src/im_app/interfaces/im_app/` (pure virtual class)
2. Add a static `create()` factory method
3. Implement per-platform in `src/im_app/platforms/<os>/`
4. Add new source files to `CMakeLists.txt` under the appropriate `if(${CMAKE_SYSTEM_NAME} …)` block
5. Do NOT add platform-specific files to the shared file lists

### GraphicsContext (GPU resource management)
- Abstract base: `src/im_app/interfaces/im_app/graphics_context.h`
- Methods: `initialize()`, `finalize()`, `swap_buffers()`, `create_texture()`, `destroy_texture()`
- `create_texture(const uint8_t* pixels, uint32_t w, uint32_t h) → uint64_t` — raw handle, no ImGui types
- Platform impls: `GlfwContext` (Linux), `WGLContext` + `D3D12Context` (Win32), `MetalContext` (macOS)

### ImageManager (image loading)
- Public API: `include/im_app/image_manager.h`
- `ImageManager::load(path)` — unified 3-tier dispatch: embedded registry → .svg file → raster file
- `ImageManager::register_embedded_image(path, data, size)` — populate bin2c dictionary
- `ImageManager::load_image_from_memory(data, size)` — direct stb_image decode
- `ImageManager::load_svg(path, w, h)` — nanosvg rasterization
- Images are **caller-owned**: call `free_image()` when done

### FontManager (font loading — reference pattern)
- `include/im_app/font_manager.h` — static API class
- Platform hook: `find_system_font()` implemented per-platform
- Font loading goes through ImGui's `ImFontAtlas`

### Layer System
- Layers inherit `ImApp::Layer` and override `on_attach()`, `on_update()`, `on_imgui_render()`
- Push layers via `Application::push_layer<T>()` in `create_im_app()`
- `IM_APP` macro → `ImApp::Application::get()`

### Signal/Slot (`src/im_neovim/include/im_neovim/signal.h`)
- `Signal<Args...>` — thread-safe, type-safe observer pattern
- `connect(callback) → uint64_t connection_id`
- `emit(args...)` — reentrant-safe (copies connection map before iterating)
- `disconnect(connection_id)`

## Graphics Backends

| Platform | Backend | Window | GraphicsContext | ImGuiRenderer |
|----------|---------|--------|----------------|---------------|
| Linux | OpenGL + GLFW | `GlfwWindow` | `GlfwContext` | `GlfwOpenGLImGuiRenderer` |
| Win32 | DirectX 12 | `Win32Window` | `D3D12Context` | `D3D12ImGuiRenderer` |
| Win32 | OpenGL (WGL) | `Win32Window` | `WGLContext` | `Win32OpenGLImGuiRenderer` |
| macOS | Metal | `DarwinWindow` | `MetalContext` | `DarwinMetalImGuiRenderer` |

### D3D12 Notes
- SRV descriptor heap size: **128** (`g_srv_heap_size`) — shared between ImGui fonts and user textures
- `DescriptorHeapAllocator` manages SRV slots with alloc/free
- `create_texture()` uses a one-shot command list with fence-synchronized upload
- User textures tracked in `m_user_textures` map for cleanup

### stb_image Flip Convention
- **No backend needs `stbi_set_flip_vertically_on_load`** — verified across OpenGL, Metal, and D3D12
- `ImageManager::set_flip_vertically_on_load()` exists as a public escape hatch but is not called by any renderer init

## Logging

- **In `im_app`**: Use `spdlog` directly via `spdlog::info()`, `spdlog::error()`, etc. Log messages prefixed with the class name: `[ClassName] message`.
- **In `im_neovim`**: Use the macros from `logging.h`: `LOG_TRACE`, `LOG_DEBUG`, `LOG_INFO`, `LOG_WARN`, `LOG_ERROR`, `LOG_CRITICAL`. They route to the `"ImNeoVim"` logger.

## Dependencies
- **ImGui** (UI), **GLFW** (Linux/macOS window), **GLEW** (OpenGL), **spdlog** + **fmt** (logging)
- **libvterm** (terminal emulation), **libuv** (async I/O), **msgpack-c** (serialization)
- **stb_image** (PNG/JPEG/BMP decoding), **nanosvg** + **nanosvgrast** (SVG rasterization)
- **Neovim** (built as external project via `ExternalProject_Add`)

## Common Pitfalls
- `graphics_context.h` must NOT include any ImGui headers — use raw `uint64_t` handles
- New public headers go in `include/im_app/`, private ones in `src/im_app/interfaces/im_app/`
- Platform-specific `.cpp` files go in `src/im_app/platforms/<os>/`, not in shared lists
- The `nanosvg` API takes a mutable `char*` (parses in-place) — use `std::string::data()`, not `.c_str()`
- For Linux builds: the `im_neovim` executable links `libim_app.a` and all thirdparty libs
