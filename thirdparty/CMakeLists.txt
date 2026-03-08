set(third_party_folder "${CMAKE_CURRENT_SOURCE_DIR}")

if(NOT TARGET fmt)
    option(FMT_INSTALL "" OFF)
    add_subdirectory(fmt)
    set_target_properties(fmt PROPERTIES FOLDER ${third_party_folder}/fmt)
endif()

if(NOT TARGET spdlog)
    option(SPDLOG_BUILD_EXAMPLE "" OFF)
    option(SPDLOG_INSTALL "" OFF)
    option(SPDLOG_FMT_EXTERNAL "" ON)

    if(WIN32)
        option(SPDLOG_WCHAR_SUPPORT "" ON)
        option(SPDLOG_WCHAR_FILENAMES "" ON)
    endif()

    set(CMAKE_POSITION_INDEPENDENT_CODE 1)
    add_subdirectory(spdlog)
    set_target_properties(spdlog PROPERTIES FOLDER ${third_party_folder}/spdlog)
endif()

if((${CMAKE_SYSTEM_NAME} STREQUAL "Linux" OR ${CMAKE_SYSTEM_NAME} STREQUAL "Darwin") AND NOT TARGET glfw)
    option(GLFW_BUILD_EXAMPLES "" OFF)
    option(GLFW_BUILD_TESTS "" OFF)
    option(GLFW_BUILD_DOCS "" OFF)
    option(GLFW_INSTALL "" OFF)
    add_subdirectory(glfw)
    set_target_properties(glfw PROPERTIES FOLDER ${third_party_folder}/glfw)
    set_target_properties(update_mappings PROPERTIES FOLDER ${third_party_folder}/glfw)
endif()

if(NOT TARGET imgui)
    include(imgui.cmake)
    set_target_properties(imgui PROPERTIES FOLDER ${third_party_folder}/imgui)
endif()

if(NOT TARGET GLEW::glew)
    if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
        option(GLEW_EGL "" ON)
    endif()

    add_subdirectory(glew)
    set_target_properties(glew_s PROPERTIES FOLDER ${third_party_folder}/glew)
endif()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    if(NOT TARGET DirectX-Headers)
        option(DXHEADERS_BUILD_TEST "" OFF)
        option(DXHEADERS_INSTALL "" OFF)
        option(DXHEADERS_BUILD_GOOGLE_TEST "" OFF)
        add_subdirectory(DirectX-Headers)
        set_target_properties(DirectX-Headers PROPERTIES FOLDER ${third_party_folder}/DirectX-Headers)
        set_target_properties(DirectX-Guids PROPERTIES FOLDER ${third_party_folder}/DirectX-Headers)
    endif()
endif()

if(NOT TARGET libvterm)
    include(libvterm.cmake)
    set_target_properties(libvterm PROPERTIES FOLDER ${third_party_folder}/libvterm)
endif()

if(NOT TARGET nvim)
    set(NVIM_URL "https://github.com/neovim/neovim/archive/refs/tags/nightly.zip")
    set(NVIM_SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/neovim-src")
    set(NVIM_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/neovim-build")
    set(NVIM_INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}/../nvim")

    message(STATUS ${PROJECT_BINARY_DIR})
    message(STATUS ${CMAKE_CURRENT_BINARY_DIR})

    include(ExternalProject)

    if(MSVC)
        ExternalProject_Add(neovim
            URL "${NVIM_URL}"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            SOURCE_DIR "${NVIM_SOURCE_DIR}"
            BINARY_DIR "${NVIM_BINARY_DIR}"
            CMAKE_ARGS "-DCMAKE_C_FLAGS=/utf-8" "-DCMAKE_INSTALL_PREFIX=${NVIM_INSTALL_DIR}"
            LOG_DOWNLOAD ON
            LOG_CONFIGURE ON
            LOG_BUILD ON
            LOG_INSTALL ON
        )
    else()
        ExternalProject_Add(neovim
            URL "${NVIM_URL}"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            SOURCE_DIR "${NVIM_SOURCE_DIR}"
            BINARY_DIR "${NVIM_BINARY_DIR}"
            CMAKE_ARGS "-DCMAKE_INSTALL_PREFIX=${NVIM_INSTALL_DIR}"
            LOG_DOWNLOAD ON
            LOG_CONFIGURE ON
            LOG_BUILD ON
            LOG_INSTALL ON
        )
    endif()

    ExternalProject_Add_Step(neovim configure_deps
        COMMAND ${CMAKE_COMMAND} -G ${CMAKE_GENERATOR}
        -D CMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -D CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -D CMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -D CMAKE_C_FLAGS=${CMAKE_C_FLAGS}
        -D CMAKE_C_FLAGS_DEBUG=${CMAKE_C_FLAGS_DEBUG}
        -D CMAKE_C_FLAGS_MINSIZEREL=${CMAKE_C_FLAGS_MINSIZEREL}
        -D CMAKE_C_FLAGS_RELWITHDEBINFO=${CMAKE_C_FLAGS_RELWITHDEBINFO}
        -D CMAKE_C_FLAGS_RELEASE=${CMAKE_C_FLAGS_RELEASE}
        -D CMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
        -S ${NVIM_SOURCE_DIR}/cmake.deps
        -B ${NVIM_SOURCE_DIR}/.deps
        COMMENT "Performing configure deps step for 'neovim'"
        DEPENDEES patch
        LOG ON
        WORKING_DIRECTORY ${NVIM_SOURCE_DIR}
    )
    ExternalProject_Add_Step(neovim build_deps
        COMMAND ${CMAKE_COMMAND} --build ${NVIM_SOURCE_DIR}/.deps --config ${CMAKE_BUILD_TYPE}
        COMMENT "Performing build deps step for 'neovim'"
        DEPENDEES configure_deps
        DEPENDERS configure
        LOG ON
        WORKING_DIRECTORY ${NVIM_SOURCE_DIR}
    )

    if(MSVC)
        set(LIB_NVIM_PATH "${NVIM_BINARY_DIR}/lib/nvim.lib")
    else()
        set(LIB_NVIM_PATH "${NVIM_BINARY_DIR}/lib/libnvim.a")
    endif()
endif()

if(NOT TARGET libuv)
    add_subdirectory(libuv)
endif()

if(NOT TARGET msgpack-c)
    add_subdirectory(msgpack-c)
endif()
