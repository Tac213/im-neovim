# Download and unpack freetype at configure time
configure_file(CMakeLists.txt.in freetype-download/CMakeLists.txt)

# Call CMake to download freetype
execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
    RESULT_VARIABLE result
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/freetype-download)

if(result)
    message(FATAL_ERROR "CMake step for freetype failed: ${result}")
endif()

# Build the downloaded freetype
execute_process(COMMAND ${CMAKE_COMMAND} --build .
    RESULT_VARIABLE result
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/freetype-download)

if(result)
    message(FATAL_ERROR "Build step for freetype failed: ${result}")
endif()

# Minimal build: disable all optional features we don't need
option(FT_DISABLE_BROTLI "" ON)
option(FT_DISABLE_BZIP2 "" ON)
option(FT_DISABLE_PNG "" ON)
option(FT_DISABLE_HARFBUZZ "" ON)
option(FT_DISABLE_ZLIB "" ON)

# Add freetype directly to our build.
# This defines the freetype target.
add_subdirectory(${CMAKE_CURRENT_BINARY_DIR}/freetype-src ${CMAKE_CURRENT_BINARY_DIR}/freetype-build EXCLUDE_FROM_ALL)

# freetype source contains UTF-8 characters; MSVC requires /utf-8 to avoid C4819 warnings
if(MSVC)
    target_compile_options(freetype PRIVATE "/utf-8")
endif()
