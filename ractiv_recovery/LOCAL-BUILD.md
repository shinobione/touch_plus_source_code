# Ractiv Recovery — local Win32 build

This is the fallback/diagnostic build path for R0/R1 when GitHub Actions is not observable.

## Safety

The target built here is **not** the historical Touch+ product executable.

It is `touchplus_ractiv_log_only.exe`, whose CMake target excludes:

- PointerMapper;
- Reprojector/contact output;
- IPC;
- UDP cursor transport;
- `win_cursor_plus` and fallback;
- daemon/menu executables;
- OS touch/mouse injection.

Do not run any historical `win_cursor_plus.exe` during R0/R1.

## Requirements

- Windows 10/11 x64 host;
- Visual Studio 2022 or Build Tools 2022 with **Desktop development with C++**;
- CMake available as `cmake.exe`;
- Git checkout of `shinobione/touch_plus_source_code`.

The executable itself is deliberately generated as **Win32/x86** because the recovered Etron control SDK is 32-bit.

## Checkout

From a PowerShell in the repository:

```powershell
git fetch origin
git switch ractiv-recovery/main
git pull --ff-only origin ractiv-recovery/main
```

Verify:

```powershell
git rev-parse --abbrev-ref HEAD
```

Expected:

```text
ractiv-recovery/main
```

## Build

Run:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\ractiv_recovery\build_minimal_log_only.ps1
```

The script always writes evidence to:

```text
ractiv_recovery\minimal-build.log
ractiv_recovery\minimal-build-status.json
```

If compilation succeeds, package it:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\ractiv_recovery\package_minimal_log_only.ps1
```

Expected safe package:

```text
ractiv_recovery\out-minimal\touchplus-ractiv-log-only-windows-x86.zip
ractiv_recovery\out-minimal\package-manifest.json
```

The packaging script fails if any historical cursor/menu/daemon executable appears or if known PointerMapper/cursor strings are found in the minimal EXE.

## If the build fails

Do **not** install random old SDKs or change project settings manually.

Return only:

```powershell
Get-Content .\ractiv_recovery\minimal-build-status.json
Get-Content .\ractiv_recovery\minimal-build.log -Tail 120
```

The recovery rule is to patch the **first concrete compiler/linker blocker only** and keep the historical algorithms unchanged.

## First hardware smoke — not yet authorized

Do not launch the resulting EXE against the Touch+ until both compilation and the package safety gate have been reviewed. When authorized, the first smoke will be observational only and will stop at:

`camera -> motion -> foreground -> hand -> mono -> pose telemetry`

with `OS_INJECTION=DISABLED` visible in the console.
