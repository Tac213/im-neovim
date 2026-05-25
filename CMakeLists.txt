cmake_minimum_required(VERSION 3.24)
project(ImNeovim VERSION 0.1.0)

add_subdirectory(thirdparty)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(
        FATAL_ERROR
        "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
    )
endif()

set(public_dir "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(im_app_dir "${CMAKE_CURRENT_SOURCE_DIR}/src/im_app")
set(im_app_private_header_dir "${im_app_dir}/interfaces")

set(im_app_public_files
    "${public_dir}/im_app/layer.h"
    "${public_dir}/im_app/application.h"
    "${public_dir}/im_app/file_system.h"
    "${public_dir}/im_app/pty.h"
    "${public_dir}/im_app/font_manager.h"
    "${public_dir}/im_app/image_manager.h"
)

set(im_app_private_files
    "${im_app_private_header_dir}/im_app/graphics_context.h"
    "${im_app_private_header_dir}/im_app/imgui_renderer.h"
    "${im_app_private_header_dir}/im_app/window.h"
    "${im_app_dir}/application.cpp"
    "${im_app_dir}/font_manager.cpp"
    "${im_app_dir}/image_manager.cpp"
)

set(im_app_platform_specific_files)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    set(im_app_platform_dir "${im_app_dir}/platforms/win32")
    list(APPEND im_app_platform_specific_files
        "${im_app_platform_dir}/win32_file_system.cpp"
        "${im_app_platform_dir}/wgl_context.h"
        "${im_app_platform_dir}/wgl_context.cpp"
        "${im_app_platform_dir}/win32_window.h"
        "${im_app_platform_dir}/win32_window.cpp"
        "${im_app_platform_dir}/win32_opengl_imgui_renderer.h"
        "${im_app_platform_dir}/win32_opengl_imgui_renderer.cpp"
        "${im_app_platform_dir}/main.cpp"
        "${im_app_platform_dir}/dx12_context.h"
        "${im_app_platform_dir}/dx12_context.cpp"
        "${im_app_platform_dir}/dx12_imgui_renderer.h"
        "${im_app_platform_dir}/dx12_imgui_renderer.cpp"
        "${im_app_platform_dir}/win32_graphics_context.cpp"
        "${im_app_platform_dir}/win32_imgui_renderer.cpp"
        "${im_app_platform_dir}/win32_pty.h"
        "${im_app_platform_dir}/win32_pty.cpp"
        "${im_app_platform_dir}/win32_font_manager.cpp"
    )
endif()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    set(im_app_platform_dir "${im_app_dir}/platforms/linux")
    list(APPEND im_app_platform_specific_files
        "${im_app_platform_dir}/linux_file_system.cpp"
        "${im_app_platform_dir}/glfw_window.h"
        "${im_app_platform_dir}/glfw_window.cpp"
        "${im_app_platform_dir}/glfw_context.h"
        "${im_app_platform_dir}/glfw_context.cpp"
        "${im_app_platform_dir}/glfw_opengl_imgui_renderer.h"
        "${im_app_platform_dir}/glfw_opengl_imgui_renderer.cpp"
        "${im_app_platform_dir}/main.cpp"
        "${im_app_platform_dir}/linux_pty.h"
        "${im_app_platform_dir}/linux_pty.cpp"
        "${im_app_platform_dir}/linux_font_manager.cpp"
    )
endif()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    find_library(METAL_FRAMEWORK Metal required)
    find_library(QUARTZ_CORE_FRAMEWORK QuartzCore required)
    set(im_app_platform_dir "${im_app_dir}/platforms/darwin")
    list(APPEND im_app_platform_specific_files
        "${im_app_platform_dir}/darwin_file_system.mm"
        "${im_app_platform_dir}/darwin_window.h"
        "${im_app_platform_dir}/darwin_window.cpp"
        "${im_app_platform_dir}/metal_context.h"
        "${im_app_platform_dir}/metal_context.mm"
        "${im_app_platform_dir}/darwin_metal_imgui_renderer.h"
        "${im_app_platform_dir}/darwin_metal_imgui_renderer.mm"
        "${im_app_platform_dir}/main.cpp"
        "${im_app_platform_dir}/darwin_pty.h"
        "${im_app_platform_dir}/darwin_pty.cpp"
        "${im_app_platform_dir}/darwin_font_manager.mm"
    )
endif()

add_library(im_app STATIC
    ${im_app_public_files}
    ${im_app_private_files}
    ${im_app_platform_specific_files}
)
add_library(ImApp::ImApp ALIAS im_app)

target_include_directories(
    im_app
    PUBLIC
    "${public_dir}"
    PRIVATE
    "${im_app_private_header_dir}"
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb"
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/nanosvg/src"
)

target_link_libraries(
    im_app
    PUBLIC
    spdlog::spdlog
    imgui
    PRIVATE
    GLEW::glew
    freetype
)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    target_link_libraries(
        im_app
        PRIVATE
        opengl32.lib
        d3d12.lib
        dxgi.lib
        d3dcompiler.lib
        dxguid.lib
        Microsoft::DirectX-Headers
    )
    target_compile_definitions(im_app PUBLIC
        IM_APP_WIN32=1
    )
endif()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    target_link_libraries(
        im_app
        PRIVATE
        glfw
    )
    target_compile_definitions(im_app PUBLIC
        IM_APP_LINUX=1
    )
endif()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    target_link_libraries(
        im_app
        PRIVATE
        glfw
        ${METAL_FRAMEWORK}
        ${QUARTZ_CORE_FRAMEWORK}
    )
    target_compile_definitions(im_app PUBLIC
        IM_APP_DARWIN=1
    )
endif()

if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    target_compile_definitions(im_app PRIVATE
        IM_APP_DEBUG=1
    )
endif()

set(im_neovim_dir "${CMAKE_CURRENT_SOURCE_DIR}/src/im_neovim")
set(im_neovim_private_header_dir "${im_neovim_dir}/include")

set(im_neovim_private_files
    "${im_neovim_private_header_dir}/im_neovim/logging.h"
    "${im_neovim_private_header_dir}/im_neovim/globals.h"
    "${im_neovim_private_header_dir}/im_neovim/signal.h"
    "${im_neovim_private_header_dir}/im_neovim/gui/text_widget.h"
    "${im_neovim_private_header_dir}/im_neovim/gui/terminal.h"
    "${im_neovim_private_header_dir}/im_neovim/gui/nvim_widget.h"
    "${im_neovim_private_header_dir}/im_neovim/gui/nvim_input.h"
    "${im_neovim_private_header_dir}/im_neovim/gui/file_tree_widget.h"
    "${im_neovim_private_header_dir}/im_neovim/gui/dock_space_layout.h"
    "${im_neovim_dir}/im_neovim_app.cpp"
    "${im_neovim_dir}/globals.cpp"
    "${im_neovim_dir}/gui/text_widget.cpp"
    "${im_neovim_dir}/gui/terminal.cpp"
    "${im_neovim_dir}/gui/nvim_widget.cpp"
    "${im_neovim_dir}/gui/nvim_input.cpp"
    "${im_neovim_dir}/gui/file_tree_widget.cpp"
    "${im_neovim_dir}/gui/dock_space_layout.cpp"
    "${im_neovim_dir}/layers/layer_libuv.h"
    "${im_neovim_dir}/layers/layer_libuv.cpp"
    "${im_neovim_dir}/layers/layer_main_window.h"
    "${im_neovim_dir}/layers/layer_main_window.cpp"
)

add_executable(imnvim
    ${im_neovim_private_files}
)
add_executable(ImNvim::App ALIAS imnvim)

target_link_libraries(
    imnvim
    PRIVATE
    ImApp::ImApp
    libvterm
    libuv::libuv
    msgpack-cxx
    tinyfiledialogs
)

target_include_directories(
    imnvim
    PRIVATE
    ${im_neovim_private_header_dir}
)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    set_target_properties(imnvim PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "ImNeovim"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "nvim.icns"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/assets/Info.plist"
    )
    set(icon_file "${CMAKE_CURRENT_SOURCE_DIR}/assets/nvim.icns")
    target_sources(imnvim PRIVATE "${icon_file}")
    set_source_files_properties("${icon_file}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )

    # Bundle the Neovim installation into the .app bundle so it can be
    # spawned at runtime.  Tolerant of missing source: no-op when nvim
    # hasn't been built yet.
    add_custom_command(TARGET imnvim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_BINARY_DIR}/nvim"
        "$<TARGET_BUNDLE_CONTENT_DIR:imnvim>/Resources/nvim"
        COMMENT "Bundling Neovim into ImNeovim.app/Contents/Resources/..."
    )
endif()

if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    target_compile_definitions(imnvim PRIVATE
        IM_NVIM_DEBUG=1
    )
endif()

# --- Windows resource file (.rc) — all build types ---
if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    # Generate the .rc file with absolute icon path.
    set(IMNVIM_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/nvim.ico")
    set(IMNVIM_MANIFEST_PATH
        "${CMAKE_CURRENT_SOURCE_DIR}/src/im_neovim/imnvim.manifest")
    set(IMNVIM_RC_IN "${im_neovim_dir}/imnvim.rc.in")
    set(IMNVIM_RC "${CMAKE_CURRENT_BINARY_DIR}/im_neovim/imnvim.rc")
    configure_file("${IMNVIM_RC_IN}" "${IMNVIM_RC}" @ONLY)
    target_sources(imnvim PRIVATE "${IMNVIM_RC}")

    # RC compiler needs to find resource.h (next to imnvim.rc.in).
    target_include_directories(imnvim PRIVATE "${im_neovim_dir}")

    # Use our custom manifest (DPI awareness, UTF-8) instead of the
    # linker's auto-generated one.  /MANIFEST:NO disables the linker's
    # own manifest so it doesn't conflict with the one embedded via .rc.
    target_link_options(imnvim PRIVATE "/MANIFEST:NO")
endif()

if(${CMAKE_BUILD_TYPE} STREQUAL "Release")
    target_compile_definitions(im_app PRIVATE
        IM_APP_NO_CONSOLE=1
    )

    if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
        set_target_properties(imnvim PROPERTIES WIN32_EXECUTABLE 1)
    endif()
endif()

# --- Install rules for Windows shipping (portable folder) ---
if(WIN32)
    # Main executable
    install(TARGETS imnvim RUNTIME DESTINATION .)

    # MSVC runtime DLLs — compiler-provided runtime libraries are not part of
    # $<TARGET_RUNTIME_DLLS:...>, so use CMake's runtime-library helper.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ".")

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(CMAKE_INSTALL_DEBUG_LIBRARIES TRUE)
        set(CMAKE_INSTALL_DEBUG_LIBRARIES_ONLY TRUE)
    endif()

    include(InstallRequiredSystemLibraries)

    # Runtime DLLs from non-system shared library targets, if any are added later.
    install(FILES $<TARGET_RUNTIME_DLLS:imnvim> DESTINATION .)

    # Bundled Neovim runtime (installed alongside the executable)
    install(DIRECTORY "${CMAKE_BINARY_DIR}/nvim/"
        DESTINATION nvim
        USE_SOURCE_PERMISSIONS)
endif()

# --- Install rules for Linux shipping (AppImage AppDir) ---
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Main executable
    install(TARGETS imnvim RUNTIME DESTINATION .)

    # Bundled Neovim runtime (installed alongside the executable).
    # The app locates nvim at runtime via: <exe_dir>/nvim/bin/nvim
    install(DIRECTORY "${CMAKE_BINARY_DIR}/nvim/"
        DESTINATION nvim
        USE_SOURCE_PERMISSIONS)

    # --- AppImage metadata files ---

    # Desktop entry (generated from template)
    set(DESKTOP_FILE_IN "${CMAKE_CURRENT_SOURCE_DIR}/assets/imnvim.desktop.in")
    set(DESKTOP_FILE_OUT "${CMAKE_CURRENT_BINARY_DIR}/imnvim.desktop")
    configure_file("${DESKTOP_FILE_IN}" "${DESKTOP_FILE_OUT}" @ONLY)
    install(FILES "${DESKTOP_FILE_OUT}" DESTINATION .)

    # AppRun entry point script
    install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/assets/AppRun"
        DESTINATION .)

    # Application icon (same PNG used on Windows/macOS)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/nvim.png"
        DESTINATION .
        RENAME imnvim.png)

    # --- To create the AppImage from the installed AppDir: ---
    # 1. Build in Release mode:
    # cmake -B build/Release -DCMAKE_BUILD_TYPE=Release
    # cmake --build build/Release -j$(nproc) --target imnvim
    # 2. Install into a staging AppDir:
    # cmake --install build/Release --prefix build/Release/AppDir
    # 3. (one-time) Download appimagetool:
    # wget https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage
    # chmod +x appimagetool-x86_64.AppImage
    # 4. Package:
    # ARCH=x86_64 ./appimagetool-x86_64.AppImage build/Release/AppDir ImNeovim-x86_64.AppImage
    #
    # The resulting ImNeovim-x86_64.AppImage is a self-contained,
    # portable binary that runs on any modern Linux desktop.
endif()
