cmake_minimum_required(VERSION 3.23)
cmake_policy(SET CMP0091 NEW)
project(TouchPlusRevival LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Phase 0A currently targets Windows Media Foundation only.")
endif()

add_executable(touchplus_probe
    src/main.cpp
)

target_compile_features(touchplus_probe PRIVATE cxx_std_20)
target_compile_definitions(touchplus_probe PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(touchplus_probe PRIVATE
    mf
    mfplat
    mfreadwrite
    mfuuid
    ole32
    windowscodecs
)

if(MSVC)
    set_property(TARGET touchplus_probe PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    target_compile_options(touchplus_probe PRIVATE /W4 /permissive- /EHsc)
endif()
