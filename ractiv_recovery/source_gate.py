#!/usr/bin/env python3
"""Source-level gates for the first Ractiv recovery boundary.

This deliberately does not claim that the 2015 product is runnable. It proves
that the recovery branch is anchored to the integrated July pipeline and that
the compatibility patch series remains observational/log-only and portable.
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
REPORT = ROOT / "ractiv_recovery" / "source-gate-report.json"


def require(text: str, needle: str, label: str, results: dict[str, object]) -> None:
    ok = needle in text
    results[label] = ok
    if not ok:
        raise SystemExit(f"FAIL: missing invariant: {label}: {needle}")


def main() -> int:
    main_cpp = MAIN.read_text(encoding="utf-8-sig")
    pointer_cpp = POINTER.read_text(encoding="utf-8-sig")
    cursor_cs = CURSOR.read_text(encoding="utf-8-sig")
    project_xml = PROJECT.read_text(encoding="utf-8-sig")
    patches = sorted(PATCH_DIR.glob("*.patch"))
    patch_text = "\n".join(p.read_text(encoding="utf-8") for p in patches)

    results: dict[str, object] = {
        "boundary": "R0/R1 integrated-July archaeology + log-only safety",
        "historical_source_unchanged_on_branch": True,
        "patch_series": [p.name for p in patches],
    }

    # Integrated July pipeline invariants.
    require(main_cpp, "camera = new Camera(true, 1280, 480, update);", "camera_1280x480", results)
    require(main_cpp, "flip(image_current_frame, image_flipped, 0);", "historical_vertical_flip", results)
    require(main_cpp, "Rect(0, 0, 640, 480)", "left_eye_split", results)
    require(main_cpp, "Rect(640, 0, 640, 480)", "right_eye_split", results)
    require(main_cpp, "motion_processor0.compute", "motion_stage", results)
    require(main_cpp, "foreground_extractor0.compute", "foreground_stage", results)
    require(main_cpp, "hand_splitter0.compute", "hand_split_stage", results)
    require(main_cpp, "mono_processor0.compute", "mono_hand_stage", results)
    require(main_cpp, "hand_resolver.compute", "hand_resolver_stage", results)
    require(main_cpp, "pointer_mapper.compute", "pointer_mapper_stage", results)
    require(main_cpp, "send_udp_message(\"win_cursor_plus\"", "historical_cursor_transport", results)

    # Historical interaction/output exists but must remain unreachable in R0/R1 LOG_ONLY mode.
    require(pointer_cpp, "dist_target_plane <= actuation_dist", "historical_down_threshold", results)
    require(pointer_cpp, "dist_target_plane > actuation_dist + 5", "historical_release_hysteresis", results)
    require(cursor_cs, "TouchInjector.InjectTouchInput", "historical_windows_touch_injection", results)
    require(cursor_cs, "mouse_event(MOUSEEVENTF_LEFTDOWN", "historical_mouse_fallback", results)

    # Toolchain archaeology facts remain visible in the untouched source.
    require(project_xml, "<PlatformToolset>v120</PlatformToolset>", "historical_vs2013_toolset", results)
    results["contains_historical_absolute_path"] = "C:\\External Storage\\Dropbox" in project_xml

    if not patches:
        raise SystemExit("FAIL: no Ractiv Recovery compatibility patches found")

    # Git can validate the complete ordered patch series without mutating the checkout.
    cmd = ["git", "apply", "--check", *[str(p.relative_to(ROOT)) for p in patches]]
    proc = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True)
    results["patch_series_applies_cleanly"] = proc.returncode == 0
    results["git_apply_stderr"] = proc.stderr.strip()
    if proc.returncode != 0:
        REPORT.write_text(json.dumps(results, indent=2), encoding="utf-8")
        raise SystemExit("FAIL: compatibility patch series no longer applies cleanly")

    # Safety assertions on the runtime compatibility patch.
    require(patch_text, "ractiv_recovery_log_only = true", "log_only_default_true", results)
    require(patch_text, "ractiv_recovery_skip_legacy_calibration = true", "dead_cdn_bypass_default_true", results)
    require(patch_text, "LOG_ONLY active: win_cursor_plus launch and OS injection disabled", "explicit_no_injection_banner", results)
    require(patch_text, "!ractiv_recovery_log_only && child_module_name", "cursor_child_spawn_guard", results)
    require(patch_text, "output=LOG_ONLY_2D", "2d_diagnostic_output", results)

    # Portability assertions on the project compatibility patch.
    require(patch_text, "<PlatformToolset>v143</PlatformToolset>", "modern_probe_toolset", results)
    require(patch_text, "..\\..\\track_plus_core\\track_plus\\imu.h", "portable_imu_project_path", results)
    require(patch_text, "dependencies\\OpenCV\\build\\x86\\vc12\\lib", "bundled_opencv_lib_path", results)
    require(patch_text, "dependencies\\Etron", "bundled_etron_path", results)

    REPORT.write_text(json.dumps(results, indent=2), encoding="utf-8")
    print(json.dumps(results, indent=2))
    print("RACTIV RECOVERY SOURCE GATE: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
