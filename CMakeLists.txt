cmake_minimum_required(VERSION 3.18)
project(playos-shell
    VERSION 0.1.0
    DESCRIPTION "PlayOS controller-first shell (Wayland + EGL/GLES2)"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ── Find dependencies ──────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)

pkg_check_modules(WAYLAND_CLIENT wayland-client REQUIRED)
pkg_check_modules(WAYLAND_EGL    wayland-egl REQUIRED)
pkg_check_modules(EGL            egl REQUIRED)
pkg_check_modules(GLES2          glesv2 REQUIRED)

# libplayos (from playos-platform-api)
find_library(PLAYOS_LIB playos HINTS ${CMAKE_SYSROOT}/usr/lib)
if(NOT PLAYOS_LIB)
    message(FATAL_ERROR "libplayos not found — build playos-platform-api first")
endif()

# Find playos headers (should be alongside the library)
get_filename_component(PLAYOS_LIB_DIR "${PLAYOS_LIB}" DIRECTORY)
find_path(PLAYOS_INCLUDE_DIR playos/playos.h
    HINTS "${PLAYOS_LIB_DIR}/../include"
          "${CMAKE_PREFIX_PATH}/include"
    REQUIRED)

# ── Trusted IPC (playos-runtime) ───────────────────────────────────────
# libplayos-trusted wraps /run/playos/control.sock for game launch etc.
set(PLAYOS_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../playos-runtime"
    CACHE PATH "Path to playos-runtime source tree")
find_library(PLAYOS_TRUSTED_LIB playos-trusted
    HINTS ${CMAKE_SYSROOT}/usr/lib
          ${PLAYOS_RUNTIME_DIR}/build)
if(NOT PLAYOS_TRUSTED_LIB)
    message(STATUS "libplayos-trusted not found — trusted IPC disabled")
    set(PLAYOS_SHELL_HAS_TRUSTED_IPC OFF)
else()
    message(STATUS "Found libplayos-trusted: ${PLAYOS_TRUSTED_LIB}")
    set(PLAYOS_SHELL_HAS_TRUSTED_IPC ON)
endif()

# ── Wayland scanner for protocols ──────────────────────────────────────
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

set(PROTOCOL_XML "${CMAKE_CURRENT_SOURCE_DIR}/protocols/playos-v1.xml")
set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen")
file(MAKE_DIRECTORY ${GEN_DIR})

# Also need xdg-shell client protocol
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
set(XDG_SHELL_XML "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")

# Generate client headers and code from playos-v1.xml
add_custom_command(
    OUTPUT ${GEN_DIR}/playos-v1-client-protocol.h ${GEN_DIR}/playos-v1-protocol.c
    COMMAND ${WAYLAND_SCANNER} client-header ${PROTOCOL_XML} ${GEN_DIR}/playos-v1-client-protocol.h
    COMMAND ${WAYLAND_SCANNER} private-code  ${PROTOCOL_XML} ${GEN_DIR}/playos-v1-protocol.c
    DEPENDS ${PROTOCOL_XML}
    COMMENT "Generating PlayOS Wayland client protocol"
)

# Generate xdg-shell client protocol
add_custom_command(
    OUTPUT ${GEN_DIR}/xdg-shell-client-protocol.h ${GEN_DIR}/xdg-shell-protocol.c
    COMMAND ${WAYLAND_SCANNER} client-header ${XDG_SHELL_XML} ${GEN_DIR}/xdg-shell-client-protocol.h
    COMMAND ${WAYLAND_SCANNER} private-code  ${XDG_SHELL_XML} ${GEN_DIR}/xdg-shell-protocol.c
    DEPENDS ${XDG_SHELL_XML}
    COMMENT "Generating xdg-shell client protocol"
)

# ── Optional: Raylib static library ────────────────────────────────────
set(PLAYOS_SHELL_USE_RAYLIB OFF CACHE BOOL
    "Build with vendored Raylib (requires external/raylib/src/)")

if(PLAYOS_SHELL_USE_RAYLIB)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/raylib/src/rcore.c")
        add_subdirectory(external/raylib)
        set(RAYLIB_LIB raylib_playos)
        set(RAYLIB_DEFINE PLAYOS_SHELL_RAYLIB)
    else()
        message(WARNING "Raylib source not found at external/raylib/src/ — "
                        "falling back to raw EGL/GLES2 renderer")
        set(PLAYOS_SHELL_USE_RAYLIB OFF)
    endif()
endif()

# ── Shell source files ─────────────────────────────────────────────────
set(SHELL_SOURCES
    src/main.c
    src/input.c
    src/screen_home.c
    src/screen_library.c
    src/screen_game_detail.c
    src/screen_settings.c
    src/render_util.c
    ${GEN_DIR}/playos-v1-client-protocol.h
    ${GEN_DIR}/playos-v1-protocol.c
    ${GEN_DIR}/xdg-shell-client-protocol.h
    ${GEN_DIR}/xdg-shell-protocol.c
)

add_executable(playos-shell ${SHELL_SOURCES})

target_include_directories(playos-shell PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${GEN_DIR}
    ${PLAYOS_INCLUDE_DIR}
    ${WAYLAND_CLIENT_INCLUDE_DIRS}
    ${WAYLAND_EGL_INCLUDE_DIRS}
    ${EGL_INCLUDE_DIRS}
    ${GLES2_INCLUDE_DIRS}
)

# Add trusted IPC include path if available
if(PLAYOS_SHELL_HAS_TRUSTED_IPC)
    target_include_directories(playos-shell PRIVATE
        ${PLAYOS_RUNTIME_DIR}/include)
endif()

target_link_directories(playos-shell PRIVATE
    ${WAYLAND_CLIENT_LIBRARY_DIRS}
    ${WAYLAND_EGL_LIBRARY_DIRS}
    ${EGL_LIBRARY_DIRS}
    ${GLES2_LIBRARY_DIRS}
)

target_link_libraries(playos-shell PRIVATE
    ${WAYLAND_CLIENT_LIBRARIES}
    ${WAYLAND_EGL_LIBRARIES}
    ${EGL_LIBRARIES}
    ${GLES2_LIBRARIES}
    ${PLAYOS_LIB}
    m
)

if(PLAYOS_SHELL_USE_RAYLIB)
    target_link_libraries(playos-shell PRIVATE ${RAYLIB_LIB})
    target_compile_definitions(playos-shell PRIVATE ${RAYLIB_DEFINE})
endif()

# Conditionally enable trusted IPC
if(PLAYOS_SHELL_HAS_TRUSTED_IPC)
    target_link_libraries(playos-shell PRIVATE ${PLAYOS_TRUSTED_LIB})
    target_compile_definitions(playos-shell PRIVATE PLAYOS_TRUSTED_IPC)
endif()

target_compile_definitions(playos-shell PRIVATE
    _POSIX_C_SOURCE=199309L
    _DEFAULT_SOURCE
)

# ── Compiler flags ─────────────────────────────────────────────────────
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(playos-shell PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ── Installation ───────────────────────────────────────────────────────
include(GNUInstallDirs)
install(TARGETS playos-shell RUNTIME DESTINATION bin)
install(DIRECTORY assets/ DESTINATION share/playos-shell/assets
        FILES_MATCHING PATTERN "*.bmp")
