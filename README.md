# TouchPlus source code / TouchPlus Revival

This repository preserves the historical Ractiv Touch+ source tree **and** hosts an active modern revival of the physical Touch+ stereo sensor.

The historical 2015 application is kept for reference. New work lives under `revival/` and is integrated through the canonical branch **`revival/main`**.

## Current revival status — 2026-08-20

The physical Touch+ is no longer an unknown or dead device. On real hardware we have now validated:

- USB identity `VID_1E4E / PID_0107` and friendly name `Touch+ Camera`;
- Etron vendor control, `SWUnlock(0x0107)`, IMU, exposure/gain and GPIO/LED access;
- persistent stereo capture from the physical device;
- real `1280x480` MJPEG stereo frames split into `640x480` LEFT + `640x480` RIGHT;
- accepted physical cadence of about **30 fps** on the current Windows driver path, despite a negotiated 60 fps mode;
- local per-device stereo calibration for serial **`0101007379`**;
- solved stereo baseline **59.953 mm**, matching the physical ~60–61 mm lens spacing;
- rectification with sub-pixel vertical epipolar error;
- metric depth validated physically at 350/600 mm with only about **+0.82%** distance-scale error;
- hardened live depth probing with catastrophic false matches rejected instead of fabricated;
- a separate working-surface frame with bare-table `H≈0` and a real 53 mm object measuring about 54–55 mm.

Merged revival milestones:

- Phase 0 — hardware probe / atomic capture;
- Phase 1A — stable stereo viewer;
- Phase 1B.1 — factory serial recovery;
- Phase 1B.2a — persistent calibration capture;
- Phase 1B.2b — robust local stereo solver;
- Phase 1C.1 — physical metric-depth validation;
- Phase 1C.2 — live rectified metric depth viewer;
- Phase 2A — working-surface frame calibration.

### Active work: Phase 2B — hand / fingertip 3D

Active PR: **#9** on branch `revival/phase2b-fingertip-3d`.

The current experimental slice is **Phase 2B.7 — SCOPA-inspired palm-core finger branch tracking**. It keeps the accepted camera/depth/surface stack immutable and tries to identify one extended index finger from a learned-background hand silhouette before refining that pixel into metric `(Xsurface, Ysurface, H)`.

Latest physical smoke status: **PARTIAL PASS / FINGERTIP IDENTITY FAIL / DO NOT MERGE**.

What now works well:

- background learning with `B`;
- no-hand rejection after background learning;
- appearance silhouette for low-texture skin;
- physical support bounding;
- palm/branch diagnostics;
- safe ambiguity rejection in some multi-branch frames.

What is still blocking Phase 2B acceptance:

- a single clearly extended index can still be assigned materially different `tip_pixel` candidates between adjacent frames;
- strong stereo support can still produce MEDIUM/HIGH metric confidence after the **wrong 2D anatomical branch** was selected;
- fingertip identity therefore remains untrusted even though the metric stereo stack itself is already validated.

Do **not** merge PR #9 until the physical fingertip identity smoke passes.

## Canonical handoff / resume point

Read this first when resuming the revival:

- [`revival/REVIVAL-ROADMAP.md`](revival/REVIVAL-ROADMAP.md) — canonical project state and next action;
- [`revival/README.md`](revival/README.md) — modern revival architecture and build notes;
- [`revival/notes/phase2b-fingertip-3d.md`](revival/notes/phase2b-fingertip-3d.md) — active tracking boundary;
- active PR **#9** — current code/CI/physical-smoke state.

Never resume from an old downloadable artifact or an expired chat link alone. Verify `revival/main`, the active PR head and the latest GitHub Actions artifact first.

## Revival architecture

```text
Touch+ hardware
      |
      v
Etron unlock / persistent stereo capture
      |
      v
per-device stereo calibration + rectification
      |
      v
metric disparity / depth
      |
      v
working-surface frame  ->  Xsurface / Ysurface / H
      |
      v
hand / fingertip identity
      |
      v
runtime outputs: pointer / touch / gestures / OSC / MIDI / apps
```

Important separation of responsibilities:

- camera calibration (`K/D/R/T/P/Q`) is per physical sensor and remains immutable after acceptance unless a real regression is demonstrated;
- `surface/<serial>.json` is a **per-setup** working-surface transform and must be recalibrated if the Touch+ base or pitch hinge moves;
- tracking is downstream of both layers and must degrade to `unknown` rather than invent a fingertip/touch.

## Modern revival build

Requirements:

- Windows 10/11;
- Visual Studio 2022 Build Tools or newer with Desktop C++ workload;
- CMake 3.23+.

Typical build from a Developer PowerShell:

```powershell
cmake -S revival -B build/revival -A Win32
cmake --build build/revival --config Release
```

The historical Etron control stack is 32-bit, so Win32 is the important physical-device target.

## Repository branches

- `master` — preserved historical fork state;
- `archive/ractiv-2015-12-01` — imported final known Ractiv source baseline;
- `revival/main` — canonical modern revival integration branch;
- `revival/phase2b-fingertip-3d` — current experimental tracking branch / PR #9.

## Historical 2015 build instructions

The original application tree used Visual Studio Community 2015.

1. Install Visual Studio Community 2015.
2. Start Visual Studio with administrator permissions and open `root_dir\track_plus_visual_studio\track_plus.sln`.
3. Press `CTRL+SHIFT+B`.
4. If the legacy build opens `winnt.h` around:

```cpp
typedef void *PVOID;
typedef void * POINTER_64 PVOID64;
```

historical instructions required inserting:

```cpp
#define POINTER_64 __ptr64
typedef void *PVOID;
typedef void * POINTER_64 PVOID64;
```

These instructions are preserved for archaeology only. **Do not use the old application stack as the starting point for TouchPlus Revival.**

## Constraints

- do not reuse the deprecated repeated PowerShell one-shot calibration workflow;
- raw personal calibration/test photos and videos are physical evidence only and should not be committed publicly;
- the old Ractiv factory CDN is gone;
- the device's useful unlocked video state is session-sensitive, so reliable software owns unlock + stream in one controlled process;
- historical Ractiv/Etron redistribution rights must be reviewed before publishing a bundled installer.

The project goal is no longer “can this hardware still work?” That question is answered. The active question is now: **can we turn the validated stereo/depth/surface stack into a reliable fingertip/touch runtime?**
