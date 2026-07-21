cmake_minimum_required(VERSION 3.21)

project(OpenCapture VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "OpenCapture is a Windows-only application")
endif()

if(MSVC AND MSVC_VERSION LESS 1930)
    message(FATAL_ERROR "OpenCapture requires Visual Studio 2022 with the MSVC v143 toolset")
endif()

option(OPENCAPTURE_BUILD_APP "Build the Win32 application" ON)
option(OPENCAPTURE_BUILD_TESTS "Build unit tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(opencapture_core STATIC
    core/capture_target.cpp
    core/session_state.cpp
)
target_include_directories(opencapture_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(opencapture_core PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(opencapture_core PRIVATE /W4 /permissive- /Zc:__cplusplus)
endif()

if(OPENCAPTURE_BUILD_APP)
    find_package(imgui CONFIG REQUIRED)

    find_path(FFMPEG_INCLUDE_DIR libavutil/avutil.h REQUIRED)
    find_library(AVUTIL_LIBRARY avutil REQUIRED)

    add_executable(OpenCapture WIN32
        app/main.cpp
        platform/win32_d3d11_app.cpp
        ui/main_panel.cpp
    )
    target_include_directories(OpenCapture PRIVATE ${FFMPEG_INCLUDE_DIR})
    target_link_libraries(OpenCapture PRIVATE
        opencapture_core
        imgui::imgui
        ${AVUTIL_LIBRARY}
        d3d11
        dxgi
        dwmapi
    )
    target_compile_definitions(OpenCapture PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(OpenCapture PRIVATE /W4 /permissive- /Zc:__cplusplus)
    endif()

    install(TARGETS OpenCapture RUNTIME DESTINATION .)
endif()

if(OPENCAPTURE_BUILD_TESTS)
    enable_testing()
    add_executable(opencapture_core_tests tests/core_tests.cpp)
    target_link_libraries(opencapture_core_tests PRIVATE opencapture_core)
    if(MSVC)
        target_compile_options(opencapture_core_tests PRIVATE /W4 /permissive- /Zc:__cplusplus)
    endif()
    add_test(NAME opencapture_core_tests COMMAND opencapture_core_tests)
endif()
