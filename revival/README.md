# TouchPlus Revival

This directory is the modern revival path for the Ractiv Touch+ hardware.

The goal is **not** to recreate the abandoned 2015 desktop application. The Touch+ is treated as a reusable stereo/IMU sensor and a new runtime is being built around its validated hardware-specific behavior.

Canonical integration branch: **`revival/main`**.

Canonical resume point: **`revival/REVIVAL-ROADMAP.md`**.

## Current physical status — 2026-08-20

Physical unit: **serial `0101007379`**.

The hardware, stereo geometry, metric depth and working-surface frame are all physically validated. The active blocker is now **fingertip identity**, not camera access or metric reconstruction.

### Merged / accepted

- **Phase 0 — Hardware Probe / Atomic Capture**
  - `VID_1E4E / PID_0107`, friendly name `Touch+ Camera`;
  - Etron init/select, `SWUnlock(0x0107)`, IMU, exposure/gain and GPIO/LED access;
  - persistent real stereo capture;
  - combined `1280x480` frame split into `640x480` LEFT + RIGHT.

- **Phase 1A — Stable Stereo Viewer**
  - persistent live viewer;
  - historical Ractiv vertical flip reproduced;
  - GDI flicker fixed;
  - 60 fps mode is advertised/negotiated, but this hardware/driver path physically delivers about **30 fps**.

- **Phase 1B.1 — Factory identity recovery**
  - serial `0101007379` recovered from device flash;
  - historical cloud key `7379` derived;
  - factory CDN confirmed unavailable.

- **Phase 1B.2a — Persistent local calibration capture**
  - persistent Win32 capture tool;
  - 20 synchronized physical checkerboard pairs captured.

- **Phase 1B.2b — Robust local stereo calibration solver**
  - 17 robust final inlier pairs;
  - baseline **59.953 mm**;
  - mono RMS LEFT **0.3472 px**, RIGHT **0.3912 px**;
  - stereo RMS **0.3799 px**;
  - rectified vertical epipolar mean **0.1195 px**, p95 **0.3096 px**, max **1.2763 px**.

- **Phase 1C — Metric depth / live depth**
  - physical 350/600 mm delta preserved within about **+0.82%**;
  - hardened point matcher uses texture gate, NCC, uniqueness, LEFT↔RIGHT consistency, local consensus and temporal rejection;
  - `P` locked probe reports valid %, median Z, MAD and confidence;
  - a fixed planar target produced `R²≈0.9984`, RMS residual `≈0.41 mm`, max residual `≈0.57 mm`;
  - low-texture points are rejected rather than fabricated.

- **Phase 2A — Working-surface frame**
  - camera calibration remains immutable;
  - per-setup surface transform is stored separately as `surface/<serial>.json`;
  - accepted physical fit: 19 samples / 17 inliers, RMS **0.924 mm**, max residual **2.021 mm**, coverage **403.9 × 356.9 mm**;
  - bare-table H checks remain around zero (`-0.9` to `+2.6 mm` across seven valid locations);
  - a rigid 53 mm object measured **54.1 mm** and **55.3 mm**.

## Active: Phase 2B — hand / fingertip 3D

Active branch: `revival/phase2b-fingertip-3d`.

Active PR: **#9**, Draft / **DO NOT MERGE**.

Current experimental slice: **Phase 2B.7 — SCOPA-inspired palm-core finger branch tracking**.

### Phase 2B progression

- 2B.1 proved runtime wiring but produced giant false hand components;
- 2B.2 hardened segmentation and removed the 20k-cell foreground failure;
- 2B.3 geodesic identity still confused wrist/scene endpoints;
- 2B.4 learned background dramatically improved no-hand rejection;
- 2B.5 decoupled 2D appearance silhouette from dense-depth availability so low-texture distal skin remains visible;
- 2B.6 support-bounded skeleton added safer ambiguity handling but still emitted anatomically wrong HIGH-confidence points;
- 2B.7 added a SCOPA-inspired palm core and finger-branch model.

Latest physical 2B.7 smoke: **PARTIAL PASS / FINGERTIP IDENTITY FAIL**.

The useful wins are now stable:

- background learning works;
- empty learned scenes can remain no-hand;
- the hand silhouette is much cleaner than early Phase 2B attempts;
- physical support bounding rejects long appearance-only tails;
- palm/branch telemetry exposes what the identity stage is doing;
- multi-branch ambiguity can fail safe.

The remaining blocker is precise:

> A single clearly extended index can still be assigned different `tip_pixel` candidates on adjacent frames, and the robust stereo matcher can then report MEDIUM/HIGH confidence for the **wrong anatomical pixel**.

Metric confidence and identity confidence are therefore not yet the same thing.

Detailed note:

- `revival/notes/phase2b-fingertip-3d.md`
- `revival/notes/phase2b7-physical-smoke-2026-08-20.md` on the active PR branch.

### Next identity boundary

Do not loosen the metric matcher. Preserve the accepted camera/depth/surface/background layers and strengthen only the 2D identity stage:

- validate palm core before branch scoring;
- keep short temporal palm persistence;
- persist branch identity instead of re-electing a fingertip every frame;
- require finger-like geometry leaving the palm boundary;
- reject unexplained 2D fingertip jumps;
- separate **identity confidence** from **stereo refinement confidence**;
- return `unknown` whenever identity is unstable;
- compare the controlled geometry path against a lightweight modern 2D hand-landmark fallback before adding more endpoint heuristics.

Temporal smoothing may stabilize a correct candidate, but must never hide a wrong branch choice.

## Architecture

```text
Touch+ hardware
      |
      v
[ Sensor Layer ]  Etron unlock + UVC/DirectShow capture + IMU
      |
      v
[ Stereo Layer ]  per-device calibration + rectification + disparity/Q
      |
      v
[ Surface Layer ] working-surface transform -> Xsurface / Ysurface / H
      |
      v
[ Identity Layer ] background + appearance silhouette + hand/palm/finger identity
      |
      v
[ Metric Tracking ] robust LEFT<->RIGHT refinement + smoothing/confidence
      |
      v
[ Runtime API ] pointer / touch / gestures / OSC / MIDI / apps
```

The legacy Ractiv algorithms remain valuable as hardware documentation and as a source of algorithmic ideas. They are not imported wholesale into the modern runtime.

## Important physical facts

- USB VID/PID: `1E4E:0107`;
- sensor family recovered from original code: `OV7740`;
- useful video state is session-sensitive;
- reliable software must unlock and keep the stream open in one controlled process;
- canonical orientation uses the historical vertical flip;
- accepted real capture baseline is ~30 fps;
- moving the Touch+ base or its mechanical pitch hinge requires **surface-frame recalibration**, not stereo recalibration.

## Build

Requirements:

- Windows 10/11;
- Visual Studio 2022 Build Tools or newer with Desktop C++ workload;
- CMake 3.23+.

Physical-device Win32 build:

```powershell
cmake -S revival -B build/revival-x86 -A Win32
cmake --build build/revival-x86 --config Release
```

The historical Etron vendor stack is 32-bit, so **Win32 is the physical-device target that matters**.

x64 is still useful for pure math/self-tests where hardware DLLs are not required.

## Runtime artifacts / calibration separation

Per-device camera calibration:

```text
calibration/<serial>.json
```

Per-setup working-surface transform:

```text
surface/<serial>.json
```

Do not copy one over the other simply because both use the same serial filename.

`K/D/R/T/P/Q` are camera/stereo calibration. The surface JSON is a separate camera-to-work-plane transform.

## Runtime controls currently layered into the depth/tracking viewer

Accepted lower-layer controls include:

- `D` — depth view;
- `S` — rectified stereo view;
- `P` — locked metric depth probe;
- `C` — capture one surface calibration point;
- `F` — fit/save surface frame;
- `R` — reset pending surface points;
- `U` — undo last pending surface point;
- `H` — print surface-relative coordinates/height;
- `Q` / `ESC` — quit.

Phase 2B adds:

- `B` — learn/relearn clean background;
- `T` — tracking ON/OFF.

## Repository branches

- `master` — historical fork preserved;
- `archive/ractiv-2015-12-01` — final known Ractiv code snapshot imported as archive;
- `revival/main` — canonical modern integration branch;
- `revival/phase2b-fingertip-3d` — active experimental tracking branch / PR #9.

## Rules for the revival

- never reuse the deprecated repeated PowerShell one-shot calibration workflow;
- raw personal calibration/test images and videos are evidence only and must not be committed publicly;
- do not reopen the 60 fps investigation unless functionality genuinely requires it;
- do not mutate accepted camera calibration to compensate for setup/surface changes;
- low confidence must become `unknown`, never a fake finite fingertip/touch;
- do not merge Phase 2B from synthetic CI alone — physical fingertip identity is the gate;
- verify GitHub state and latest Actions artifacts when resuming; do not rely on old chat download links.

## Historical reference

The old Ractiv application can still be studied for its camera control, calibration, SCOPA/HandResolver logic and runtime intent. The modern revival deliberately avoids rebuilding the complete 2015 UI/process stack.

The project has already passed the hardware viability question. The current engineering problem is now narrow and measurable: **turn a validated stereo/depth/surface system into a reliably identified fingertip before touch semantics are attempted.**
