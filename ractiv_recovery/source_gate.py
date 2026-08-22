#!/usr/bin/env python3
"""Source-level gates for the first Ractiv recovery boundary.

This does not claim that the 2015 product is runnable. It proves that:
- the branch is anchored to the integrated July historical pipeline;
- compatibility patches remain auditable;
- the dedicated R0/R1 executable compiles only the historical vision stages we
  intend to smoke and excludes the historical OS-output stack by construction.
"""

from __future__ import annotations

import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAIN = ROOT / "track_plus_core" / "track_plus" / "main.cpp"
POINTER = ROOT / "track_plus_core" / "track_plus" / "pointer_mapper.cpp"
CURSOR = ROOT / "track_plus_visual_studio" / "win_cursor_plus" / "MainWindow.xaml.cs"
PROJECT = ROOT / "track_plus_visual_studio" / "track_plus" / "track_plus.vcxproj"
PATCH_DIR = ROOT / "ractiv_recovery" / "patches"
MINIMAL_MAIN = ROOT / "ractiv_recovery" / "log_only_main.cpp"
MINIMAL_CMAKE = ROOT / "ractiv_recovery" / "CMakeLists.txt"
MINIMAL_PACKAGE = ROOT / "ractiv_recovery" / "package_minimal_log_only.ps1"
REPORT = ROOT / "ractiv_recovery" / "source-gate-report.json"


def require(text: str, needle: str, label: str, results: dict[str, object]) -> None:
    ok = needle in text
    results[label] = ok
    if not ok:
        raise SystemExit(f"FAIL: missing invariant: {label}: {needle}")


def forbid(text: str, needle: str, label: str, results: dict[str, object]) -> None:
    ok = needle not in text
    results[label] = ok
    if not ok:
        raise SystemExit(f"FAIL: forbidden R0/R1 dependency present: {label}: {needle}")


def main() -> int:
    main_cpp = MAIN.read_text(encoding="utf-8-sig")
    pointer_cpp = POINTER.read_text(encoding="utf-8-sig")
    cursor_cs = CURSOR.read_text(encoding="utf-8-sig")
    project_xml = PROJECT.read_text(encoding="utf-8-sig")
    minimal_main = MINIMAL_MAIN.read_text(encoding="utf-8-sig")
    minimal_cmake = MINIMAL_CMAKE.read_text(encoding="utf-8-sig")
    minimal_package = MINIMAL_PACKAGE.read_text(encoding="utf-8-sig")
    patches = sorted(PATCH_DIR.glob("*.patch"))
    patch_text = "\n".join(p.read_text(encoding="utf-8") for p in patches)

    results: dict[str, object] = {
        "boundary": "R0/R1 integrated-July archaeology + minimal log-only safety",
        "historical_source_unchanged_on_branch": True,
        "patch_series": [p.name for p in patches],
    }

    # ------------------------------------------------------------------
    # Historical July source remains our evidence/reference.
    # ------------------------------------------------------------------
    require(main_cpp, "camera = new Camera(true, 1280, 480, update);", "historical_camera_1280x480", results)
    require(main_cpp, "flip(image_current_frame, image_flipped, 0);", "historical_vertical_flip", results)
    require(main_cpp, "Rect(0, 0, 640, 480)", "historical_left_eye_split", results)
    require(main_cpp, "Rect(640, 0, 640, 480)", "historical_right_eye_split", results)
    require(main_cpp, "motion_processor0.compute", "historical_motion_stage", results)
    require(main_cpp, "foreground_extractor0.compute", "historical_foreground_stage", results)
    require(main_cpp, "hand_splitter0.compute", "historical_hand_split_stage", results)
    require(main_cpp, "mono_processor0.compute", "historical_mono_hand_stage", results)
    require(main_cpp, "hand_resolver.compute", "historical_hand_resolver_stage", results)
    require(main_cpp, "pointer_mapper.compute", "historical_pointer_mapper_stage", results)
    require(main_cpp, "send_udp_message(\"win_cursor_plus\"", "historical_cursor_transport", results)

    require(pointer_cpp, "dist_target_plane <= actuation_dist", "historical_down_threshold", results)
    require(pointer_cpp, "dist_target_plane > actuation_dist + 5", "historical_release_hysteresis", results)
    require(cursor_cs, "TouchInjector.InjectTouchInput", "historical_windows_touch_injection", results)
    require(cursor_cs, "mouse_event(MOUSEEVENTF_LEFTDOWN", "historical_mouse_fallback", results)

    require(project_xml, "<PlatformToolset>v120</PlatformToolset>", "historical_vs2013_toolset", results)
    results["contains_historical_absolute_path"] = "C:\\External Storage\\Dropbox" in project_xml

    # ------------------------------------------------------------------
    # Compatibility patch series still applies to untouched historical source.
    # ------------------------------------------------------------------
    if not patches:
        raise SystemExit("FAIL: no Ractiv Recovery compatibility patches found")

    cmd = ["git", "apply", "--check", *[str(p.relative_to(ROOT)) for p in patches]]
    proc = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True)
    results["patch_series_applies_cleanly"] = proc.returncode == 0
    results["git_apply_stderr"] = proc.stderr.strip()
    if proc.returncode != 0:
        REPORT.write_text(json.dumps(results, indent=2), encoding="utf-8")
        raise SystemExit("FAIL: compatibility patch series no longer applies cleanly")

    require(patch_text, "ractiv_recovery_log_only = true", "legacy_patch_log_only_default_true", results)
    require(patch_text, "ractiv_recovery_skip_legacy_calibration = true", "legacy_patch_dead_cdn_bypass", results)
    require(patch_text, "LOG_ONLY active: win_cursor_plus launch and OS injection disabled", "legacy_patch_no_injection_banner", results)
    require(patch_text, "!ractiv_recovery_log_only && child_module_name", "legacy_patch_cursor_spawn_guard", results)
    require(patch_text, "<PlatformToolset>v143</PlatformToolset>", "legacy_patch_modern_probe_toolset", results)
    require(patch_text, "dependencies\\OpenCV\\build\\x86\\vc12\\lib", "legacy_patch_bundled_opencv", results)
    require(patch_text, "dependencies\\Etron", "legacy_patch_bundled_etron", results)

    # ------------------------------------------------------------------
    # Minimal recovery runtime: same historical vision stages, output stack absent.
    # ------------------------------------------------------------------
    require(minimal_main, "Camera(true, 1280, 480, on_frame)", "minimal_camera_1280x480", results)
    require(minimal_main, "flip(frame, flipped, 0)", "minimal_historical_vertical_flip", results)
    require(minimal_main, "Rect(0, 0, 640, 480)", "minimal_left_eye_split", results)
    require(minimal_main, "Rect(640, 0, 640, 480)", "minimal_right_eye_split", results)
    require(minimal_main, "motion0.compute", "minimal_motion_stage", results)
    require(minimal_main, "foreground0.compute", "minimal_foreground_stage", results)
    require(minimal_main, "hand0.compute", "minimal_hand_split_stage", results)
    require(minimal_main, "mono0.compute", "minimal_mono_stage", results)
    require(minimal_main, "pose_estimator.compute", "minimal_pose_stage", results)
    require(minimal_main, "OS_INJECTION=DISABLED", "minimal_no_injection_banner", results)

    # Check actual include/API lines rather than comments that intentionally
    # document excluded historical components.
    for needle, label in [
        ('#include "ipc.h"', "minimal_no_ipc_include"),
        ('#include "udp.h"', "minimal_no_udp_include"),
        ('#include "pointer_mapper.h"', "minimal_no_pointer_mapper_include"),
        ('#include "reprojector.h"', "minimal_no_reprojector_include"),
        ('send_udp_message(', "minimal_no_udp_call"),
        ('create_process(', "minimal_no_process_spawn_call"),
        ('mouse_event(', "minimal_no_mouse_injection_call"),
        ('SendInput(', "minimal_no_sendinput_call"),
        ('InjectTouchInput(', "minimal_no_touch_injection_call"),
    ]:
        forbid(minimal_main, needle, label, results)

    require(minimal_cmake, "add_executable(touchplus_ractiv_log_only", "minimal_dedicated_target", results)
    require(minimal_cmake, "Camera.cpp", "minimal_links_historical_camera", results)
    require(minimal_cmake, "mono_processor_new.cpp", "minimal_links_historical_mono", results)
    require(minimal_cmake, "pose_estimator.cpp", "minimal_links_historical_pose", results)
    require(minimal_cmake, "CMAKE_SIZEOF_VOID_P EQUAL 8", "minimal_rejects_x64", results)

    # The target definition itself must not compile known output modules.
    for needle, label in [
        ('"${CORE}/ipc.cpp"', "cmake_excludes_ipc"),
        ('"${CORE}/udp.cpp"', "cmake_excludes_udp"),
        ('"${CORE}/pointer_mapper.cpp"', "cmake_excludes_pointer_mapper"),
        ('"${CORE}/reprojector.cpp"', "cmake_excludes_reprojector"),
        ('"${CORE}/hand_resolver.cpp"', "cmake_excludes_hand_resolver_first_smoke"),
        ('"${CORE}/tool_pointer_mapper.cpp"', "cmake_excludes_tool_pointer_mapper"),
    ]:
        forbid(minimal_cmake, needle, label, results)

    # Packaging is allowed to contain exactly one EXE: the minimal runtime.
    require(minimal_package, '$_ .Name' if False else 'touchplus_ractiv_log_only.exe', "package_minimal_exe_named", results)
    require(minimal_package, "unexpectedExes", "package_rejects_extra_executables", results)
    require(minimal_package, "win_cursor_plus", "package_explicit_cursor_forbidden", results)
    require(minimal_package, "daemon_plus", "package_explicit_daemon_forbidden", results)
    require(minimal_package, "menu_plus", "package_explicit_menu_forbidden", results)
    require(minimal_package, "PointerMapper", "package_binary_string_pointer_gate", results)

    REPORT.write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(json.dumps(results, indent=2))
    print("RACTIV RECOVERY SOURCE GATE: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
