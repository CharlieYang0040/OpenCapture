cmake_minimum_required(VERSION 3.21)

project(OpenCapture VERSION 0.2.6 LANGUAGES CXX)

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

add_library(opencapture_i18n STATIC
    ui/i18n.cpp
)
target_include_directories(opencapture_i18n PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(opencapture_i18n PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(opencapture_i18n PRIVATE /W4 /permissive- /Zc:__cplusplus)
endif()

add_library(opencapture_core STATIC
    core/capture_target.cpp
    core/hotkey.cpp
    core/recording_options.cpp
    core/screenshot_options.cpp
    core/session_state.cpp
    core/ui_scale.cpp
)
target_include_directories(opencapture_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(opencapture_core PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(opencapture_core PRIVATE /W4 /permissive- /Zc:__cplusplus)
endif()

add_library(opencapture_audio STATIC
    audio/audio_timeline_mixer.cpp
    audio/wasapi_capture.cpp
)
target_include_directories(opencapture_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(opencapture_audio PUBLIC opencapture_core ole32)
target_compile_definitions(opencapture_audio PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(opencapture_audio PRIVATE /W4 /permissive- /Zc:__cplusplus)
endif()

if(OPENCAPTURE_BUILD_APP)
    find_package(imgui CONFIG REQUIRED)

    find_path(FFMPEG_INCLUDE_DIR libavutil/avutil.h REQUIRED)
    find_library(AVUTIL_LIBRARY avutil REQUIRED)
    find_library(AVCODEC_LIBRARY avcodec REQUIRED)
    find_library(AVFILTER_LIBRARY avfilter REQUIRED)
    find_library(AVFORMAT_LIBRARY avformat REQUIRED)

    configure_file(
        app/OpenCapture.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/OpenCapture.rc
        @ONLY
    )

    add_executable(OpenCapture WIN32
        ${CMAKE_CURRENT_BINARY_DIR}/OpenCapture.rc
        app/resource.h
        assets/opencapture.ico
        app/main.cpp
        encoder/ffmpeg_audio_encoder.cpp
        encoder/ffmpeg_d3d11_encoder.cpp
        encoder/ffmpeg_encoder_registry.cpp
        encoder/ffmpeg_muxer.cpp
        encoder/video_encoder_worker.cpp
        encoder/ffmpeg_gif_converter.cpp
        gpu/d3d11_frame_processor.cpp
        image/screenshot_service.cpp
        platform/capture_target_overlay.cpp
        platform/capture_target_picker.cpp
        platform/global_hotkeys.cpp
        platform/system_tray.cpp
        platform/windows_graphics_capture.cpp
        platform/win32_d3d11_app.cpp
        ui/fonts.cpp
        ui/main_panel.cpp
        ui/theme.cpp
        ui/tooltips.cpp
    )
    target_include_directories(OpenCapture PRIVATE ${FFMPEG_INCLUDE_DIR})
    target_link_libraries(OpenCapture PRIVATE
        opencapture_core
        opencapture_audio
        opencapture_i18n
        imgui::imgui
        ${AVUTIL_LIBRARY}
        ${AVCODEC_LIBRARY}
        ${AVFILTER_LIBRARY}
        ${AVFORMAT_LIBRARY}
        d3d11
        d3dcompiler
        dxgi
        dwmapi
        shell32
        windowscodecs
        windowsapp
    )
    target_compile_definitions(OpenCapture PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(OpenCapture PRIVATE /W4 /permissive- /Zc:__cplusplus)
    endif()

    install(TARGETS OpenCapture RUNTIME DESTINATION .)

    if(OPENCAPTURE_BUILD_TESTS)
        add_executable(opencapture_encoder_probe
            tools/encoder_probe.cpp
            encoder/ffmpeg_encoder_registry.cpp
        )
        target_include_directories(opencapture_encoder_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_include_directories(opencapture_encoder_probe SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIR})
        target_link_libraries(opencapture_encoder_probe PRIVATE ${AVCODEC_LIBRARY} ${AVUTIL_LIBRARY} dxgi)
        target_compile_definitions(opencapture_encoder_probe PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
        if(MSVC)
            target_compile_options(opencapture_encoder_probe PRIVATE /W4 /permissive- /Zc:__cplusplus)
        endif()

        add_executable(opencapture_wasapi_probe
            tools/wasapi_probe.cpp
        )
        target_link_libraries(opencapture_wasapi_probe PRIVATE opencapture_audio)
        target_compile_definitions(opencapture_wasapi_probe PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
        if(MSVC)
            target_compile_options(opencapture_wasapi_probe PRIVATE /W4 /permissive- /Zc:__cplusplus)
        endif()

        add_executable(opencapture_ffmpeg_build_info
            tools/ffmpeg_build_info.cpp
        )
        target_include_directories(opencapture_ffmpeg_build_info SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIR})
        target_link_libraries(opencapture_ffmpeg_build_info PRIVATE ${AVUTIL_LIBRARY})
        target_compile_definitions(opencapture_ffmpeg_build_info PRIVATE __STDC_CONSTANT_MACROS)
        if(MSVC)
            target_compile_options(opencapture_ffmpeg_build_info PRIVATE /W4 /permissive- /Zc:__cplusplus)
        endif()
    endif()
endif()

if(OPENCAPTURE_BUILD_TESTS)
    enable_testing()
    add_executable(opencapture_core_tests tests/core_tests.cpp)
    target_link_libraries(opencapture_core_tests PRIVATE opencapture_core opencapture_i18n)
    if(MSVC)
        target_compile_options(opencapture_core_tests PRIVATE /W4 /permissive- /Zc:__cplusplus)
    endif()
    add_test(NAME opencapture_core_tests COMMAND opencapture_core_tests)

    add_executable(opencapture_audio_tests tests/audio_tests.cpp)
    target_link_libraries(opencapture_audio_tests PRIVATE opencapture_audio)
    if(MSVC)
        target_compile_options(opencapture_audio_tests PRIVATE /W4 /permissive- /Zc:__cplusplus)
    endif()
    add_test(NAME opencapture_audio_tests COMMAND opencapture_audio_tests)
endif()
