cmake_minimum_required(VERSION 3.16)
project(skope C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

option(SKOPE_DOWNLOAD_RAYLIB
  "Fetch raylib from GitHub if not found on the system" ON)

# ---------------------------------------------------------------------------
# Compiler flags
# ---------------------------------------------------------------------------

if(MSVC)
  add_compile_options(/W3)
else()
  add_compile_options(
    -Wall -Wextra -Wno-unused-parameter
    -fno-strict-aliasing
  )
endif()

# ---------------------------------------------------------------------------
# Find or fetch raylib
# ---------------------------------------------------------------------------

find_package(raylib 5.5 QUIET)

if(NOT raylib_FOUND)
  if(SKOPE_DOWNLOAD_RAYLIB)
    include(FetchContent)
    FetchContent_Declare(
      raylib
      GIT_REPOSITORY https://github.com/raysan5/raylib.git
      GIT_TAG        5.5
      GIT_SHALLOW    TRUE
    )
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
    # CMake 4.x removed compatibility with cmake_minimum_required < 3.5.
    # raylib 5.5 is clean, but guard against any transitive dep that isn't.
    if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
      set(CMAKE_POLICY_VERSION_MINIMUM "3.5" CACHE STRING "" FORCE)
    endif()
    FetchContent_MakeAvailable(raylib)
    message(STATUS "skope: raylib fetched via FetchContent")
  else()
    message(FATAL_ERROR
      "raylib not found. Install it, or set SKOPE_DOWNLOAD_RAYLIB=ON.")
  endif()
else()
  message(STATUS "skope: using system raylib ${raylib_VERSION}")
endif()

# ---------------------------------------------------------------------------
# Source files
#   scope-ipc.c is compiled as part of this project so skope links the
#   same reader implementation it was designed against. The producer-side
#   functions in scope-ipc.c are included too but are never called at
#   runtime from skope — they just need to compile cleanly.
# ---------------------------------------------------------------------------

set(SKOPE_SOURCES
  src/skope.c
  src/scope-ipc.c
)

add_executable(skope ${SKOPE_SOURCES})

# ---------------------------------------------------------------------------
# Include paths
#   compat/   provides synth-types.h and portable_atomic.h
#   src/      provides scope-ipc.h (already there after the copy step)
# ---------------------------------------------------------------------------

target_include_directories(skope PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/compat
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# scope-ipc.c uses ftruncate, shm_open, nanosleep etc — needs POSIX 2008
target_compile_definitions(skope PRIVATE _POSIX_C_SOURCE=200809L)

# ---------------------------------------------------------------------------
# Link raylib and platform deps
# ---------------------------------------------------------------------------

target_link_libraries(skope PRIVATE raylib)

if(UNIX AND NOT APPLE)
  # POSIX shm, pthreads, math
  target_link_libraries(skope PRIVATE rt pthread m)

  # Wayland or X11 — raylib itself handles this, but some distros need
  # an explicit -ldl for dlopen used inside raylib's backend selection.
  target_link_libraries(skope PRIVATE dl)

  # If building from FetchContent raylib on Linux without a display
  # server (CI), raylib may pull in OpenGL — handled by FetchContent
  # CMake already, but make GL explicit just in case.
  find_package(OpenGL QUIET)
  if(OpenGL_FOUND)
    target_link_libraries(skope PRIVATE OpenGL::GL)
  endif()

elseif(APPLE)
  target_link_libraries(skope PRIVATE m)
  # macOS frameworks (raylib's CMake exports these, but belt-and-braces)
  find_library(COCOA_FRAMEWORK   Cocoa   REQUIRED)
  find_library(IOKIT_FRAMEWORK   IOKit   REQUIRED)
  find_library(OPENGL_FRAMEWORK  OpenGL  REQUIRED)
  find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
  find_library(COREGRAPHICS_FRAMEWORK   CoreGraphics   REQUIRED)
  target_link_libraries(skope PRIVATE
    ${COCOA_FRAMEWORK}
    ${IOKIT_FRAMEWORK}
    ${OPENGL_FRAMEWORK}
    ${COREFOUNDATION_FRAMEWORK}
    ${COREGRAPHICS_FRAMEWORK}
  )
elseif(WIN32)
  target_link_libraries(skope PRIVATE winmm)
endif()

# ---------------------------------------------------------------------------
# HiDPI / Retina: tell Windows to be DPI-aware via a manifest or pragma
# ---------------------------------------------------------------------------

if(WIN32)
  if(MSVC)
    # Embedded manifest requesting Per-Monitor DPI awareness v2
    target_sources(skope PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/dpi.manifest)
    set_target_properties(skope PROPERTIES
      LINK_FLAGS "/MANIFEST:EMBED")
  else()
    # MinGW: link the shim resource instead
    configure_file(
      ${CMAKE_CURRENT_SOURCE_DIR}/cmake/dpi.rc.in
      ${CMAKE_BINARY_DIR}/dpi.rc
      COPYONLY)
    target_sources(skope PRIVATE ${CMAKE_BINARY_DIR}/dpi.rc)
  endif()
endif()

# ---------------------------------------------------------------------------
# macOS: set High Resolution capable in Info.plist (for bundled apps)
# ---------------------------------------------------------------------------

if(APPLE)
  set_target_properties(skope PROPERTIES
    MACOSX_BUNDLE FALSE
  )
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------

install(TARGETS skope RUNTIME DESTINATION bin)

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

message(STATUS "skope build:")
message(STATUS "  C standard  : ${CMAKE_C_STANDARD}")
message(STATUS "  Build type  : ${CMAKE_BUILD_TYPE}")
message(STATUS "  RECORD_CHANNELS: 10 (from synth-types.h)")
