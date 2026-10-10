# Android consumes host-generated C++; it must never try to run a cross-built
# rexglue executable during an APK build.
add_subdirectory("${REXSDK_DIR}" rexglue-sdk)
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/sources.cmake")
    message(FATAL_ERROR "Host code generation is required before building the Android game")
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/generated/default/sources.cmake")
function(rexglue_setup_target target_name)
    add_library(${target_name}_recomp OBJECT ${GENERATED_SOURCES})
    target_include_directories(${target_name}_recomp PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/generated/default")
    target_link_libraries(${target_name}_recomp PRIVATE rex::runtime)
    target_compile_options(${target_name}_recomp PRIVATE -fno-strict-aliasing -fno-char8_t -g0)
    target_precompile_headers(${target_name}_recomp PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/burstlimit_pch.h")
    target_include_directories(${target_name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/generated/default")
    target_link_libraries(${target_name} PRIVATE ${target_name}_recomp rex::runtime)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/metadata/icons")
        rexglue_embed_metadata(${target_name}
            DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/metadata/icons" PREFIX "icons")
    endif()
    rexglue_configure_target(${target_name} ${ARGN})
endfunction()
