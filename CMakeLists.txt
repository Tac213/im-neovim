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

add_executable(im_neovim
    ${im_neovim_private_files}
)
add_executable(ImNeovim::App ALIAS im_neovim)

target_link_libraries(
    im_neovim
    PRIVATE
    ImApp::ImApp
    libvterm
    libuv::libuv
    msgpack-cxx
    tinyfiledialogs
)

target_include_directories(
    im_neovim
    PRIVATE
    ${im_neovim_private_header_dir}
)

if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
    target_compile_definitions(im_neovim PRIVATE
        IM_NVIM_DEBUG=1
    )
endif()

if(${CMAKE_BUILD_TYPE} STREQUAL "Release")
    target_compile_definitions(im_app PRIVATE
        IM_APP_NO_CONSOLE=1
    )

    if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
        set_target_properties(im_neovim PROPERTIES WIN32_EXECUTABLE 1)
    endif()
endif()
