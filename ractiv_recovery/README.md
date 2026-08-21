# Touch+ Ractiv Recovery

Status: **ACTIVE HISTORICAL RECOVERY EXPERIMENT**

Branch: `ractiv-recovery/main`

Historical base: `master` at `902cb8cc3e660b0ca8d9049fabd383c34da69607` (2015-07-20).

This branch intentionally starts from the last-known integrated July lineage rather than from `revival/main`. The modern Revival work remains preserved separately and is not modified by this recovery experiment.

## Goal

Determine whether the original Ractiv software path can reach a usable Touch+ interaction loop faster than completing the modern contact stack, while preserving the physically validated Revival calibration/runtime as a fallback and likely source of compatibility adapters.

## Current boundary — R0/R1

The first boundary is deliberately conservative:

1. preserve the historical July source unchanged;
2. prove the integrated historical path still exists in source;
3. maintain compatibility changes as separate patches;
4. force the first runtime bring-up into **LOG-ONLY** mode;
5. do not launch `win_cursor_plus`;
6. do not inject Windows touch/mouse input;
7. bypass the dead historical factory-calibration download during the first 2D bring-up;
8. probe the old Win32 build with a current MSVC toolchain and record the exact failures.

The recovery patch is:

`ractiv_recovery/patches/0001-log-only-bringup.patch`

It is intentionally applied to a CI checkout rather than committed over the 2015 source.

## Historical integrated path being recovered

The July `main.cpp` visibly wires:

`Camera(1280x480)`

`-> vertical flip`

`-> LEFT/RIGHT 640x480 split`

`-> MotionProcessorNew`

`-> ForegroundExtractorNew`

`-> HandSplitterNew`

`-> MonoProcessorNew`

`-> PoseEstimator`

`-> HandResolver`

`-> PointerMapper`

`-> UDP to win_cursor_plus`

The historical downstream code also contains plane-distance actuation + release hysteresis, Windows Touch Injection, mouse fallback and TUIO. Those output paths are historical evidence only during R0/R1 and remain disabled for physical smoke testing.

## Why calibration is bypassed in the first run

The original `Reprojector::load()` expects serial-specific Ractiv assets and can attempt a download from the historical CDN when they are absent. That infrastructure is gone.

The first recovery smoke therefore tests only the original hardware/camera/background/hand/mono/pose pipeline. It does **not** claim historical 3D or contact correctness.

A later boundary will add a compatibility adapter from the locally validated Revival calibration to the historical Ractiv geometry path.

## CI

`.github/workflows/ractiv-recovery.yml` contains two jobs.

### Source/safety gate

`ractiv_recovery/source_gate.py`

Checks that the integrated July pipeline is still present, verifies the old contact/output code exists, checks that the LOG_ONLY patch applies cleanly, and verifies that the recovery patch defaults to no OS injection.

### Build archaeology probe

`ractiv_recovery/build_probe.ps1`

Attempts a Win32 build of the historical project using the repository's bundled dependencies and the current MSVC toolset. The original project targets Visual Studio 2013 (`v120`) and also contains historical machine-local paths. Therefore this first build attempt is evidence gathering, not yet a release gate.

The probe uploads:

- `build-probe.log`
- `build-probe-status.json`

A failed historical compile is not hidden: the exact errors define the next compatibility patch.

## Acceptance for first physical Ractiv smoke

Do not run a hardware smoke until a reproducible LOG_ONLY executable has been produced.

When that build exists, the initial physical acceptance sequence is:

1. Touch+ device detected;
2. serial `0101007379` recovered;
3. historical Ractiv camera initializer/unlock succeeds;
4. persistent real 1280x480 stereo frames arrive;
5. LEFT/RIGHT split is correct;
6. background/motion stage stabilizes;
7. hand segmentation responds to a real hand;
8. mono/pose telemetry changes coherently with the hand;
9. `win_cursor_plus` is not launched;
10. no Windows touch/mouse event is injected.

Only after that boundary passes do we reconnect Ractiv 3D / `PointerMapper` through a local-calibration compatibility layer.

## Relationship to Revival

The modern Revival line is paused, not discarded. Its accepted physical facts remain the reference for hardware truth: working Etron unlock, persistent stereo capture, serial recovery, local stereo calibration, metric depth, surface coordinates and physically validated fingertip 3D tracking.

If Ractiv's historical hand/anatomy/contact stack proves useful, the likely end state is hybrid rather than ideological: original Ractiv interaction logic where it works, Revival calibration/runtime/safety where the 2015 infrastructure is dead or weaker.
