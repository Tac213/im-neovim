# cmake/InstallLicenses.cmake
#
# Prepares third-party license files and the project's own LICENSE + NOTICE
# for distribution.  Call prepare_license_files() once after
# add_subdirectory(thirdparty) has completed.  Then use:
# - install_license_files()       for Windows / Linux install() rules
# - bundle_licenses_macos()       for macOS POST_BUILD .app bundling

# ---------------------------------------------------------------------------
# Internal helper -- copy a license file into the staging directory
# ---------------------------------------------------------------------------
function(_stage_license SRC DST_NAME)
    if(EXISTS "${SRC}")
        configure_file("${SRC}" "${CMAKE_BINARY_DIR}/licenses/${DST_NAME}" COPYONLY)
    else()
        message(WARNING "License file not found, skipping: ${SRC}")
    endif()
endfunction()

# ---------------------------------------------------------------------------
# prepare_license_files()
#
# Copies every third-party license into ${CMAKE_BINARY_DIR}/licenses/ so
# that a single install(DIRECTORY …) or copy_directory can ship them all.
# Must be called AFTER add_subdirectory(thirdparty) because some sources
# only exist after the download steps complete.
# ---------------------------------------------------------------------------
function(prepare_license_files)
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/licenses")

    # -- Source-tree licenses (git submodules) --------------------------------
    _stage_license("${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui/LICENSE.txt"
        "imgui-MIT.txt")
    _stage_license("${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/glew/LICENSE.txt"
        "glew-BSD3.txt")
    _stage_license("${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/stb/LICENSE"
        "stb-MIT.txt")
    _stage_license("${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/nanosvg/LICENSE.txt"
        "nanosvg-zlib.txt")
    _stage_license("${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/fmt/LICENSE"
        "fmt-MIT.txt")
    _stage_license("${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/spdlog/LICENSE"
        "spdlog-MIT.txt")
    _stage_license("${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libvterm/LICENSE"
        "libvterm-MIT.txt")

    # Platform-specific submodule licenses
    if(WIN32)
        _stage_license("${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/DirectX-Headers/LICENSE"
            "DirectX-Headers-MIT.txt")
    else()
        _stage_license("${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/glfw/LICENSE.md"
            "glfw-zlib.txt")
    endif()

    # -- Build-tree licenses (downloaded at configure time) -------------------
    _stage_license("${CMAKE_BINARY_DIR}/thirdparty/freetype/freetype-src/LICENSE.TXT"
        "freetype-FTL.txt")

    if(EXISTS "${CMAKE_BINARY_DIR}/thirdparty/freetype/freetype-src/docs/FTL.TXT")
        _stage_license("${CMAKE_BINARY_DIR}/thirdparty/freetype/freetype-src/docs/FTL.TXT"
            "freetype-FTL-detail.txt")
    endif()

    _stage_license("${CMAKE_BINARY_DIR}/thirdparty/libuv/libuv-src/LICENSE"
        "libuv-MIT.txt")
    _stage_license("${CMAKE_BINARY_DIR}/thirdparty/msgpack-c/msgpack-c-src/LICENSE_1_0.txt"
        "msgpack-c-BSL.txt")
    _stage_license("${CMAKE_BINARY_DIR}/thirdparty/neovim-src/LICENSE.txt"
        "neovim-Apache2.txt")

    # -- tinyfiledialogs: license is embedded in the source header ------------
    set(_tfd_src
        "${CMAKE_BINARY_DIR}/thirdparty/tinyfiledialogs/tinyfiledialogs-src/tinyfiledialogs.c")

    if(EXISTS "${_tfd_src}")
        file(READ "${_tfd_src}" _tfd_content LIMIT 4096)

        # The zlib license block ends with the standard "3. This notice..."
        # clause followed by the closing C comment.
        string(FIND "${_tfd_content}"
            "3. This notice may not be removed or altered from any source distribution."
            _tfd_end)

        if(_tfd_end GREATER -1)
            # Search for the closing "*/" in the tail after the license text
            string(SUBSTRING "${_tfd_content}" ${_tfd_end} -1 _tfd_tail)
            string(FIND "${_tfd_tail}" "*/" _tfd_close_rel)

            if(_tfd_close_rel GREATER -1)
                math(EXPR _tfd_cut "${_tfd_end} + ${_tfd_close_rel} + 2")
                string(SUBSTRING "${_tfd_content}" 0 ${_tfd_cut} _tfd_license)

                # Drop the opening /* line so the file starts cleanly
                string(REGEX REPLACE "^/\\*[^\n]*\n" "" _tfd_license
                    "${_tfd_license}")
                string(STRIP "${_tfd_license}" _tfd_license)
                file(WRITE "${CMAKE_BINARY_DIR}/licenses/tinyfiledialogs-zlib.txt"
                    "${_tfd_license}\n")
            endif()
        endif()
    else()
        message(WARNING
            "tinyfiledialogs source not found at ${_tfd_src} -- license will be missing")
    endif()
endfunction()

# ---------------------------------------------------------------------------
# install_license_files()
#
# Installs the staged licenses/ directory + the project's LICENSE and NOTICE
# into the install prefix.  Call inside the appropriate platform install block.
# ---------------------------------------------------------------------------
function(install_license_files)
    # Third-party licenses (staged by prepare_license_files)
    install(DIRECTORY "${CMAKE_BINARY_DIR}/licenses/"
        DESTINATION licenses
        USE_SOURCE_PERMISSIONS)

    # Project's own license and notice
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        DESTINATION .
        RENAME LICENSE.txt)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/NOTICE"
        DESTINATION .)
endfunction()

# ---------------------------------------------------------------------------
# bundle_licenses_macos()
#
# Copies the staged licenses/ directory + LICENSE/NOTICE into the macOS .app
# bundle's Resources folder.  Call once during CMake configure; the actual
# copy happens as a POST_BUILD step on the imnvim target.
# ---------------------------------------------------------------------------
function(bundle_licenses_macos)
    # Copy the project LICENSE and NOTICE into the staging area so one
    # copy_directory command ships everything.
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        "${CMAKE_BINARY_DIR}/licenses_staging/LICENSE.txt" COPYONLY)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/NOTICE"
        "${CMAKE_BINARY_DIR}/licenses_staging/NOTICE" COPYONLY)

    add_custom_command(TARGET imnvim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_BINARY_DIR}/licenses"
        "$<TARGET_BUNDLE_CONTENT_DIR:imnvim>/Resources/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_BINARY_DIR}/licenses_staging"
        "$<TARGET_BUNDLE_CONTENT_DIR:imnvim>/Resources"
        COMMENT "Bundling license files into ImNeovim.app/Contents/Resources/..."
    )
endfunction()
