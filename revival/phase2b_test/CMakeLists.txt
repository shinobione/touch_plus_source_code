cmake_minimum_required(VERSION 3.23)
project(TouchPlusPhase2BTrackerTest LANGUAGES CXX)

add_executable(touchplus_fingertip_tracker_selftest
    ../src/fingertip_tracker_selftest.cpp
)
add_executable(touchplus_fingertip_fusion_selftest
    ../src/fingertip_fusion_v9_selftest.cpp
)
add_executable(touchplus_fingertip_refiner_selftest
    ../src/fingertip_refiner_v10_selftest.cpp
)
add_executable(touchplus_fingertip_promotion_gate_selftest
    ../src/fingertip_promotion_gate_v10c_selftest.cpp
)
add_executable(touchplus_fingertip_authoritative_selection_selftest
    ../src/fingertip_authoritative_selection_v10d_selftest.cpp
)
add_executable(touchplus_contact_semantics_selftest
    ../src/contact_state_machine_v2c1_selftest.cpp
)
add_executable(touchplus_phase2c1a_diagnostic_selftest
    ../src/phase2c1a_diagnostic_selftest.cpp
)

foreach(target
        touchplus_fingertip_tracker_selftest
        touchplus_fingertip_fusion_selftest
        touchplus_fingertip_refiner_selftest
        touchplus_fingertip_promotion_gate_selftest
        touchplus_fingertip_authoritative_selection_selftest
        touchplus_contact_semantics_selftest
        touchplus_phase2c1a_diagnostic_selftest)
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
