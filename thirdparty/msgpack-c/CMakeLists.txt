# Download and unpack msgpack-c at configure time
configure_file(CMakeLists.txt.in msgpack-c-download/CMakeLists.txt)

# Call CMake to download and msgpack-c
execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
    RESULT_VARIABLE result
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/msgpack-c-download)

if(result)
    message(FATAL_ERROR "CMake step for msgpack-c failed: ${result}")
endif()

# Build the downloaded msgpack-c
execute_process(COMMAND ${CMAKE_COMMAND} --build .
    RESULT_VARIABLE result
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/msgpack-c-download)

if(result)
    message(FATAL_ERROR "Build step for msgpack-c failed: ${result}")
endif()

# Add msgpack-c directly to our build.
# This defines the msgpack-cxx target.
option(MSGPACK_CXX11 "" OFF)
option(MSGPACK_CXX20 "" ON)
option(MSGPACK_USE_BOOST "" OFF)
option(MSGPACK_BUILD_DOCS "" OFF)
add_subdirectory(${CMAKE_CURRENT_BINARY_DIR}/msgpack-c-src ${CMAKE_CURRENT_BINARY_DIR}/msgpack-c-build)
