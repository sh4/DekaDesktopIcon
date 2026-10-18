# SPDX-License-Identifier: MIT
# Copyright (c) 2026 sh4
cmake_minimum_required(VERSION 3.21)
project(DekaDesktopIcon VERSION 1.4.4 LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "DekaDesktopIcon requires the Windows x64 MSVC toolchain.")
endif()
if(CMAKE_GENERATOR_PLATFORM MATCHES "ARM" OR CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM|aarch)")
    message(FATAL_ERROR "ARM targets are not supported; choose x64.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(DekaDesktopIconCore SHARED src/DekaDesktopIconCore.cpp)
add_executable(DekaDesktopIcon WIN32 src/DekaDesktopIcon.cpp src/CommandClient.cpp)
add_executable(DekaDesktopIconTestHost WIN32 tests/DekaDesktopIconTestHost.cpp)
add_executable(DekaDesktopIconSortTest WIN32 tests/DekaDesktopIconSortTest.cpp)

set(deka_targets
    DekaDesktopIconCore
    DekaDesktopIcon
    DekaDesktopIconTestHost
    DekaDesktopIconSortTest
)
foreach(target IN LISTS deka_targets)
    target_include_directories(${target} PRIVATE src tests)
    target_compile_definitions(${target} PRIVATE UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_link_options(${target} PRIVATE /MANIFEST:EMBED /DYNAMICBASE /NXCOMPAT)
    target_link_libraries(${target} PRIVATE
        user32 gdi32 comctl32 ole32 oleaut32 shell32 uuid gdiplus windowscodecs comdlg32 uxtheme
    )
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    foreach(configuration Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${configuration}" configuration_upper)
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${configuration_upper} "${CMAKE_BINARY_DIR}/bin"
        )
    endforeach()
endforeach()

add_custom_target(check-source-style
    COMMAND powershell -NoProfile
        -File "${CMAKE_SOURCE_DIR}/scripts/Test-SourceStyle.ps1"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Checking C++ formatting and explicit control-statement braces"
)
