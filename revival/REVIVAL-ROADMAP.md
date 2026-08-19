# TouchPlus Revival — canonical roadmap / handoff

Last updated: **2026-08-19 15:45 CEST**

This file is the canonical resume point for the TouchPlus Revival work. If a new ChatGPT/Codex session starts, read this file first, then inspect the active PR/branch named below before changing code.

## 0. Project intent

Revive the abandoned Ractiv Touch+ hardware as a modern stereo/3D input device, without trying to resurrect the complete original 2015 UI stack.

Preferred direction: **Option B — modern runtime around the original hardware**.

Keep/use only what is valuable from Ractiv:

- USB / Etron control and software unlock;
- stereo camera access;
- IMU / accelerometer access;
- sensor controls (exposure, gain, LEDs, etc.);
- geometry/calibration knowledge;
- any useful historical tracking ideas.

Build a modern pipeline on top:

- persistent stereo capture;
- local calibration / rectification;
- disparity and metric depth;
- modern hand / finger / pointer tracking;
- eventually mouse/touch/gesture/OSC/MIDI/Unity style outputs.

Do **not** rewrite the historical snapshot in-place. Revival code lives under `revival/` and the canonical integration branch is `revival/main`.

---

## 1. Canonical Git state

Repository: `shinobione/touch_plus_source_code`

Canonical Revival base branch: `revival/main`

### Completed slices

- **Phase 0 — Hardware Probe / Atomic Capture**: merged into `revival/main`.
  - physical Touch+ detected;
  - vendor control works;
  - software unlock works;
  - IMU works;
  - live video works;
  - stereo 1280x480 frame splits into two real 640x480 eyes.

- **Phase 1A — Stable Stereo Viewer**: merged into `revival/main`.
  - persistent LEFT/RIGHT viewer;
  - GDI flicker fixed with back-buffering;
  - historical Ractiv vertical flip reproduced so images are upright;
  - measured physical cadence documented as ~30 fps.

- **Phase 1B.1 — Factory calibration identity recovery**: merged into `revival/main`.
  - device serial recovered from Touch+ flash;
  - historical cloud key derived;
  - old factory-calibration CDN checked and found unavailable.

### Active slice

**PR #4 — `Phase 1B.2a — guided local stereo calibration capture`**

- PR: `#4`
- base: `revival/main`
- head branch: `revival/phase1b2-local-calibration`
- current known head at handoff: `7864ee02ce5eb84ce927dbbd1fa76f829a3ae856`
- PR is intentionally **Draft** until the physical calibration-capture smoke passes.

Important: the PR description may still contain language from the first PowerShell one-shot approach. The branch code has moved to a **persistent live calibration capture executable**; inspect the branch files, not only the old PR wording.

---

## 2. Physical device facts already proven

Physical unit under test:

- USB identity: `VID_1E4E / PID_0107`
- camera friendly name: `Touch+ Camera`
- sensor type used by Ractiv/Etron layer: `OV7740`
- recovered Ractiv serial: **`0101007379`**
- historical factory cloud key: **`7379`**

### Vendor / control path

Confirmed on the real hardware:

- `EtronDI_Init` works;
- `eSPAEAWB_EnumDevice` sees both the user's UGREEN camera and Touch+;
- `eSPAEAWB_SelectDevice` works;
- `eSPAEAWB_SetSensorType(OV7740)` works;
- `eSPAEAWB_SWUnlock(0x0107)` works;
- accelerometer values are returned and vary physically;
- exposure / gain / GPIO / LED calls succeed;
- the recovered Ractiv legacy initializer succeeds completely.

Critical behavior: the Touch+ video state is effectively session-sensitive. A reliable runtime must **unlock first and open/keep the video stream in the same controlled flow**. Opening/switching cameras through Windows Camera can make Touch+ return to a gray stream until it is unlocked again.

### Stereo video path

Confirmed on the physical device:

- native mode advertises `1280x480 @ 60 fps MJPG`;
- one frame contains two physical camera views side-by-side;
- LEFT = 640x480;
- RIGHT = 640x480;
- the two eyes show real parallax and are not duplicated crops;
- persistent live viewing is stable after the anti-flicker fix;
- historical Ractiv vertical flip is required to make the scene upright while preserving horizontal stereo identity.

### 60 fps investigation — CLOSED FOR NOW

Do not reopen this investigation unless a later phase actually needs more speed.

Measured repeatedly on the real device:

- Media Foundation raw MJPEG path: about **29.9 samples/s**;
- median frame timestamp: about **32 ms**;
- DirectShow `IAMStreamConfig`, forcing `AvgTimePerFrame = 1e7/60`: still about **30.6 callbacks/s**;
- DirectShow + full recovered Ractiv sensor initializer (AE/AWB off, LEDs, exposure 15 ms, gains): still about **30.7 callbacks/s**.

Therefore:

- `60 fps` is advertised / negotiated;
- physical delivery on this unit/firmware/driver path is ~30 fps;
- this is **not** a viewer/GDI/RGB conversion bottleneck.

Baseline for Revival: **stable stereo ~30 fps**.

---

## 3. Factory calibration recovery — result

The real Touch+ flash returned 10 bytes:

- decimal: `0,1,0,1,0,0,7,3,7,9`
- hex: `00 01 00 01 00 00 07 03 07 09`
- Ractiv serial: `0101007379`
- historical cloud key: `7379`

Ractiv historically looked for per-device assets such as:

- `0.jpg`
- `1.jpg`
- `stereoCalibData.txt`
- later-derived rectification data (`rect0.txt`, `rect1.txt`)

The old CloudFront hostname no longer resolves in either HTTP or HTTPS, so **factory calibration recovery is unavailable**. This path is closed.

Canonical fallback: **new local metric stereo calibration of the real unit**.

---

## 4. Calibration target — PHYSICALLY VALIDATED

Target geometry:

- checkerboard: 10 x 7 printed squares;
- OpenCV-style inner corners: **9 x 6**;
- square size: **25.0 mm**;
- printed board size: 250 x 175 mm;
- A4 landscape, printed at Actual Size / 100%.

A PDF version was produced for the user's printer because the printer workflow did not accept SVG directly.

Physical validation already done by user:

- printed 100 mm reference bar measured with a ruler;
- result: **exactly 100 mm**.

Therefore the printed target scale is accepted for metric calibration.

Do not ask the user to reprint the board unless a later solver proves the geometry impossible.

---

## 5. IMPORTANT: first calibration capture workflow is deprecated

The first Phase 1B.2a approach used `touchplus-calibration-capture.ps1` around repeated runs of `touchplus_atomic_probe.exe`.

Physical smoke exposed two problems:

1. the script rejected `-Pairs 3` because it hard-coded a minimum of 8, even though a 3-pair smoke was desired;
2. more importantly, it had no true live stereo preview and repeatedly reopened the Touch+ for one-shot captures; the user observed gray/unusable output.

Treat this as a **workflow design failure**, not a target/camera failure.

Do not continue collecting calibration data with the repeated one-shot PowerShell loop.

---

## 6. Current active implementation: persistent live calibration capture

The active branch now contains a dedicated executable:

`revival/src/calibration_capture.cpp`

Target executable:

`touchplus_calibration_capture.exe`

Design:

1. Etron unlock once;
2. open Touch+ stereo stream once;
3. keep stream open continuously;
4. show persistent live LEFT / RIGHT preview;
5. apply the historical Ractiv vertical flip in the live/captured frame path;
6. press **SPACE** to save the current synchronized stereo frame;
7. reject nearly uniform/gray frames before saving;
8. continue to the next pose without closing the camera;
9. **Q / ESC** quits.

The executable accepts small smoke counts, including:

```powershell
.\touchplus_calibration_capture.exe --pairs 3
```

Expected smoke behavior:

- LEFT and RIGHT live video must be visible before any capture;
- no flicker;
- no gray stream;
- SPACE saves pair 001, 002, 003;
- saved eye PNGs are upright;
- each pose shows the full checkerboard in both eyes.

Expected output layout:

```text
calibration-captures/
  0101007379/
    raw/
      pair-001-full.png
      pair-001-left.png
      pair-001-right.png
      pair-001.json
      pair-002-...
      pair-003-...
```

### Packaging / artifact rule

**Do not rely on ChatGPT `sandbox:/...` links across chat sessions.** Those links can expire with the code-interpreter/session runtime.

Instead, in a new chat:

1. inspect PR #4 / branch `revival/phase1b2-local-calibration`;
2. inspect the latest successful `Revival Calibration Capture Kit` GitHub Actions run for that branch/head;
3. download the GitHub Actions artifact named:
   - `touchplus-phase1b2-calibration-capture-kit`
4. verify that the artifact contains `touchplus_calibration_capture.exe` plus the Etron DLLs and calibration target/docs;
5. give the user a fresh downloadable artifact link/file if needed.

At the time of this handoff, the latest known packaging CI for the persistent live capture executable had passed successfully.

---

## 7. NEXT ACTION — exact continuation point

This is the immediate canonical action in the next chat.

### Step A — refresh PR #4 / artifact

Read:

- this roadmap;
- PR #4;
- current head of `revival/phase1b2-local-calibration`;
- latest GitHub Actions runs/artifacts for that head.

If necessary, update the PR description so it no longer presents the deprecated repeated one-shot PowerShell loop as the preferred workflow.

### Step B — give user the current live-capture kit

Use the latest successful GitHub Actions artifact, **not an old sandbox link**.

### Step C — physical 3-pair smoke

User runs:

```powershell
.\touchplus_calibration_capture.exe --pairs 3
```

Acceptance:

- persistent LEFT/RIGHT live preview appears;
- both eyes show non-gray real video;
- checkerboard can be positioned while preview remains live;
- three captures save successfully with SPACE;
- no camera close/reopen between pairs;
- six eye PNGs (`3 x left/right`) are visually usable.

Suggested smoke poses:

1. board front-facing and centered;
2. board moderately yawed left;
3. board moderately yawed right.

Keep the complete checkerboard visible in both eyes.

### Step D — inspect the 3-pair dataset

User should upload either:

- the six eye PNGs, or
- a ZIP of the 3-pair dataset.

Check:

- focus / blur;
- exposure / contrast;
- full checkerboard visibility in both eyes;
- non-duplicate poses;
- real left/right parallax;
- no gray frames;
- orientation consistency.

If the 3-pair smoke passes, collect **18–25 diverse clean pairs** using the same persistent session.

---

## 8. Phase 1B.2b — solver (next code slice after dataset)

Once a clean dataset exists, build the local calibration solver.

Input:

- LEFT/RIGHT 640x480 synchronized pairs;
- checkerboard 9x6 inner corners;
- square size 25.0 mm;
- already-upright persistent-capture images.

Required outputs:

- camera matrix `K1` / `K2`;
- distortion coefficients `D1` / `D2`;
- stereo rotation `R`;
- stereo translation `T` in millimetres;
- essential/fundamental matrices if useful (`E`, `F`);
- rectification transforms `R1`, `R2`;
- projection matrices `P1`, `P2`;
- disparity-to-depth reprojection matrix `Q`;
- per-pair corner detection result;
- mono reprojection RMS;
- stereo calibration RMS;
- rectified epipolar vertical-error statistics;
- rectified preview images with horizontal guide lines;
- machine-readable calibration bundle versioned by device serial `0101007379`.

Acceptance before runtime integration:

- corner detection succeeds on most/all selected pairs;
- no gross outlier poses;
- reprojection error is reasonable and stable;
- rectified left/right features align horizontally;
- computed baseline / geometry is physically plausible;
- a simple disparity/depth demo gives coherent near/far behavior.

Do not install calibration into the runtime solely because OpenCV returns a matrix; review metrics and previews first.

---

## 9. Phase 1C — rectified stereo + metric depth

After solver acceptance:

- load calibration by serial;
- rectify live LEFT/RIGHT frames;
- implement/test disparity (start with OpenCV StereoSGBM or equivalent); 
- convert disparity to metric depth via `Q`;
- add live diagnostic modes:
  - raw stereo;
  - rectified stereo;
  - disparity;
  - depth heat/gray visualization;
  - point-depth readout under cursor;
- verify known-distance objects with a tape/ruler.

Goal: stable metric Z estimates in the useful Touch+ working volume.

---

## 10. Phase 2 — modern tracking core

Only after stereo geometry is trustworthy.

Incremental targets:

1. foreground / working-surface mask;
2. hand region detection;
3. finger/index candidate extraction;
4. left/right correspondence using rectified geometry;
5. triangulated 3D fingertip position;
6. temporal smoothing / confidence;
7. touch-plane calibration;
8. pointer/touch state (`hover`, `touch/down`, `release`).

Do not begin with a giant AI model unless geometry proves insufficient. First exploit the stereo hardware cleanly.

---

## 11. Phase 3 — useful runtime outputs

Potential outputs once 3D tracking is stable:

- Windows mouse pointer;
- virtual touch / click plane;
- pinch / gesture controls;
- OSC;
- MIDI;
- Unity/Unreal bridge;
- custom app integrations;
- debug telemetry / recording.

The original Ractiv code already contained pointer mapping, UDP IPC, pinch logic and a Unity-style tool mode, so those ideas can be reused conceptually without reviving the old UI wholesale.

---

## 12. Known technical constraints

- recovered Etron DLL stack is Win32 / 32-bit;
- modern Revival executables that link the old vendor-control SDK therefore use Win32 builds;
- Etron may initialize COM internally, so COM apartment conflicts must be handled carefully (Phase 0C already introduced compatibility handling where needed);
- Windows Camera is useful only as a diagnostic; do not make it part of the runtime;
- the Touch+ must be software-unlocked before useful video;
- switching/reopening camera clients can return the device to gray state;
- advertised `@60` does not mean measured 60 fps on the real hardware;
- current accepted physical baseline is ~30 fps stereo.

---

## 13. Licensing / redistribution caution

The historical Ractiv code uses the Aladdin Free Public License v9 and the Etron binaries may have separate redistribution constraints.

Personal revival / experimentation is the current goal. Before publishing a public installer that bundles historical proprietary DLLs, review redistribution rights separately.

---

## 14. One-line handoff for a new chat

Use this if the next session needs a concise command:

> `@GitHub Reprends TouchPlus Revival depuis revival/REVIVAL-ROADMAP.md. Vérifie revival/main, puis la PR #4 / branche revival/phase1b2-local-calibration et le dernier artifact GitHub Actions. Ne réutilise pas l'ancien workflow PowerShell one-shot de calibration. La prochaine action canonique est le smoke physique de touchplus_calibration_capture.exe --pairs 3 avec preview LEFT/RIGHT persistante.`
