# Download and unpack libuv at configure time
configure_file(CMakeLists.txt.in libuv-download/CMakeLists.txt)

# Call CMake to download and libuv
execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
    RESULT_VARIABLE result
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/libuv-download)

if(result)
    message(FATAL_ERROR "CMake step for libuv failed: ${result}")
endif()

# Build the downloaded libuv
execute_process(COMMAND ${CMAKE_COMMAND} --build .
    RESULT_VARIABLE result
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/libuv-download)

if(result)
    message(FATAL_ERROR "Build step for libuv failed: ${result}")
endif()

# Add libuv directly to our build.
# This defines the libuv::libuv target.
option(LIBUV_BUILD_SHARED "" OFF)
option(UV_LINT_W4 "" OFF)
add_subdirectory(${CMAKE_CURRENT_BINARY_DIR}/libuv-src ${CMAKE_CURRENT_BINARY_DIR}/libuv-build)
