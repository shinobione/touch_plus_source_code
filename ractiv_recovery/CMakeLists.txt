cmake_minimum_required(VERSION 3.21)

project(touchplus_ractiv_recovery_log_only LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Ractiv Recovery R0/R1 target is Windows-only")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Ractiv Recovery R0/R1 must be generated for Win32/x86 (use: cmake -A Win32)")
endif()

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# The bundled OpenCV/Etron/TurboJPEG import libraries were built for the old
# Win32 MSVC runtime. Use the dynamic CRT, matching the historical Release
# project closely and avoiding accidental /MT ABI mismatches.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(CORE "${REPO_ROOT}/track_plus_core/track_plus")
set(DEPS "${REPO_ROOT}/dependencies")
set(OPENCV_ROOT "${DEPS}/OpenCV/build")
set(OPENCV_LIB "${OPENCV_ROOT}/x86/vc12/lib")
set(ETRON "${DEPS}/Etron")
set(TURBOJPEG "${DEPS}/TurboJPEG")
set(DIRECTSHOW "${DEPS}/DirectShow")

# This is intentionally NOT the historical product target. It compiles the
# smallest practical source closure for the first July-vision hardware smoke.
# Explicitly excluded: historical main.cpp, IPC, UDP, Reprojector,
# PointerMapper, HandResolver/output mapping, tool mode and all Windows cursor
# executables. No legacy OS-input path can therefore be reached from this EXE.
set(RACTIV_RECOVERY_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/log_only_main.cpp"

    # Device + stereo capture / recovered sensor initialization.
    "${CORE}/Camera.cpp"
    "${CORE}/CameraDS.cpp"
    "${CORE}/RactivJPEGDecompressor.cpp"
    "${CORE}/camera_initializer_new.cpp"

    # Exact July segmentation/anatomy stages used by the minimal entrypoint.
    "${CORE}/blob_detector_new.cpp"
    "${CORE}/blob_new.cpp"
    "${CORE}/contour_functions.cpp"
    "${CORE}/foreground_extractor_new.cpp"
    "${CORE}/hand_splitter_new.cpp"
    "${CORE}/mono_processor_new.cpp"
    "${CORE}/motion_processor_new.cpp"
    "${CORE}/pose_estimator.cpp"

    # Shared utilities required by the above stages.
    "${CORE}/filesystem.cpp"
    "${CORE}/globals.cpp"
    "${CORE}/histogram_builder.cpp"
    "${CORE}/low_pass_filter.cpp"
    "${CORE}/math_plus.cpp"
    "${CORE}/mat_functions.cpp"
    "${CORE}/permutation.cpp"
    "${CORE}/string_functions.cpp"
    "${CORE}/value_store.cpp"
)

add_executable(touchplus_ractiv_log_only ${RACTIV_RECOVERY_SOURCES})

# Keep Release output deterministic so packaging does not need to understand
# Visual Studio's per-config directory layout.
set_target_properties(touchplus_ractiv_log_only PROPERTIES
    OUTPUT_NAME "touchplus_ractiv_log_only"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/out"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/out"
)

target_include_directories(touchplus_ractiv_log_only PRIVATE
    "${CORE}"
    "${ETRON}"
    "${TURBOJPEG}"
    "${OPENCV_ROOT}/include"
    "${DIRECTSHOW}/Include"
)

target_compile_definitions(touchplus_ractiv_log_only PRIVATE
    WIN32
    _CONSOLE
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
    RACTIV_RECOVERY_LOG_ONLY=1
)

if(MSVC)
    target_compile_options(touchplus_ractiv_log_only PRIVATE
        /W3
        /MP
        /EHsc
        /Zc:wchar_t
    )
endif()

# Use explicit absolute paths for the bundled historical import libraries so a
# developer machine cannot silently substitute another OpenCV/Etron build.
set(OPENCV_LIBS
    "${OPENCV_LIB}/opencv_core249.lib"
    "${OPENCV_LIB}/opencv_highgui249.lib"
    "${OPENCV_LIB}/opencv_imgproc249.lib"
    "${OPENCV_LIB}/opencv_video249.lib"
    "${OPENCV_LIB}/opencv_flann249.lib"
    "${OPENCV_LIB}/opencv_nonfree249.lib"
    "${OPENCV_LIB}/opencv_features2d249.lib"
    "${OPENCV_LIB}/opencv_ocl249.lib"
    "${OPENCV_LIB}/opencv_objdetect249.lib"
)

foreach(_lib IN LISTS OPENCV_LIBS)
    if(NOT EXISTS "${_lib}")
        message(FATAL_ERROR "Bundled OpenCV import library missing: ${_lib}")
    endif()
endforeach()

foreach(_lib IN ITEMS
    "${ETRON}/eSPDI.lib"
    "${ETRON}/eSPAEAWBCtrl.lib"
    "${TURBOJPEG}/turbojpeg.lib")
    if(NOT EXISTS "${_lib}")
        message(FATAL_ERROR "Bundled historical import library missing: ${_lib}")
    endif()
endforeach()

target_link_libraries(touchplus_ractiv_log_only PRIVATE
    ${OPENCV_LIBS}
    "${ETRON}/eSPDI.lib"
    "${ETRON}/eSPAEAWBCtrl.lib"
    "${TURBOJPEG}/turbojpeg.lib"

    # Current Windows SDK system libraries; no old product executable/UI libs.
    dxguid
    ole32
    oleaut32
    strmiids
    uuid
    ws2_32
)

message(STATUS "Touch+ Ractiv Recovery target: minimal LOG_ONLY Win32")
message(STATUS "Historical core: ${CORE}")
message(STATUS "OS input / PointerMapper / Reprojector / IPC / UDP: excluded")
