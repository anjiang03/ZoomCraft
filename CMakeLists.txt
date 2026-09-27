cmake_minimum_required(VERSION 3.21)
project(ZoomCraft LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT MSVC)
    message(FATAL_ERROR "ZoomCraft targets MSVC (Visual Studio 2022) + Windows SDK 10.")
endif()

add_executable(ZoomCraft WIN32
    src/main.cpp
)

target_include_directories(ZoomCraft PRIVATE src)

target_compile_definitions(ZoomCraft PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)

# /O2 - performance first. Drop /RTC etc for release only.
target_compile_options(ZoomCraft PRIVATE
    /W4 /permissive- /EHsc /utf-8 /O2 /GS- /Gy /Gw
)

target_link_options(ZoomCraft PRIVATE /OPT:REF /OPT:ICF)

target_link_libraries(ZoomCraft PRIVATE
    d3d11 dxgi d2d1 dwrite d3dcompiler
    magnification dwmapi
    user32 gdi32 ole32 shcore winmm
)

# Static CRT -> single self-contained small exe.
set_property(TARGET ZoomCraft PROPERTY MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set_target_properties(ZoomCraft PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
