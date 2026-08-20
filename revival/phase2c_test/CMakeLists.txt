cmake_minimum_required(VERSION 3.23)
cmake_policy(SET CMP0091 NEW)
project(TouchPlusPhase2CContact LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "TouchPlus Phase 2C currently targets Windows builds only.")
endif()

add_executable(touchplus_touch_contact_selftest
    ../src/touch_contact_selftest.cpp
)
target_compile_features(touchplus_touch_contact_selftest PRIVATE cxx_std_20)
target_compile_definitions(touchplus_touch_contact_selftest PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)
target_include_directories(touchplus_touch_contact_selftest PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)
if(MSVC)
    set_property(TARGET touchplus_touch_contact_selftest PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    target_compile_options(touchplus_touch_contact_selftest PRIVATE /W4 /permissive- /EHsc)
endif()

# The live Touch+ runtime still depends on the recovered 32-bit Etron SDK.
# Phase 2C.1 therefore builds a dedicated Win32 wrapper executable while the
# semantic state machine itself remains independently testable in x64 CI.
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(ETRON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../dependencies/Windows/Etron")
    set(LEGACY_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../build")

    add_executable(touchplus_phase2c_tracker
        ../src/depth_viewer.cpp
    )
    target_compile_features(touchplus_phase2c_tracker PRIVATE cxx_std_20)
    target_compile_definitions(touchplus_phase2c_tracker PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    )
    target_include_directories(touchplus_phase2c_tracker PRIVATE
        "${ETRON_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    )
    if(MSVC)
        # Phase 2C.1 force-includes a semantic wrapper which in turn includes
        # the accepted 2B.9C.2 runtime. No accepted tracker/depth source is
        # modified by this slice.
        target_compile_options(touchplus_phase2c_tracker PRIVATE
            "/FI${CMAKE_CURRENT_SOURCE_DIR}/../src/touch_contact_runtime.h"
        )
    endif()
    target_link_libraries(touchplus_phase2c_tracker PRIVATE
        mf
        mfplat
        mfreadwrite
        mfuuid
        ole32
        gdi32
        user32
        "${ETRON_DIR}/eSPDI.lib"
        "${ETRON_DIR}/eSPAEAWBCtrl.lib"
    )

    foreach(etron_dll eSPAEAWBCtrl.dll eSPDI.dll EtLib.dll)
        add_custom_command(TARGET touchplus_phase2c_tracker POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LEGACY_BUILD_DIR}/${etron_dll}"
                "$<TARGET_FILE_DIR:touchplus_phase2c_tracker>/${etron_dll}"
            VERBATIM
        )
    endforeach()
endif()
