cmake_minimum_required(VERSION 3.25)

project(NovenTarkovSupport
    VERSION 0.1.0
    LANGUAGES CXX
)

if(NOT WIN32)
    message(FATAL_ERROR "Noven Tarkov Support currently targets Windows only.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(NovenTarkovSupport WIN32
    src/main.cpp
    src/app/App.cpp
    src/capture/DxgiDesktopDuplicationBackend.cpp
    src/capture/Roi.cpp
    src/common/DebugLog.cpp
    src/hotkey/GlobalHotkey.cpp
    src/scanner/DebugImageWriter.cpp
    src/scanner/ScanTrigger.cpp
)

target_include_directories(NovenTarkovSupport PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_definitions(NovenTarkovSupport PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)

if(MSVC)
    target_compile_options(NovenTarkovSupport PRIVATE
        /W4
        /permissive-
        /utf-8
        /EHsc
    )
else()
    target_compile_options(NovenTarkovSupport PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_options(NovenTarkovSupport PRIVATE
        -municode
    )
endif()

target_link_libraries(NovenTarkovSupport PRIVATE
    user32
    d3d11
    dxgi
)
