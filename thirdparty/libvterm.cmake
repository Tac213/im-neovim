set(libvterm_source_dir ${CMAKE_CURRENT_SOURCE_DIR}/libvterm/src)
set(libvterm_include_dir ${CMAKE_CURRENT_SOURCE_DIR}/libvterm/include)

set(libvterm_headers
    ${libvterm_include_dir}/vterm.h
    ${libvterm_include_dir}/vterm_keycodes.h
)

set(libvterm_sources
    ${libvterm_source_dir}/vterm_internal.h
    ${libvterm_source_dir}/utf8.h
    ${libvterm_source_dir}/rect.h
    ${libvterm_source_dir}/vterm.c
    ${libvterm_source_dir}/encoding.c
    ${libvterm_source_dir}/keyboard.c
    ${libvterm_source_dir}/mouse.c
    ${libvterm_source_dir}/parser.c
    ${libvterm_source_dir}/pen.c
    ${libvterm_source_dir}/screen.c
    ${libvterm_source_dir}/state.c
    ${libvterm_source_dir}/unicode.c
)

add_library(libvterm STATIC
    ${libvterm_headers}
    ${libvterm_sources}
)

target_include_directories(libvterm
    PUBLIC
    ${libvterm_include_dir}
)
