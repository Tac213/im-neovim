cmake_minimum_required(VERSION 3.16)
project(glew LANGUAGES C)

if(NOT DEFINED CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type")
endif()

include(GNUInstallDirs)

set(CMAKE_DEBUG_POSTFIX d)

option(GLEW_BUILD_UTILS "utilities" OFF)
option(GLEW_REGAL "Regal mode" OFF)
option(GLEW_OSMESA "OSMesa mode" OFF)

if(APPLE)
  option(GLEW_BUILD_FRAMEWORK "Build Framework bundle for OSX" OFF)
endif()

option(GLEW_X11 "X11 mode" OFF)
option(GLEW_EGL "EGL mode" OFF)
option(GLEW_BUILD_SHARED_LIBS "install/link shared instead of static libs" OFF)
option(GLEW_INSTALL "Install glew" OFF)

set(GLEW_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# get version from config/version
file(STRINGS ${GLEW_DIR}/config/version _VERSION_MAJOR_STRING REGEX "GLEW_MAJOR[ ]*=[ ]*[0-9]+.*")
string(REGEX REPLACE "GLEW_MAJOR[ ]*=[ ]*([0-9]+)" "\\1" CPACK_PACKAGE_VERSION_MAJOR ${_VERSION_MAJOR_STRING})
file(STRINGS ${GLEW_DIR}/config/version _VERSION_MINOR_STRING REGEX "GLEW_MINOR[ ]*=[ ]*[0-9]+.*")
string(REGEX REPLACE "GLEW_MINOR[ ]*=[ ]*([0-9]+)" "\\1" CPACK_PACKAGE_VERSION_MINOR ${_VERSION_MINOR_STRING})
file(STRINGS ${GLEW_DIR}/config/version _VERSION_PATCH_STRING REGEX "GLEW_MICRO[ ]*=[ ]*[0-9]+.*")
string(REGEX REPLACE "GLEW_MICRO[ ]*=[ ]*([0-9]+)" "\\1" CPACK_PACKAGE_VERSION_PATCH ${_VERSION_PATCH_STRING})
set(GLEW_VERSION ${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH})

if(NOT GLEW_CUSTOM_OUTPUT_DIRS)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

find_package(OpenGL REQUIRED)

# cmake<3.10 doesn't detect EGL/GLX
if(CMAKE_VERSION VERSION_LESS 3.10)
  find_library(OPENGL_egl_LIBRARY NAMES EGL)

  if(OPENGL_egl_LIBRARY)
    set(OpenGL_EGL_FOUND TRUE)
  endif()
endif()

# prefer GLVND
if(OPENGL_opengl_LIBRARY)
  set(GLEW_LIBRARIES ${OPENGL_opengl_LIBRARY})
else()
  set(GLEW_LIBRARIES ${OPENGL_gl_LIBRARY})
endif()

# X11 required except for Windows and Apple OSX platforms
if(GLEW_X11 AND NOT WIN32 AND NOT APPLE)
  find_package(X11)
  list(APPEND GLEW_LIBRARIES ${X11_LIBRARIES})
endif()

if(WIN32)
  set(GLEW_LIB_NAME glew32)
else()
  set(GLEW_LIB_NAME GLEW)
  set(DLL_PREFIX lib)
endif()

set(_GLEW_COMPILE_DEFINITIONS "GLEW_NO_GLU")

# ### Regal mode ####
if(GLEW_REGAL)
  if(WIN32)
    set(REGAL_LIB_NAME regal32)
  else()
    set(REGAL_LIB_NAME Regal)
  endif()

  list(APPEND _GLEW_COMPILE_DEFINITIONS "GLEW_REGAL")
  set(GLEW_LIBRARIES ${REGAL_LIB_NAME})
endif()

# ### OSMesa mode ####
if(GLEW_OSMESA)
  if(WIN32)
    set(OSMESA_LIB_NAME osmesa)
  else()
    set(OSMESA_LIB_NAME OSMesa)
  endif()

  list(APPEND _GLEW_COMPILE_DEFINITIONS "GLEW_OSMESA")
  list(APPEND GLEW_LIBRARIES ${OSMESA_LIB_NAME})
  set(X11_LIBRARIES)
endif()

# ### EGL ####
if(GLEW_EGL AND UNIX)
  list(APPEND _GLEW_COMPILE_DEFINITIONS "GLEW_EGL")

  if(NOT OpenGL_EGL_FOUND)
    message(FATAL_ERROR "EGL library set but not found.")
  endif()

  list(APPEND GLEW_LIBRARIES ${OPENGL_egl_LIBRARY})
endif()

# ### GLEW ####
set(GLEW_PUBLIC_HEADERS_FILES
  ${GLEW_DIR}/include/GL/wglew.h
  ${GLEW_DIR}/include/GL/glew.h
  ${GLEW_DIR}/include/GL/glxew.h
  ${GLEW_DIR}/include/GL/eglew.h
)
set(GLEW_SRC_FILES ${GLEW_DIR}/src/glew.c)

if(WIN32)
  list(APPEND GLEW_SRC_FILES ${GLEW_DIR}/src/win32/glew.rc)
endif()

if(GLEW_BUILD_SHARED_LIBS)
  add_library(glew SHARED ${GLEW_PUBLIC_HEADERS_FILES} ${GLEW_SRC_FILES})
  set_target_properties(glew PROPERTIES COMPILE_DEFINITIONS "GLEW_BUILD" OUTPUT_NAME "${GLEW_LIB_NAME}" PREFIX "${DLL_PREFIX}"
    VERSION ${GLEW_VERSION}
    SOVERSION ${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR})
  target_include_directories(glew
    PUBLIC
    ${GLEW_DIR}/include ${X11_INCLUDE_DIR}
  )
  target_compile_definitions(glew
    PUBLIC
    ${_GLEW_COMPILE_DEFINITIONS}
  )
  add_library(GLEW::glew ALIAS glew)
else()
  add_library(glew_s STATIC ${GLEW_PUBLIC_HEADERS_FILES} ${GLEW_SRC_FILES})
  set_target_properties(glew_s PROPERTIES COMPILE_DEFINITIONS "GLEW_STATIC" OUTPUT_NAME "${GLEW_LIB_NAME}" PREFIX lib)
  target_include_directories(glew_s
    PUBLIC
    ${GLEW_DIR}/include ${X11_INCLUDE_DIR}
  )
  target_compile_definitions(glew_s
    PUBLIC
    ${_GLEW_COMPILE_DEFINITIONS}
  )
  add_library(GLEW::glew ALIAS glew_s)
endif()

if(MSVC)
  if(GLEW_BUILD_SHARED_LIBS)
    # add options from visual studio project
    target_compile_definitions(glew PRIVATE "GLEW_BUILD;VC_EXTRALEAN")
    target_link_libraries(glew LINK_PRIVATE -BASE:0x62AA0000)

    # kill security checks which are dependent on stdlib
    target_compile_options(glew PRIVATE -GS-)

    # remove stdlib dependency
    target_link_libraries(glew LINK_PRIVATE -nodefaultlib -noentry)
    target_link_libraries(glew LINK_PRIVATE libvcruntime.lib)
    target_link_libraries(glew LINK_PRIVATE msvcrt.lib)
  else()
    # add options from visual studio project
    target_compile_definitions(glew_s PRIVATE "GLEW_STATIC;VC_EXTRALEAN")

    # kill security checks which are dependent on stdlib
    target_compile_options(glew_s PRIVATE -GS-)
  endif()

  string(REGEX REPLACE "/RTC(su|[1su])" "" CMAKE_C_FLAGS_DEBUG ${CMAKE_C_FLAGS_DEBUG})
elseif(WIN32 AND((CMAKE_C_COMPILER_ID MATCHES "GNU") OR(CMAKE_C_COMPILER_ID MATCHES "Clang")))
  if(GLEW_BUILD_SHARED_LIBS)
    # remove stdlib dependency on windows with GCC and Clang (for similar reasons
    # as to MSVC - to allow it to be used with any Windows compiler)
    target_compile_options(glew PRIVATE -fno-builtin -fno-stack-protector)
    target_link_libraries(glew LINK_PRIVATE -nostdlib)
  else()
    target_compile_options(glew_s PRIVATE -fno-builtin -fno-stack-protector)
  endif()
endif()

if(GLEW_BUILD_FRAMEWORK AND GLEW_BUILD_SHARED_LIBS)
  set_target_properties(glew PROPERTIES
    FRAMEWORK TRUE
    FRAMEWORK_VERSION ${GLEW_VERSION}
    MACOSX_FRAMEWORK_IDENTIFIER net.sourceforge.glew
    MACOSX_FRAMEWORK_SHORT_VERSION_STRING ${GLEW_VERSION}
    MACOSX_FRAMEWORK_BUNDLE_VERSION ${GLEW_VERSION}
    XCODE_ATTRIBUTE_INSTALL_PATH "@rpath"
    PUBLIC_HEADER "${GLEW_PUBLIC_HEADERS_FILES}"
    OUTPUT_NAME GLEW
  )
endif()

if(GLEW_BUILD_SHARED_LIBS)
  target_link_libraries(glew LINK_PUBLIC ${GLEW_LIBRARIES})
else()
  target_link_libraries(glew_s ${GLEW_LIBRARIES})
endif()

if(CMAKE_VERSION VERSION_LESS 2.8.12)
  set(MAYBE_EXPORT "")
else()
  if(GLEW_BUILD_SHARED_LIBS)
    target_include_directories(glew PUBLIC $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  else()
    target_compile_definitions(glew_s INTERFACE "GLEW_STATIC")
    target_include_directories(glew_s PUBLIC $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  endif()

  set(MAYBE_EXPORT EXPORT glew-targets)
endif()

set(targets_to_install "")

if(GLEW_BUILD_SHARED_LIBS)
  list(APPEND targets_to_install glew)
else()
  list(APPEND targets_to_install glew_s)
endif()

if(GLEW_INSTALL)
  install(TARGETS ${targets_to_install}
    ${MAYBE_EXPORT}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FRAMEWORK DESTINATION ${CMAKE_INSTALL_PREFIX}
  )
endif()

if(GLEW_BUILD_UTILS)
  set(GLEWINFO_SRC_FILES ${GLEW_DIR}/src/glewinfo.c)

  if(WIN32)
    list(APPEND GLEWINFO_SRC_FILES ${GLEW_DIR}/src/win32/glewinfo.rc)
  endif()

  add_executable(glewinfo ${GLEWINFO_SRC_FILES})

  if(GLEW_BUILD_SHARED_LIBS)
    target_link_libraries(glewinfo glew)
  else()
    target_link_libraries(glewinfo glew_s)
  endif()

  if(NOT WIN32)
    target_link_libraries(glewinfo ${X11_LIBRARIES})
  endif()

  set(VISUALINFO_SRC_FILES ${GLEW_DIR}/src/visualinfo.c)

  if(WIN32)
    list(APPEND VISUALINFO_SRC_FILES ${GLEW_DIR}/src/win32/visualinfo.rc)
  endif()

  add_executable(visualinfo ${VISUALINFO_SRC_FILES})

  if(GLEW_BUILD_SHARED_LIBS)
    target_link_libraries(visualinfo glew)
  else()
    target_link_libraries(visualinfo glew_s)
  endif()

  if(NOT WIN32)
    target_link_libraries(visualinfo ${X11_LIBRARIES})
  endif()

  if(GLEW_INSTALL)
    install(TARGETS glewinfo visualinfo
      DESTINATION ${CMAKE_INSTALL_BINDIR})
  endif()
endif()

set(prefix ${CMAKE_INSTALL_PREFIX})
set(exec_prefix ${CMAKE_INSTALL_PREFIX})
set(libdir ${CMAKE_INSTALL_FULL_LIBDIR})
set(includedir ${CMAKE_INSTALL_FULL_INCLUDEDIR})
set(version ${GLEW_VERSION})
set(libname ${GLEW_LIB_NAME})
set(cflags)
set(requireslib glu)

# Mac OSX has no glu.pc unless optional X11/GLX is installed
if(APPLE)
  set(requireslib)
endif()

configure_file(${GLEW_DIR}/glew.pc.in ${CMAKE_CURRENT_BINARY_DIR}/glew.pc @ONLY)

if(GLEW_INSTALL)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/glew.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
  )
endif()

if(WIN32 AND MSVC AND(NOT MSVC_VERSION LESS 1600) AND(NOT CMAKE_VERSION VERSION_LESS "3.1") AND GLEW_INSTALL)
  install(
    FILES $<TARGET_PDB_FILE:glew>
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    CONFIGURATIONS Debug RelWithDebInfo
  )
endif()

if(GLEW_INSTALL)
  install(
    FILES ${GLEW_PUBLIC_HEADERS_FILES}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/GL)
endif()

if(MAYBE_EXPORT AND GLEW_INSTALL)
  install(EXPORT glew-targets DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/glew
    NAMESPACE GLEW::)
  install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/glew-config.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyImportedTargetProperties.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/glew)
endif()

if(NOT TARGET uninstall)
  configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
    IMMEDIATE @ONLY)

  add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P
    ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
endif()
