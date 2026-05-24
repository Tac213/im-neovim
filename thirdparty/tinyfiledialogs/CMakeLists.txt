# Download and unpack tinyfiledialogs at configure time
configure_file(CMakeLists.txt.in tinyfiledialogs-download/CMakeLists.txt)

# Call CMake to download tinyfiledialogs
execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
    RESULT_VARIABLE result
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tinyfiledialogs-download)

if(result)
    message(FATAL_ERROR "CMake step for tinyfiledialogs failed: ${result}")
endif()

# Build the downloaded tinyfiledialogs
execute_process(COMMAND ${CMAKE_COMMAND} --build .
    RESULT_VARIABLE result
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tinyfiledialogs-download)

if(result)
    message(FATAL_ERROR "Build step for tinyfiledialogs failed: ${result}")
endif()

# Create a static library from the downloaded source.
# tinyfiledialogs is a single .c file, not a CMake project,
# so we build it manually instead of using add_subdirectory().
add_library(tinyfiledialogs STATIC
    ${CMAKE_CURRENT_BINARY_DIR}/tinyfiledialogs-src/tinyfiledialogs.c
)
target_include_directories(tinyfiledialogs PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}/tinyfiledialogs-src
)

# On Linux, tinyfiledialogs uses dlopen/dlsym for runtime GTK detection.
# No GTK build dependency — falls back to zenity at runtime.
if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    target_link_libraries(tinyfiledialogs PUBLIC ${CMAKE_DL_LIBS})
endif()
