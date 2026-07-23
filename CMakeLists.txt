cmake_minimum_required(VERSION 3.20)
project(playos_shell VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Resolve a PlayOS dependency from a sibling checkout if present (multi-repo
# local dev), otherwise fetch it from GitHub.
function(playos_add_dependency name)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../${name}/CMakeLists.txt")
        message(STATUS "PlayOS: using sibling checkout for ${name}")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../${name}"
                         "${CMAKE_BINARY_DIR}/_playos/${name}")
    else()
        message(STATUS "PlayOS: fetching ${name} from GitHub")
        FetchContent_Declare(${name}
            GIT_REPOSITORY "https://github.com/PlayOS-Foundation/${name}.git"
            GIT_TAG main)
        FetchContent_MakeAvailable(${name})
    endif()
endfunction()

# Raylib: use system package on platforms like Alpine where it's provided,
# otherwise fetch raylib 6.0 via FetchContent (default for Windows/macOS).
option(PLAYOS_USE_SYSTEM_RAYLIB "Use system-installed raylib (pkg-config)" OFF)
if(PLAYOS_USE_SYSTEM_RAYLIB)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(RAYLIB REQUIRED raylib)
    message(STATUS "PlayOS shell: using system raylib ${RAYLIB_VERSION}")
    # Create an imported target for compatibility with the rest of the build.
    add_library(raylib SHARED IMPORTED)
    set_target_properties(raylib PROPERTIES
        IMPORTED_LOCATION "${RAYLIB_LIBRARY_DIRS}/libraylib.so"
        INTERFACE_INCLUDE_DIRECTORIES "${RAYLIB_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES "${RAYLIB_LIBRARIES}")
    # Platform API backends expect GLFW already linked through raylib.
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_X11 OFF CACHE BOOL "" FORCE)
else()
    # Raylib must be fetched BEFORE playos-platform-api so the platform-api's
    # CMakeLists.txt can see the raylib target and build its raylib backends.
    FetchContent_Declare(raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 6.0)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)

    # On Linux the shell runs as a Wayland client of the PlayOS compositor. Enable
    # PLAYOS_SHELL_WAYLAND to build Raylib's GLFW with the Wayland backend (only)
    # instead of X11. Requires wayland-protocols and libxkbcommon-dev. Off by
    # default so the standard desktop (X11) build is unaffected.
    option(PLAYOS_SHELL_WAYLAND "Build the shell for Wayland (Linux)" OFF)
    if(PLAYOS_SHELL_WAYLAND AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
        set(GLFW_BUILD_X11 OFF CACHE BOOL "" FORCE)
    endif()

    FetchContent_MakeAvailable(raylib)
endif()

playos_add_dependency(playos-platform-api)
playos_add_dependency(playos-runtime)

add_executable(playos-shell
    src/main.cpp
    src/shell_app.cpp
    src/core/screen_stack.cpp
    src/ui/theme.cpp
    src/ui/icons.cpp
    src/ui/status_bar.cpp
    src/screens/library_screen.cpp
    src/screens/overlay_screen.cpp
    src/screens/wifi_screen.cpp
    src/screens/installer_screen.cpp  # dd-based disk image installer
)
target_link_libraries(playos-shell PRIVATE
    PlayOS::PlatformAPI
    PlayOS::playos-input-raylib
    PlayOS::playos-audio-raylib
    PlayOS::playos-touch-raylib
    PlayOS::Runtime
    raylib)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(playos-shell PRIVATE PlayOS::playos-display-linux)
else()
    target_link_libraries(playos-shell PRIVATE PlayOS::playos-display-raylib)
endif()
if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(playos-shell PRIVATE PlayOS::playos-storage-windows)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(playos-shell PRIVATE PlayOS::playos-storage-linux)
else()
    target_link_libraries(playos-shell PRIVATE PlayOS::playos-storage-null)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(playos-shell PRIVATE PlayOS::playos-battery-linux)
    target_link_libraries(playos-shell PRIVATE PlayOS::playos-network-linux)
    target_link_libraries(playos-shell PRIVATE PlayOS::playos-bluetooth-linux)
elseif(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(playos-shell PRIVATE PlayOS::playos-battery-windows)
    target_link_libraries(playos-shell PRIVATE PlayOS::playos-network-windows)
    target_link_libraries(playos-shell PRIVATE PlayOS::playos-bluetooth-windows)
else()
    target_link_libraries(playos-shell PRIVATE PlayOS::playos-battery-null)
    target_link_libraries(playos-shell PRIVATE PlayOS::playos-network-null)
    target_link_libraries(playos-shell PRIVATE PlayOS::playos-bluetooth-null)
endif()

# Installer is now integrated into playos-shell (src/screens/installer_screen.cpp).
# The standalone playos-installer-gui binary that spawned the legacy shell-script
# installer has been retired.
