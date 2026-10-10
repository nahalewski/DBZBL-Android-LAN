# Dragon Ball Z: Burst Limit Recompiled
#
# SDK boilerplate lives in generated/rexglue.cmake. The ReXGlue SDK is taken
# from thirdparty/rexglue-sdk (git submodule) unless REXSDK_DIR is set.

cmake_minimum_required(VERSION 3.25)
project(burstlimit LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    # Enable both languages before configuring SDL; otherwise its .m files
    # may be classified as Objective-C++ after a later language enable.
    enable_language(OBJC OBJCXX)
endif()

if(NOT REXSDK_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rexglue-sdk/CMakeLists.txt")
    set(REXSDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rexglue-sdk" CACHE PATH
        "Path to rexglue-sdk source tree")
endif()

if(ANDROID)
    include(android/rexglue.cmake)
else()
    include(generated/rexglue.cmake)
endif()

set(BURSTLIMIT_SOURCES
    src/main.cpp
    src/burstlimit_branding.cpp
    src/burstlimit_buttons.cpp
    src/burstlimit_camera.cpp
    src/burstlimit_cpk.cpp
    src/burstlimit_forms.cpp
    src/burstlimit_kicharge.cpp
    src/burstlimit_mods.cpp
    src/burstlimit_netinput.cpp
    src/burstlimit_online.cpp
    src/burstlimit_patches.cpp
    src/burstlimit_saves.cpp
    src/burstlimit_stages.cpp
)

if(ANDROID)
    list(APPEND BURSTLIMIT_SOURCES src/burstlimit_install_android.cpp
        src/burstlimit_android_input.cpp)
elseif(APPLE)
    list(APPEND BURSTLIMIT_SOURCES src/burstlimit_install_macos.mm
        src/burstlimit_game_import.cpp)
    set_source_files_properties(src/burstlimit_install_macos.mm PROPERTIES
        COMPILE_OPTIONS "-fobjc-arc")
else()
    list(APPEND BURSTLIMIT_SOURCES src/burstlimit_install.cpp)
endif()

# Optional Discord Rich Presence. Download the Discord Social SDK and point
# DISCORD_SDK_ROOT at it (the folder that contains include/, lib/ and bin/).
set(DISCORD_SDK_ROOT "" CACHE PATH "Discord Social SDK root (optional)")
set(BURSTLIMIT_DISCORD OFF)
if(DISCORD_SDK_ROOT AND EXISTS "${DISCORD_SDK_ROOT}/include/discordpp.h")
    set(BURSTLIMIT_DISCORD ON)
    list(APPEND BURSTLIMIT_SOURCES src/discord_presence.cpp)
endif()

if(ANDROID)
    add_library(burstlimit SHARED ${BURSTLIMIT_SOURCES})
    set_target_properties(burstlimit PROPERTIES OUTPUT_NAME main)
    target_link_libraries(burstlimit PRIVATE android log libavutil)
elseif(APPLE)
    set(BURSTLIMIT_MAC_ICON "${CMAKE_CURRENT_SOURCE_DIR}/macos/BurstLimit.icns")
    set_source_files_properties("${BURSTLIMIT_MAC_ICON}" PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    add_executable(burstlimit MACOSX_BUNDLE ${BURSTLIMIT_SOURCES} "${BURSTLIMIT_MAC_ICON}")
    set_target_properties(burstlimit PROPERTIES
        MACOSX_BUNDLE_ICON_FILE BurstLimit.icns
        MACOSX_BUNDLE_GUI_IDENTIFIER org.burstlimit.macos
        MACOSX_BUNDLE_BUNDLE_NAME "Burst Limit"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.4.1"
        MACOSX_BUNDLE_BUNDLE_VERSION "0.4.1")
    target_link_libraries(burstlimit PRIVATE libavutil archive
        "-framework AppKit" "-framework UniformTypeIdentifiers")
    # macOS ships libarchive, but Xcode doesn't ship its public headers.
    # Only the headers are needed from Homebrew; the app links the OS library.
    find_path(BURSTLIMIT_ARCHIVE_INCLUDE_DIR archive.h
        PATHS /opt/homebrew/opt/libarchive/include /usr/local/opt/libarchive/include REQUIRED)
    target_include_directories(burstlimit PRIVATE "${BURSTLIMIT_ARCHIVE_INCLUDE_DIR}")
elseif(WIN32)
    # Exe / window / taskbar icon.
    enable_language(RC)
    list(APPEND BURSTLIMIT_SOURCES res/burstlimit.rc)
    add_executable(burstlimit WIN32 ${BURSTLIMIT_SOURCES})
else()
    add_executable(burstlimit ${BURSTLIMIT_SOURCES})
endif()

rexglue_setup_target(burstlimit GPU_PLUGINS xenos)

# Online version string: the lobby only lists rooms between identical strings.
# Release tag + git hash; a build id when the tree isn't a git checkout (point
# BURSTLIMIT_GIT_DIR at one to use its tag and hash).
set(BURSTLIMIT_VERSION_OVERRIDE "" CACHE STRING "Online version string (empty = git tag + hash)")
set(BURSTLIMIT_GIT_DIR "" CACHE PATH "Git checkout that names this build (default: the source tree)")
set(_bl_version "${BURSTLIMIT_VERSION_OVERRIDE}")
if(NOT _bl_version)
    set(_bl_git_dir "${CMAKE_CURRENT_SOURCE_DIR}")
    if(BURSTLIMIT_GIT_DIR)
        set(_bl_git_dir "${BURSTLIMIT_GIT_DIR}")
    endif()
    find_package(Git QUIET)
    set(_bl_hash "")
    set(_bl_tag "")
    if(GIT_FOUND)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${_bl_git_dir}" rev-parse --short HEAD
            OUTPUT_VARIABLE _bl_hash OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
            RESULT_VARIABLE _bl_hash_result)
        if(NOT _bl_hash_result EQUAL 0)
            set(_bl_hash "")
        endif()
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${_bl_git_dir}" describe --tags
            --match "v[0-9]*" --abbrev=0
            OUTPUT_VARIABLE _bl_tag OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
            RESULT_VARIABLE _bl_tag_result)
        if(NOT _bl_tag_result EQUAL 0)
            set(_bl_tag "")
        endif()
    endif()
    if(_bl_hash AND _bl_tag)
        set(_bl_version "${_bl_tag}+${_bl_hash}")
    elseif(_bl_hash)
        set(_bl_version "g${_bl_hash}")
    else()
        string(TIMESTAMP _bl_stamp "%Y%m%d%H%M" UTC)
        set(_bl_version "build${_bl_stamp}")
    endif()
endif()
message(STATUS "Online version: burstlimit-${_bl_version}")
target_compile_definitions(burstlimit PRIVATE "BURSTLIMIT_VERSION=\"${_bl_version}\"")

# ReXGlue UI sources use ImGui directly.
if(ANDROID OR APPLE)
    # rexruntime owns the ImGui context. Linking the OBJECT library here would
    # give libmain a separate context, crashing game overlays and dialogs.
    target_include_directories(burstlimit PRIVATE "${REXSDK_DIR}/thirdparty/imgui")
else()
    target_link_libraries(burstlimit PRIVATE imgui::imgui)
endif()
# stb_image's zlib decoder (save import reads compressed zips).
target_include_directories(burstlimit PRIVATE "${REXSDK_DIR}/thirdparty/stb")
if(WIN32)
    target_link_libraries(burstlimit PRIVATE shell32)
endif()
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

option(BURSTLIMIT_BUILD_LAN_TESTS "Build the direct LAN integration probe" OFF)
if(BURSTLIMIT_BUILD_LAN_TESTS AND NOT WIN32 AND NOT ANDROID)
    add_executable(burstlimit_lan_probe tests/lan_probe.cpp)
    target_link_libraries(burstlimit_lan_probe PRIVATE rex::runtime)
    target_include_directories(burstlimit_lan_probe PRIVATE "${CMAKE_BINARY_DIR}/rexglue-sdk/include")
endif()
