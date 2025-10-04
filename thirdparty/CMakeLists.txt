set(third_party_folder "${CMAKE_CURRENT_SOURCE_DIR}")

if(NOT TARGET imgui)
    include(imgui.cmake)
    set_target_properties(imgui PROPERTIES FOLDER ${third_party_folder}/imgui)
endif()

if(NOT TARGET GLEW::glew)
    if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
        option(GLEW_EGL "" ON)
    endif()

    add_subdirectory(glew)
    set_target_properties(glew_s PROPERTIES FOLDER ${THIRD_PARTY_FOLDER}/glew)
endif()
