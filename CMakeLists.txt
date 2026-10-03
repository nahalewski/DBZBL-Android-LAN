# Dragon Ball Z: Burst Limit Recompiled
#
# SDK boilerplate lives in generated/rexglue.cmake. The ReXGlue SDK is taken
# from thirdparty/rexglue-sdk (git submodule) unless REXSDK_DIR is set.

cmake_minimum_required(VERSION 3.25)
project(burstlimit LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT REXSDK_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rexglue-sdk/CMakeLists.txt")
    set(REXSDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rexglue-sdk" CACHE PATH
        "Path to rexglue-sdk source tree")
endif()

include(generated/rexglue.cmake)

set(BURSTLIMIT_SOURCES
    src/main.cpp
    src/burstlimit_camera.cpp
    src/burstlimit_cpk.cpp
    src/burstlimit_forms.cpp
    src/burstlimit_patches.cpp
)

# Optional Discord Rich Presence. Download the Discord Social SDK and point
# DISCORD_SDK_ROOT at it (the folder that contains include/, lib/ and bin/).
set(DISCORD_SDK_ROOT "" CACHE PATH "Discord Social SDK root (optional)")
set(BURSTLIMIT_DISCORD OFF)
if(DISCORD_SDK_ROOT AND EXISTS "${DISCORD_SDK_ROOT}/include/discordpp.h")
    set(BURSTLIMIT_DISCORD ON)
    list(APPEND BURSTLIMIT_SOURCES src/discord_presence.cpp)
endif()

if(WIN32)
    # Exe / window / taskbar icon.
    enable_language(RC)
    list(APPEND BURSTLIMIT_SOURCES res/burstlimit.rc)
    add_executable(burstlimit WIN32 ${BURSTLIMIT_SOURCES})
else()
    add_executable(burstlimit ${BURSTLIMIT_SOURCES})
endif()

rexglue_setup_target(burstlimit GPU_PLUGINS xenos)

# ReXGlue UI sources use ImGui directly.
target_link_libraries(burstlimit PRIVATE imgui::imgui)
target_include_directories(burstlimit PRIVATE "${CMAKE_BINARY_DIR}/rexglue-sdk/include")

if(BURSTLIMIT_DISCORD)
    message(STATUS "Discord Rich Presence: ON (${DISCORD_SDK_ROOT})")
    target_include_directories(burstlimit PRIVATE "${DISCORD_SDK_ROOT}/include")
    target_link_libraries(burstlimit PRIVATE "${DISCORD_SDK_ROOT}/lib/release/discord_partner_sdk.lib")
    add_custom_command(
        TARGET burstlimit
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${DISCORD_SDK_ROOT}/bin/release/discord_partner_sdk.dll"
            "$<TARGET_FILE_DIR:burstlimit>/discord_partner_sdk.dll"
    )
else()
    message(STATUS "Discord Rich Presence: OFF (set DISCORD_SDK_ROOT to enable)")
endif()
