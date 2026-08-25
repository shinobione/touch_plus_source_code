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
add_executable(touchplus_contact_proxy_shadow_v2c1h_selftest
    ../src/contact_proxy_shadow_v2c1h_selftest.cpp
)
add_executable(touchplus_phase2c1i_shadow_dataset_selftest
    ../src/phase2c1i_shadow_dataset_selftest.cpp
)

foreach(target
        touchplus_fingertip_tracker_selftest
        touchplus_fingertip_fusion_selftest
        touchplus_fingertip_refiner_selftest
        touchplus_fingertip_promotion_gate_selftest
        touchplus_fingertip_authoritative_selection_selftest
        touchplus_contact_semantics_selftest
        touchplus_phase2c1a_diagnostic_selftest
        touchplus_contact_proxy_shadow_v2c1h_selftest
        touchplus_phase2c1i_shadow_dataset_selftest)
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

# Phase 2C.1H is diagnostic-only, but its synthetic safety regression must
# actually execute in both x64 and Win32 tracker CI rather than merely compile.
# A non-zero exit code fails the enclosing build step.
add_custom_command(
    TARGET touchplus_contact_proxy_shadow_v2c1h_selftest
    POST_BUILD
    COMMAND "$<TARGET_FILE:touchplus_contact_proxy_shadow_v2c1h_selftest>"
    COMMENT "Run Phase 2C.1H shadow contact proxy self-test"
    VERBATIM
)

# Phase 2C.1I verifies the numeric non-conflicting label scheme, exact CSV
# safety columns, preservation of invalid per-frame rows and shadow-only
# ownership in both x64 and Win32 tracker builds.
add_custom_command(
    TARGET touchplus_phase2c1i_shadow_dataset_selftest
    POST_BUILD
    COMMAND "$<TARGET_FILE:touchplus_phase2c1i_shadow_dataset_selftest>"
    COMMENT "Run Phase 2C.1I shadow dataset CSV self-test"
    VERBATIM
)
