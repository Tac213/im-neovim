set(imgui_source_dir ${CMAKE_CURRENT_SOURCE_DIR}/imgui)

set(imgui_sources
    ${imgui_source_dir}/imgui_demo.cpp
    ${imgui_source_dir}/imgui_draw.cpp
    ${imgui_source_dir}/imgui_tables.cpp
    ${imgui_source_dir}/imgui_widgets.cpp
    ${imgui_source_dir}/imgui.cpp
    ${imgui_source_dir}/imconfig.h
    ${imgui_source_dir}/imgui_internal.h
    ${imgui_source_dir}/imgui.h
    ${imgui_source_dir}/imstb_rectpack.h
    ${imgui_source_dir}/imstb_textedit.h
    ${imgui_source_dir}/imstb_truetype.h
)

set(imgui_backends
    ${imgui_source_dir}/backends/imgui_impl_opengl3.cpp
    ${imgui_source_dir}/backends/imgui_impl_opengl3.h
    ${imgui_source_dir}/backends/imgui_impl_opengl3_loader.h
)

set(imgui_miscs
    ${imgui_source_dir}/misc/cpp/imgui_stdlib.cpp
    ${imgui_source_dir}/misc/cpp/imgui_stdlib.h
)

set(imgui_platform_specific_files "")

if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    set(imgui_platform_specific_files
        ${imgui_source_dir}/backends/imgui_impl_win32.cpp
        ${imgui_source_dir}/backends/imgui_impl_win32.h
    )
endif()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    set(imgui_platform_specific_files
        ${imgui_source_dir}/backends/imgui_impl_glfw.cpp
        ${imgui_source_dir}/backends/imgui_impl_glfw.h
    )
endif()

add_library(imgui STATIC
    ${imgui_sources}
    ${imgui_backends}
    ${imgui_miscs}
    ${imgui_platform_specific_files}
)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    include(FindX11)
    target_link_libraries(imgui
        PRIVATE
        glfw
        X11::X11
    )
endif()

target_include_directories(imgui
    PUBLIC
    ${imgui_source_dir}
    ${imgui_source_dir}/backends
)

add_executable(binary_to_compressed_c
    ${imgui_source_dir}/misc/fonts/binary_to_compressed_c.cpp
)

add_custom_command(TARGET binary_to_compressed_c
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:binary_to_compressed_c> ${CMAKE_CURRENT_SOURCE_DIR}/../bin/$<TARGET_FILE_NAME:binary_to_compressed_c>)
