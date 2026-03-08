```
# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ImNeovim is an ImGui-based graphical user interface for Neovim. It embeds Neovim and provides a modern, customizable UI using ImGui, with support for multiple platforms (Windows, Linux, macOS) and graphics backends (OpenGL, DirectX 12, Metal).

## Repository Structure

```

├── src/
│ ├── im_app/ # Core application framework (cross-platform)
│ │ ├── interfaces/ # Abstract interfaces for graphics, window, renderer
│ │ ├── platforms/ # Platform-specific implementations
│ │ │ ├── win32/ # Windows implementation
│ │ │ ├── linux/ # Linux implementation (GLFW-based)
│ │ │ └── darwin/ # macOS implementation
│ │ └── application.cpp # Main application logic
│ └── im_neovim/ # Neovim integration
│ ├── include/ # Private headers
│ ├── gui/ # GUI widgets (text_widget, terminal, nvim_widget)
│ ├── layers/ # Layer implementations (libuv)
│ └── im_neovim_app.cpp # Main entry point for ImNeovim
├── include/
│ └── im_app/ # Public API headers
├── thirdparty/ # Submodules and dependencies
│ ├── imgui/ # ImGui library
│ ├── glfw/ # GLFW window library (Linux/macOS)
│ ├── glew/ # OpenGL extension loader
│ ├── spdlog/ # Logging library
│ ├── fmt/ # Formatting library
│ ├── libvterm/ # Terminal emulation library
│ ├── libuv/ # Async I/O library
│ ├── msgpack-c/ # MessagePack serialization
│ └── DirectX-Headers/ # DirectX headers (Windows)
├── build/ # Build output directory
│ └── claude/ # Claude-specific build directory (for tool operations)
├── bin/ # Binary tools
└── CMakeLists.txt # Root CMake configuration

````

## Build System

### Prerequisites

- CMake 3.24 or later
- C++23 compiler (MSVC on Windows, GCC on Linux, Clang on macOS)
- Git with submodules initialized

### Building on Windows

```bash
# Initialize submodules
git submodule update --init --recursive

# Build (using separate build/claude directory)
rm -rf build/claude
cmake -B build/claude -G "Visual Studio 17 2022" -A x64
cmake --build build/claude --config Debug
````

### Building on Linux/macOS

```bash
# Initialize submodules
git submodule update --init --recursive

# Build (using separate build/claude directory)
rm -rf build/claude
cmake -B build/claude -DCMAKE_BUILD_TYPE=Debug
cmake --build build/claude -j$(nproc)
```

## Key Components

### Core Application Framework (im_app library)

- `application.h/cpp`: Main application lifecycle management
- `layer.h`: Layer system for extending functionality
- `window.h`: Abstract window interface (platform-specific implementations in platforms/)
- `graphics_context.h`: Abstract graphics context (WGL, GLFW GL, Metal, DirectX 12)
- `imgui_renderer.h`: Abstract ImGui renderer interface (platform/graphics-specific)
- `file_system.h`: File system operations (platform-specific)
- `pty.h`: Pseudoterminal interface (for Neovim communication)

### Neovim Integration (im_neovim executable)

- `nvim_widget.h/cpp`: Main widget for embedding Neovim
- `text_widget.h/cpp`: Text rendering widget
- `terminal.h/cpp`: Terminal emulation using libvterm
- `layer_libuv.h/cpp`: libuv integration for async operations
- `globals.h/cpp`: Global state management
- `signal.h`: Thread-safe Signal/Slot implementation for event-driven communication

### Signal/Slot System (`signal.h`)

The Signal class provides a type-safe, thread-safe implementation of the observer pattern for decoupled communication between components.

**Key Features:**
- **Type-safe**: Template-based with variadic arguments (`Signal<Args...>`)
- **Thread-safe**: Uses mutex protection for connection/disconnection/emit operations
- **Reentrant-safe**: Creates a copy of connections before iterating during `emit()` to handle callbacks that modify connections
- **Connection management**: Returns connection IDs for explicit disconnection

**Usage Pattern:**
```cpp
// Define a signal
ImNeovim::Signal<int, const std::string&> on_file_opened;

// Connect a slot
uint64_t conn_id = on_file_opened.connect([](int id, const std::string& path) {
    // handle file open event
});

// Emit the signal
on_file_opened.emit(42, "/path/to/file");

// Disconnect when no longer needed
on_file_opened.disconnect(conn_id);
```

**Design Decisions:**
- Connection IDs (uint64_t) are used instead of iterator-based handles for safer cross-thread invalidation
- The `emit()` method creates a copy of the connection map to allow safe reentrant modifications
- Empty/null callbacks are filtered during connection and skipped during emission

## Development Workflow

### Code Style

This project uses strict code formatting and linting rules. All code must pass `clang-format` and `clang-tidy` checks.

#### Formatting Rules (`.clang-format`)

- **Base Style**: LLVM with customizations
- **Indentation**: 4 spaces (no tabs)
- **Pointer Alignment**: Left (`Type* ptr`, not `Type *ptr`)

#### Naming Conventions (`.clang-tidy`)

| Entity            | Convention                    | Example              |
| ----------------- | ----------------------------- | -------------------- |
| Classes           | `CamelCase`                   | `MyClass`            |
| Structs           | `CamelCase`                   | `MyStruct`           |
| Enums             | `CamelCase`                   | `MyEnum`             |
| Local variables   | `lower_case`                  | `local_var`          |
| Private members   | `lower_case` with `m_` prefix | `m_member_var`       |
| Protected members | `lower_case` (no prefix)      | `protected_var`      |
| Public members    | `lower_case` (no prefix)      | `public_var`         |
| Static constants  | `lower_case` with `s_` prefix | `s_static_const`     |
| Global constants  | `lower_case` with `g_` prefix | `g_global_const`     |
| Methods           | `lower_case`                  | `my_method()`        |
| Private methods   | `lower_case` with `_` prefix  | `_private_method()`  |
| Protected methods | `lower_case` (no prefix)      | `protected_method()` |
| Enum constants    | `CamelCase`                   | `EnumValue`          |
| Parameters        | `lower_case`                  | `param_name`         |
| Namespaces        | `CamelCase`                   | `MyNamespace`        |
| Typedefs/Using    | `CamelCase`                   | `MyTypedef`          |

### Common Tasks

1. **Building the project**: Use build/claude directory as specified above
2. **Formatting code**: Use clang-format (configuration in .clang-format)
3. **Linting**: Use clang-tidy (configuration in .clang-tidy)
4. **Debugging**:
   - On Windows: Use Visual Studio debugger with build/claude/Debug/im_neovim.exe
   - On Linux/macOS: Use GDB/LLDB with build/claude/im_neovim

### Adding New Features

1. Follow existing patterns in the codebase
2. Implement cross-platform abstractions in `src/im_app/interfaces/`
3. Add platform-specific implementations in `src/im_app/platforms/<os>/`
4. Update CMakeLists.txt if adding new files
5. Test on all supported platforms if possible

## Dependencies

- **ImGui**: UI framework
- **GLFW**: Window management (Linux/macOS)
- **GLEW**: OpenGL extensions
- **spdlog**: Logging
- **fmt**: String formatting
- **libvterm**: Terminal emulation
- **libuv**: Async I/O
- **msgpack-c**: MessagePack serialization
- **Neovim**: Embedded Neovim instance (built as external project)

## Platform-Specific Notes

### Windows

- Uses DirectX 12 or OpenGL (WGL) graphics backends
- Window management via Win32 API
- Visual Studio 2022 solution generated in build/claude/ImNeovim.sln

### Linux

- Uses GLFW for window management
- OpenGL graphics backend
- Requires X11 or Wayland display server

### macOS

- Uses Metal graphics backend
- Cocoa window management
- Objective-C++ implementation

```

```
