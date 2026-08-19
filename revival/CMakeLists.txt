cmake_minimum_required(VERSION 3.23)
cmake_policy(SET CMP0091 NEW)
project(TouchPlusRevival LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "TouchPlus Revival currently targets Windows only.")
endif()

set(REVIVAL_TARGETS)

add_executable(touchplus_probe
    src/main.cpp
)
list(APPEND REVIVAL_TARGETS touchplus_probe)
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

add_executable(touchplus_usb_probe
    src/usb_probe.cpp
)
list(APPEND REVIVAL_TARGETS touchplus_usb_probe)
target_compile_features(touchplus_usb_probe PRIVATE cxx_std_20)
target_compile_definitions(touchplus_usb_probe PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(touchplus_usb_probe PRIVATE
    cfgmgr32
    setupapi
)

# The recovered Etron control SDK is 32-bit only. Anything that combines
# vendor unlock/control with live video therefore gets a dedicated Win32 build.
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(ETRON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../dependencies/Windows/Etron")
    set(LEGACY_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../build")

    add_executable(touchplus_atomic_probe
        src/atomic_probe.cpp
    )
    list(APPEND REVIVAL_TARGETS touchplus_atomic_probe)
    target_compile_features(touchplus_atomic_probe PRIVATE cxx_std_20)
    target_compile_definitions(touchplus_atomic_probe PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    )
    target_include_directories(touchplus_atomic_probe PRIVATE "${ETRON_DIR}")
    target_link_libraries(touchplus_atomic_probe PRIVATE
        mf
        mfplat
        mfreadwrite
        mfuuid
        ole32
        windowscodecs
        "${ETRON_DIR}/eSPDI.lib"
        "${ETRON_DIR}/eSPAEAWBCtrl.lib"
    )

    # Phase 1A RT deliberately avoids MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING.
    # Media Foundation decodes MJPEG to YUY2; the viewer performs its own tiny
    # YUY2->BGRA conversion and keeps capture on a thread separate from GDI.
    add_executable(touchplus_stereo_viewer
        src/stereo_viewer_rt.cpp
    )
    list(APPEND REVIVAL_TARGETS touchplus_stereo_viewer)
    target_compile_features(touchplus_stereo_viewer PRIVATE cxx_std_20)
    target_compile_definitions(touchplus_stereo_viewer PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    )
    target_include_directories(touchplus_stereo_viewer PRIVATE "${ETRON_DIR}")
    target_link_libraries(touchplus_stereo_viewer PRIVATE
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

    # Phase 1B.2a keeps the proven live stream open for the entire calibration
    # session. SPACE captures the next decoded stereo frame into synchronized
    # full/left/right PNG files without reopening or re-unlocking the device.
    add_executable(touchplus_calibration_capture
        src/calibration_capture.cpp
    )
    list(APPEND REVIVAL_TARGETS touchplus_calibration_capture)
    target_compile_features(touchplus_calibration_capture PRIVATE cxx_std_20)
    target_compile_definitions(touchplus_calibration_capture PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    )
    target_include_directories(touchplus_calibration_capture PRIVATE "${ETRON_DIR}")
    target_link_libraries(touchplus_calibration_capture PRIVATE
        mf
        mfplat
        mfreadwrite
        mfuuid
        ole32
        gdi32
        user32
        windowscodecs
        "${ETRON_DIR}/eSPDI.lib"
        "${ETRON_DIR}/eSPAEAWBCtrl.lib"
    )

    # Phase 1C live integration consumes only a physically validated
    # per-serial calibration. It rectifies both eyes in-process, computes a
    # dependency-free diagnostic disparity heatmap and reports full-resolution
    # subpixel camera-coordinate Z under the mouse cursor through Q.
    add_executable(touchplus_depth_viewer
        src/depth_viewer.cpp
        src/depth_math.h
        src/depth_point_robust.h
    )
    list(APPEND REVIVAL_TARGETS touchplus_depth_viewer)
    target_compile_features(touchplus_depth_viewer PRIVATE cxx_std_20)
    target_compile_definitions(touchplus_depth_viewer PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    )
    target_include_directories(touchplus_depth_viewer PRIVATE
        "${ETRON_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    # Keep depth_math.h as the dependency-free reference implementation, but
    # force-include a runtime wrapper that renames the permissive point matcher
    # and replaces it with texture/NCC/LR-consistency/local-consensus/temporal
    # validation. This isolates the physical-smoke fix to the live viewer.
    if(MSVC)
        target_compile_options(touchplus_depth_viewer PRIVATE
            "/FI${CMAKE_CURRENT_SOURCE_DIR}/src/depth_point_robust.h"
        )
    endif()
    target_link_libraries(touchplus_depth_viewer PRIVATE
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

    # Raw timing probe: no decode, no color conversion, no GDI. This isolates
    # the actual native MJPEG sample cadence delivered by the Touch+ driver.
    add_executable(touchplus_timing_probe
        src/timing_probe.cpp
    )
    list(APPEND REVIVAL_TARGETS touchplus_timing_probe)
    target_compile_features(touchplus_timing_probe PRIVATE cxx_std_20)
    target_compile_definitions(touchplus_timing_probe PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    )
    target_include_directories(touchplus_timing_probe PRIVATE "${ETRON_DIR}")
    target_link_libraries(touchplus_timing_probe PRIVATE
        mf
        mfplat
        mfreadwrite
        mfuuid
        ole32
        "${ETRON_DIR}/eSPDI.lib"
        "${ETRON_DIR}/eSPAEAWBCtrl.lib"
    )

    # Legacy-path timing probe: replay the exact DirectShow IAMStreamConfig
    # negotiation Ractiv used, including AvgTimePerFrame = 1e7 / 60.
    add_executable(touchplus_directshow_timing_probe
        src/directshow_timing_probe.cpp
    )
    list(APPEND REVIVAL_TARGETS touchplus_directshow_timing_probe)
    target_compile_features(touchplus_directshow_timing_probe PRIVATE cxx_std_20)
    target_compile_definitions(touchplus_directshow_timing_probe PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    )
    target_include_directories(touchplus_directshow_timing_probe PRIVATE "${ETRON_DIR}")
    target_link_libraries(touchplus_directshow_timing_probe PRIVATE
        strmiids
        ole32
        oleaut32
        "${ETRON_DIR}/eSPDI.lib"
        "${ETRON_DIR}/eSPAEAWBCtrl.lib"
    )

    # Same legacy DirectShow timing path, but with the complete recovered
    # Ractiv sensor initializer applied atomically immediately after SWUnlock.
    add_executable(touchplus_directshow_legacy_timing_probe
        src/directshow_legacy_timing_probe.cpp
    )
    list(APPEND REVIVAL_TARGETS touchplus_directshow_legacy_timing_probe)
    target_compile_features(touchplus_directshow_legacy_timing_probe PRIVATE cxx_std_20)
    target_compile_definitions(touchplus_directshow_legacy_timing_probe PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    )
    target_include_directories(touchplus_directshow_legacy_timing_probe PRIVATE "${ETRON_DIR}")
    target_link_libraries(touchplus_directshow_legacy_timing_probe PRIVATE
        strmiids
        ole32
        oleaut32
        "${ETRON_DIR}/eSPDI.lib"
        "${ETRON_DIR}/eSPAEAWBCtrl.lib"
    )

    # Etron initializes COM internally on the caller thread. On the physical
    # Touch+ this can leave the thread in a different apartment model before
    # atomic_probe.cpp reaches its own CoInitializeEx call. Force-include a
    # tiny compatibility shim that reuses that apartment on RPC_E_CHANGED_MODE
    # while keeping CoUninitialize balanced when we own the COM init count.
    if(MSVC)
        target_compile_options(touchplus_atomic_probe PRIVATE
            "/FI${CMAKE_CURRENT_SOURCE_DIR}/src/com_compat.h"
        )
    endif()

    foreach(etron_target
            touchplus_atomic_probe
            touchplus_stereo_viewer
            touchplus_calibration_capture
            touchplus_depth_viewer
            touchplus_timing_probe
            touchplus_directshow_timing_probe
            touchplus_directshow_legacy_timing_probe)
        foreach(etron_dll eSPAEAWBCtrl.dll eSPDI.dll EtLib.dll)
            add_custom_command(TARGET ${etron_target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${LEGACY_BUILD_DIR}/${etron_dll}"
                    "$<TARGET_FILE_DIR:${etron_target}>/${etron_dll}"
                VERBATIM
            )
        endforeach()
    endforeach()
endif()

if(MSVC)
    foreach(target IN LISTS REVIVAL_TARGETS)
        set_property(TARGET ${target} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
        target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc)
    endforeach()
endif()