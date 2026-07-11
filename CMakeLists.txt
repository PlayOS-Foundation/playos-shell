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

playos_add_dependency(playos-platform-api)
playos_add_dependency(playos-runtime)

add_executable(playos-shell src/main.cpp)
target_link_libraries(playos-shell PRIVATE
    PlayOS::PlatformAPI
    PlayOS::playos-input-raylib
    PlayOS::playos-display-raylib
    PlayOS::playos-audio-raylib
    PlayOS::playos-touch-raylib
    PlayOS::Runtime
    raylib)
if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(playos-shell PRIVATE PlayOS::playos-storage-windows)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(playos-shell PRIVATE PlayOS::playos-storage-linux)
else()
    target_link_libraries(playos-shell PRIVATE PlayOS::playos-storage-null)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(playos-shell PRIVATE PlayOS::playos-battery-linux)
else()
    target_link_libraries(playos-shell PRIVATE PlayOS::playos-battery-null)
endif()
