cmake_minimum_required(VERSION 3.23)
project(TouchPlusPhase2BTrackerTest LANGUAGES CXX)

add_executable(touchplus_fingertip_tracker_selftest
    ../src/fingertip_tracker_selftest.cpp
)
target_compile_features(touchplus_fingertip_tracker_selftest PRIVATE cxx_std_20)
target_compile_definitions(touchplus_fingertip_tracker_selftest PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)
target_include_directories(touchplus_fingertip_tracker_selftest PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

if(MSVC)
    set_property(TARGET touchplus_fingertip_tracker_selftest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    target_compile_options(touchplus_fingertip_tracker_selftest PRIVATE /W4 /permissive- /EHsc)
endif()
