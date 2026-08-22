# Ractiv Recovery Status

Date: 2026-08-22

PR: #12

State: **R0/R1 ACTIVE — DO NOT MERGE — NO HARDWARE SMOKE YET**

Historical source base: `master` / `902cb8cc3e660b0ca8d9049fabd383c34da69607`.

CI base: `ractiv-recovery/ci-base` — contains only the Ractiv Recovery workflow so the historical `master` archive remains untouched.

## Current objective

Produce a reproducible Win32 **LOG_ONLY** executable from the integrated July Ractiv lineage and run the first observational smoke on the real Touch+.

The primary recovery target is now a dedicated minimal entrypoint:

`ractiv_recovery/log_only_main.cpp`

with its own Win32 CMake target. This deliberately compiles only:

`Camera -> MotionProcessorNew -> ForegroundExtractorNew -> HandSplitterNew -> MonoProcessorNew -> PoseEstimator telemetry`

The historical full application remains source evidence and a secondary build-archaeology probe; it is no longer the critical path for the first smoke.

## Safety boundary

The minimal target excludes by construction:

- `PointerMapper`;
- `Reprojector` / historical contact output;
- IPC;
- UDP cursor transport;
- `win_cursor_plus` and fallback;
- daemon/menu executables;
- Windows touch/mouse injection.

Packaging permits exactly one EXE: `touchplus_ractiv_log_only.exe`.

The first physical smoke stops at camera/background/hand/mono/pose telemetry.

## Historical compatibility patch series

These remain useful for full-product archaeology and preserve a second route:

1. `0001-log-only-bringup.patch`
2. `0002-portable-vs-project.patch`

## Build paths

Primary:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\ractiv_recovery\build_minimal_log_only.ps1
powershell.exe -ExecutionPolicy Bypass -File .\ractiv_recovery\package_minimal_log_only.ps1
```

Secondary/non-blocking archaeology:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\ractiv_recovery\build_probe.ps1
```

## CI visibility note

The GitHub connector available during this recovery work exposes pull-request-triggered workflow runs but not all push-triggered runs. No build PASS is claimed until an actual job/artifact or equivalent concrete compiler result is observed.

## Next decision

1. Obtain the first concrete Win32 compiler/linker result for `touchplus_ractiv_log_only`.
2. Patch only the exact blocker if compilation fails.
3. When compilation + package safety gate both pass, perform the first real Touch+ Ractiv smoke with **OS_INJECTION=DISABLED**.
4. Only after that decide whether to continue July anatomy, port December SCOPA, or inject Revival calibration/metric geometry.
