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

    # CameraDS uses ATL CComPtr and filesystem.h historically includes atlstr.h.
    # The Desktop C++ workload does not always install ATL by default, so fail
    # at configure time with an actionable prerequisite instead of a long C1083
    # compile cascade.
    include(CheckIncludeFileCXX)
    check_include_file_cxx("atlbase.h" RACTIV_HAVE_ATLBASE_H)
    check_include_file_cxx("atlstr.h" RACTIV_HAVE_ATLSTR_H)
    if(NOT RACTIV_HAVE_ATLBASE_H OR NOT RACTIV_HAVE_ATLSTR_H)
        message(FATAL_ERROR
            "MSVC ATL headers are required by the recovered July CameraDS path. "
            "Install Visual Studio component 'C++ ATL for latest v143 build tools (x86 & x64)' "
            "(component id Microsoft.VisualStudio.Component.VC.ATL), then reconfigure.")
    endif()
endif()

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(CORE "${REPO_ROOT}/track_plus_core/track_plus")
set(DEPS "${REPO_ROOT}/dependencies")
set(OPENCV_ROOT "${DEPS}/OpenCV/build")
set(OPENCV_LIB "${OPENCV_ROOT}/x86/vc12/lib")
set(ETRON "${DEPS}/Etron")
set(TURBOJPEG "${DEPS}/TurboJPEG")
set(DIRECTSHOW "${DEPS}/DirectShow")

# Keep the July historical source itself untouched. For the isolated LOG_ONLY
# target only, generate a recovery copy of MonoProcessorNew with a narrow guard
# around its old OpenCV 2.4 approxPolyDP call. Physical smoke showed that a
# degenerate hand contour can otherwise reach cvPointSeqFromMat and throw on
# every subsequent frame. The generated copy drops only that bad frame.
set(RACTIV_MONO_ORIGINAL "${CORE}/mono_processor_new.cpp")
set(RACTIV_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(RACTIV_MONO_RECOVERY "${RACTIV_GENERATED_DIR}/mono_processor_new_recovery.cpp")
file(MAKE_DIRECTORY "${RACTIV_GENERATED_DIR}")
file(READ "${RACTIV_MONO_ORIGINAL}" RACTIV_MONO_SOURCE)

set(RACTIV_MONO_APPROX_NEEDLE [=[	approxPolyDP(Mat(contour_sorted), contour_approximated, 2, true);]=])
set(RACTIV_MONO_APPROX_REPLACEMENT [=[	if (contour_sorted.size() < 3)
	{
		const int recovery_guard_rejects = value_store.get_int("recovery_contour_guard_rejects") + 1;
		value_store.set_int("recovery_contour_guard_rejects", recovery_guard_rejects);
		value_store.set_bool("reset", true);
		if ((recovery_guard_rejects % 60) == 1)
			std::cerr << "[RACTIV_RECOVERY] WARN: degenerate mono contour side=" << name
					  << " points=" << contour_sorted.size()
					  << " action=DROP_FRAME_CONTINUE" << std::endl;
		return false;
	}

	approxPolyDP(contour_sorted, contour_approximated, 2, true);
	if (contour_approximated.empty())
	{
		const int recovery_guard_rejects = value_store.get_int("recovery_contour_guard_rejects") + 1;
		value_store.set_int("recovery_contour_guard_rejects", recovery_guard_rejects);
		value_store.set_bool("reset", true);
		if ((recovery_guard_rejects % 60) == 1)
			std::cerr << "[RACTIV_RECOVERY] WARN: empty mono approximation side=" << name
					  << " input_points=" << contour_sorted.size()
					  << " action=DROP_FRAME_CONTINUE" << std::endl;
		return false;
	}]=])

string(FIND "${RACTIV_MONO_SOURCE}" "${RACTIV_MONO_APPROX_NEEDLE}" RACTIV_MONO_APPROX_POS)
if(RACTIV_MONO_APPROX_POS EQUAL -1)
    message(FATAL_ERROR
        "Recovery mono guard could not find the expected July approxPolyDP call; refusing an unverified source transform")
endif()
string(REPLACE
    "${RACTIV_MONO_APPROX_NEEDLE}"
    "${RACTIV_MONO_APPROX_REPLACEMENT}"
    RACTIV_MONO_SOURCE
    "${RACTIV_MONO_SOURCE}")
file(WRITE "${RACTIV_MONO_RECOVERY}" "${RACTIV_MONO_SOURCE}")

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
    "${RACTIV_MONO_RECOVERY}"
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

# IMPORTANT: the bundled DirectShow SDK contains an obsolete basetsd.h. If its
# include directory is searched before the current Windows SDK, modern winnt.h
# sees that stale basetsd.h and POINTER_64 is never defined, producing the
# misleading PVOID64/Buffer errors observed on the first real local build.
# Put the current Windows SDK shared/um headers ahead of the historical
# DirectShow fallback. qedit.h is absent from current SDKs, so it still falls
# through to the bundled DirectShow copy while core Windows headers stay modern.
if(MSVC)
    # CMake requires parentheses in environment-variable names to be escaped.
    # ProgramFiles(x86) is therefore read via ProgramFiles\(x86\), then
    # normalized before constructing the selected Windows SDK include root.
    set(RACTIV_PROGRAMFILES_X86 "$ENV{ProgramFiles\(x86\)}")
    if(RACTIV_PROGRAMFILES_X86 STREQUAL "")
        message(FATAL_ERROR "ProgramFiles(x86) environment variable is not available")
    endif()
    file(TO_CMAKE_PATH "${RACTIV_PROGRAMFILES_X86}" RACTIV_PROGRAMFILES_X86)

    set(RACTIV_WINSDK_INCLUDE_ROOT
        "${RACTIV_PROGRAMFILES_X86}/Windows Kits/10/Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")
    if(NOT EXISTS "${RACTIV_WINSDK_INCLUDE_ROOT}/shared/basetsd.h" OR
       NOT EXISTS "${RACTIV_WINSDK_INCLUDE_ROOT}/um/windows.h")
        message(FATAL_ERROR
            "Could not locate the selected Windows SDK include tree at: ${RACTIV_WINSDK_INCLUDE_ROOT}")
    endif()

    target_include_directories(touchplus_ractiv_log_only BEFORE PRIVATE
        "${RACTIV_WINSDK_INCLUDE_ROOT}/shared"
        "${RACTIV_WINSDK_INCLUDE_ROOT}/um"
    )
endif()

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
    WIN32_LEAN_AND_MEAN
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
message(STATUS "Recovery MonoProcessor source: ${RACTIV_MONO_RECOVERY}")
message(STATUS "OS input / PointerMapper / Reprojector / IPC / UDP: excluded")
