cmake_minimum_required(VERSION 3.23)
project(TouchPlusPhase2BTrackerTest LANGUAGES CXX)

add_executable(touchplus_fingertip_tracker_selftest
    ../src/fingertip_tracker_selftest.cpp
)
add_executable(touchplus_fingertip_fusion_selftest
    ../src/fingertip_fusion_v9_selftest.cpp
)

foreach(target touchplus_fingertip_tracker_selftest touchplus_fingertip_fusion_selftest)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target} PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    )
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    )
    if(MSVC)
        set_property(TARGET ${target} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
        target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc)
    endif()
endforeach()
