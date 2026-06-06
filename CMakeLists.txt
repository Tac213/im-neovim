cmake_minimum_required(VERSION 3.24)
project(ImNeovim VERSION 0.1.0)

# -- Capture build metadata --
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE IMNVIM_GIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

if(NOT IMNVIM_GIT_SHA)
    set(IMNVIM_GIT_SHA "unknown")
endif()

string(TIMESTAMP IMNVIM_BUILD_DATE "%Y-%m-%d %H:%M:%S UTC" UTC)

add_subdirectory(thirdparty)

include(cmake/InstallLicenses.cmake)
prepare_license_files()

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
    "${im_neovim_dir}/gui/about_panel.cpp"
    "${im_neovim_private_header_dir}/im_neovim/gui/about_panel.h"
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
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

# -- Generate build_config.h from template --
set(BUILD_CONFIG_IN "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_config.h.in")
set(BUILD_CONFIG_OUT
    "${CMAKE_CURRENT_BINARY_DIR}/generated/im_neovim/build_config.h")
configure_file("${BUILD_CONFIG_IN}" "${BUILD_CONFIG_OUT}" @ONLY)

# -- Parse assets/imnvim.assets and generate embedded asset headers --
set(IMNVIM_ASSETS_RC "${CMAKE_CURRENT_SOURCE_DIR}/assets/imnvim.assets")

if(EXISTS "${IMNVIM_ASSETS_RC}")
    file(STRINGS "${IMNVIM_ASSETS_RC}" ASSET_LINES)
    set(ASSET_HEADERS "")
    set(ASSET_REGISTRATIONS "")

    foreach(line IN LISTS ASSET_LINES)
        string(STRIP "${line}" line)

        if(line STREQUAL "" OR line MATCHES "^#")
            continue()
        endif()

        # Parse: TYPE VIRTUAL_PATH SOURCE_PATH
        if(line MATCHES "^(IMAGE|FONT) +([^ ]+) +(.+)$")
            set(ASSET_TYPE "${CMAKE_MATCH_1}")
            set(ASSET_VPATH "${CMAKE_MATCH_2}")
            set(ASSET_SPATH "${CMAKE_MATCH_3}")

            if(ASSET_TYPE STREQUAL "IMAGE")
                # Read the source file as hex
                file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${ASSET_SPATH}"
                    ASSET_HEX HEX)

                # Convert hex string to C array: "89504e47..." → "0x89, 0x50,
                # ..."
                string(REGEX REPLACE "([0-9a-fA-F][0-9a-fA-F])" "0x\\1, "
                    ASSET_HEX_ARR "${ASSET_HEX}")

                # Sanitize virtual path to a valid C identifier
                string(REGEX REPLACE "[:/\\.-]" "_" ASSET_SYMBOL
                    "${ASSET_VPATH}")

                # Strip leading underscores
                string(REGEX REPLACE "^_+" "" ASSET_SYMBOL
                    "${ASSET_SYMBOL}")

                # Strip trailing comma+space from hex array
                string(REGEX REPLACE ", $" "" ASSET_HEX_ARR
                    "${ASSET_HEX_ARR}")

                set(ASSET_HEADER
                    "${CMAKE_CURRENT_BINARY_DIR}/generated/imnvim_assets/${ASSET_SYMBOL}.h"
                )

                # Generate per-asset header
                file(WRITE "${ASSET_HEADER}"
                    "#pragma once\n"
                    "#include <cstddef>\n"
                    "static const unsigned char ${ASSET_SYMBOL}[] = { ${ASSET_HEX_ARR} };\n"
                    "static const size_t ${ASSET_SYMBOL}_size = sizeof(${ASSET_SYMBOL});\n"
                )

                list(APPEND ASSET_HEADERS "${ASSET_SYMBOL}.h")
                list(APPEND ASSET_REGISTRATIONS
                    "    ImApp::ImageManager::register_embedded_image(\"${ASSET_VPATH}\", ${ASSET_SYMBOL}, ${ASSET_SYMBOL}_size)"
                )
            endif()
        endif()
    endforeach()

    # Generate master header with registration macro
    set(MASTER_HEADER
        "${CMAKE_CURRENT_BINARY_DIR}/generated/imnvim_assets/imnvim_assets.h")
    set(ASSET_INCLUDES "")

    foreach(hdr IN LISTS ASSET_HEADERS)
        string(APPEND ASSET_INCLUDES
            "#include \"imnvim_assets/${hdr}\"\n")
    endforeach()

    # Build registration body with \ line continuations in the template.
    # Each registration is plain (no trailing \), the template provides them.
    # Use a real newline character as the join separator.
    string(ASCII 10 LF)
    string(JOIN "${LF}" ASSET_REG_BODY ${ASSET_REGISTRATIONS})

    # Use bracket argument + string(CONFIGURE) to avoid CMake escape issues
    # with backslash-newline continuations in the generated C macro.
    # The ; is in the template (not in ASSET_REG_BODY) because CMake treats
    # semicolons as list separators.
    set(MASTER_TEMPLATE [=[
#pragma once
#include <im_app/image_manager.h>
@ASSET_INCLUDES@
#define IMNVIM_REGISTER_EMBEDDED_ASSETS() \
    do { \
@ASSET_REG_BODY@;\
    } while(0)
]=])
    string(CONFIGURE "${MASTER_TEMPLATE}" MASTER_CONTENT @ONLY)
    file(WRITE "${MASTER_HEADER}" "${MASTER_CONTENT}")
endif()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    set_target_properties(imnvim PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "ImNeovim"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "nvim.icns"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/assets/Info.plist"
    )
    target_sources(imnvim PRIVATE
        "${im_neovim_dir}/platforms/darwin_menu_bridge.mm"
        "${im_neovim_private_header_dir}/im_neovim/platforms/darwin_menu_bridge.h"
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

    bundle_licenses_macos()
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
        "${CMAKE_CURRENT_SOURCE_DIR}/src/im_neovim/platforms/imnvim.manifest")
    set(IMNVIM_RC_IN "${im_neovim_dir}/platforms/imnvim.rc.in")
    set(IMNVIM_RC "${CMAKE_CURRENT_BINARY_DIR}/im_neovim/imnvim.rc")
    configure_file("${IMNVIM_RC_IN}" "${IMNVIM_RC}" @ONLY)
    target_sources(imnvim PRIVATE "${IMNVIM_RC}")

    # RC compiler needs to find resource.h (next to imnvim.rc.in).
    target_include_directories(imnvim PRIVATE "${im_neovim_dir}/platforms")

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
if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
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

    # License files
    install_license_files()
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

    # License files
    install_license_files()
endif()

# --- Install rules for macOS shipping (.app bundle → .dmg) ---
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # The .app bundle is self-contained: Neovim runtime and licenses are
    # already copied into the bundle via POST_BUILD steps.  Only the
    # bundle itself needs to be installed.
    install(TARGETS imnvim BUNDLE DESTINATION .)
endif()

# --- CPack configuration ---
set(CPACK_PACKAGE_NAME "ImNeovim")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "ImGui-based graphical user interface for Neovim")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_VENDOR "ImNeovim Contributors")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/ImNeovim/ImNeovim")

# -- macOS: produce a .dmg disk image via the DragNDrop generator --
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "ImNeovim")
    set(CPACK_DMG_FORMAT "UDZO")
endif()

include(CPack)
