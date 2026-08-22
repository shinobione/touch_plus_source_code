# Ractiv Recovery — local Win32 build and physical diagnostics

This is the reproducible local path for the isolated Ractiv Recovery R0/R1 runtime.

## Safety boundary

The target built here is **not** the historical Touch+ product executable.

It is `touchplus_ractiv_log_only.exe`, whose CMake target excludes:

- PointerMapper;
- Reprojector/contact output;
- IPC;
- UDP cursor transport;
- `win_cursor_plus` and fallback;
- daemon/menu executables;
- OS touch/mouse injection.

The optional `--viewer` mode is still observational only. It draws the July 2015 MonoProcessor landmarks on the live LEFT/RIGHT camera images; it does not enable any output stack.

Do not run any historical `win_cursor_plus.exe` during R0/R1.

## Current physical status

The minimal runtime has now been built and run on the real Touch+ hardware.

Observed physical evidence:

- Touch+ `1E4E:0107` opens through the historical Camera path;
- serial `0101007379` is recovered from flash;
- recovered sensor initializer applies successfully;
- the historical two-hand startup/wiggle arms `MotionProcessorNew`;
- Motion -> Foreground -> HandSplitter -> MonoProcessor -> PoseEstimator runs on hardware;
- `pose=point` and dynamic palm/index/thumb telemetry have been observed;
- the recovery contour guard prevents the previously observed `cvPointSeqFromMat` crash storm;
- OS injection remains disabled.

The next R1 diagnostic is visual validation of the historical `pt_index` / `pt_thumb` locations before adding `HandResolver`.

## Requirements

- Windows 10/11 x64 host;
- Visual Studio 2022 or Build Tools 2022 with **Desktop development with C++**;
- **C++ ATL for latest v143 build tools (x86 & x64)** (`Microsoft.VisualStudio.Component.VC.ATL`);
- CMake available as `cmake.exe`;
- Visual C++ 2013 Redistributable **x86** at runtime for the bundled OpenCV 2.4.9 VC12 DLLs;
- Git checkout of `shinobione/touch_plus_source_code`.

The executable itself is deliberately generated as **Win32/x86** because the recovered Etron control SDK is 32-bit.

## Checkout

From PowerShell in the repository:

```powershell
git fetch origin
git switch ractiv-recovery/main
git pull --ff-only origin ractiv-recovery/main
```

Verify:

```powershell
git rev-parse --abbrev-ref HEAD
git rev-parse --short HEAD
```

## Build

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\ractiv_recovery\build_minimal_log_only.ps1
```

Evidence is written to:

```text
ractiv_recovery\minimal-build.log
ractiv_recovery\minimal-build-status.json
```

If compilation succeeds, package it:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\ractiv_recovery\package_minimal_log_only.ps1
```

Expected package:

```text
ractiv_recovery\out-minimal\touchplus-ractiv-log-only-windows-x86.zip
ractiv_recovery\out-minimal\package-manifest.json
```

The packaging script fails if any historical cursor/menu/daemon executable appears or if known PointerMapper/cursor strings are found in the minimal EXE.

## Runtime: telemetry only

From the packaged runtime directory:

```powershell
.\touchplus_ractiv_log_only.exe 2>&1 |
  Tee-Object .\ractiv-smoke.log
```

The July tracker requires its historical startup motion calibration. After the background is seeded, present **two separated hands** and move them clearly for a few seconds until `motion_pass` starts increasing.

## Runtime: LEFT/RIGHT landmark viewer

Use the same safe executable with:

```powershell
.\touchplus_ractiv_log_only.exe --viewer 2>&1 |
  Tee-Object .\ractiv-landmark-viewer.log
```

The window shows the vertically corrected historical stereo pair at 640x480 per eye and overlays the current MonoProcessor landmarks:

```text
PALM  = cyan
INDEX = magenta
THUMB = yellow
```

The MonoProcessor coordinates are produced at 160x120 and mapped back to the native 640x480 eye image with the exact x4 resize relationship used by the recovery pipeline.

`Q` or `ESC` closes the viewer safely. `Ctrl+C` remains available from the console.

What matters in this diagnostic:

1. whether `INDEX` sits on the actual visible index fingertip;
2. whether LEFT and RIGHT identify the same anatomical finger;
3. whether `THUMB` is plausible when present;
4. whether incorrect/ambiguous poses fail with missing landmarks rather than convincing wrong points.

Do **not** add PointerMapper, Reprojector or Windows output based only on a plausible-looking point. The next historical layer, if needed, is `HandResolver`, which exists specifically to refine the coarse index/thumb against the full-resolution foreground.

## If the build fails

Do not change random project settings or install arbitrary old SDKs. Return:

```powershell
Get-Content .\ractiv_recovery\minimal-build-status.json
Get-Content .\ractiv_recovery\minimal-build.log -Tail 120
```

For a noisy MSVC log:

```powershell
Select-String `
  -Path .\ractiv_recovery\minimal-build.log `
  -Pattern ': error ','fatal error','LNK[0-9]{4}','MSB[0-9]{4}' `
  -Context 3,6 |
  Select-Object -First 80
```

The recovery rule remains: patch the **first concrete blocker only**, preserve the historical algorithms as evidence, and keep output/injection disabled until the observational layers are physically validated.
