# ImNeovim

An ImGui-based graphical user interface for Neovim. Embeds Neovim and provides
a modern, customizable UI with support for Windows, Linux, and macOS, and
graphics backends: OpenGL, DirectX 12, and Metal.

## Prerequisites

- **CMake 3.24** or later
- **C++23** compiler (MSVC 2022, GCC 13+, Clang 17+)
- **Git** with submodules
- **Ninja** or **Visual Studio 2022** (Windows)

## Building

### First-time setup

```bash
git submodule update --init --recursive
```

### Windows

```bash
# Configure (Ninja)
cmake -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build everything (executable + dependencies)
cmake --build build/Release

# Install to dist/
cmake --install build/Release --prefix dist/Release/ImNeovim
```

### Linux

```bash
# Configure
cmake -B build/Release -DCMAKE_BUILD_TYPE=Release

# Build everything
cmake --build build/Release -j$(nproc)

# Install to dist/
cmake --install build/Release --prefix dist/Release/ImNeovim
```

### macOS

```bash
# Configure
cmake -B build/Release -DCMAKE_BUILD_TYPE=Release

# Build everything (produces ImNeovim.app in the build directory)
cmake --build build/Release -j$(nproc)
```

## License

ImNeovim is licensed under the **Apache License, Version 2.0**.
See [LICENSE](LICENSE) for the full text.

Third-party components distributed with ImNeovim are covered by their own
licenses. See [NOTICE](NOTICE) for attribution details and the
`licenses/` directory in the distribution for full license texts.

