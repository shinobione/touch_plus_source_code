# TouchPlus Revival — canonical roadmap / handoff

Last updated: **2026-08-19 16:55 CEST**

This file is the canonical resume point for TouchPlus Revival. Read it first in a new ChatGPT/Codex session, then inspect the active PR/branch before changing code.

## 0. Project intent

Revive the abandoned Ractiv Touch+ hardware as a modern stereo/3D input device without trying to resurrect the complete original 2015 UI stack.

Preferred direction: **Option B — modern runtime around the original hardware**.

Keep/use what is valuable from Ractiv:

- USB / Etron control and software unlock;
- stereo camera access;
- IMU / accelerometer access;
- exposure, gain and LEDs;
- calibration / geometry knowledge;
- useful historical tracking ideas.

Build a modern pipeline on top:

- persistent stereo capture;
- local metric calibration / rectification;
- disparity and metric depth;
- modern hand / finger / pointer tracking;
- mouse/touch/gesture/OSC/MIDI/Unity-style outputs.

Do not rewrite the historical snapshot in-place. Revival code lives under `revival/`; the canonical integration branch is `revival/main`.

---

## 1. Canonical Git state

Repository: `shinobione/touch_plus_source_code`

Canonical Revival base branch: `revival/main`

### Completed slices

- **Phase 0 — Hardware Probe / Atomic Capture**: merged.
  - physical Touch+ detected;
  - vendor control + software unlock work;
  - IMU works;
  - live stereo video works;
  - 1280x480 frame splits into two real 640x480 eyes.

- **Phase 1A — Stable Stereo Viewer**: merged.
  - persistent LEFT/RIGHT viewer;
  - GDI flicker fixed with back-buffering;
  - historical Ractiv vertical flip reproduced;
  - measured physical cadence documented as ~30 fps.

- **Phase 1B.1 — Factory calibration identity recovery**: merged.
  - device serial recovered from flash;
  - historical cloud key derived;
  - old factory-calibration CDN confirmed unavailable.

- **Phase 1B.2a — Persistent live stereo calibration capture**: merged via PR #4 on **2026-08-19**.
  - accepted implementation: `touchplus_calibration_capture.exe`;
  - one Etron unlock, one persistent stream;
  - persistent LEFT/RIGHT preview;
  - SPACE saves synchronized pairs without reopening the camera;
  - gray/uniform guard;
  - captured PNGs already use canonical upright orientation;
  - physical 3-pair smoke PASS;
  - follow-up 20-pose dataset captured successfully.

### Active slice

**Phase 1B.2b — local metric calibration solver**

- branch: `revival/phase1b2b-calibration-solver`
- solver: `revival/tools/touchplus-calibration-solver.py`
- notes: `revival/notes/phase1b2b-calibration-solver.md`
- CI: `.github/workflows/revival-calibration-solver.yml`
- expected PR: **#5** once opened.

The active goal is to make the successful calibration reproducible and auditable, not merely keep matrices produced inside one chat session.

---

## 2. Physical device facts already proven

Physical unit under test:

- USB identity: `VID_1E4E / PID_0107`
- camera friendly name: `Touch+ Camera`
- historical sensor type: `OV7740`
- recovered Ractiv serial: **`0101007379`**
- historical factory cloud key: **`7379`**

Confirmed vendor/control behavior:

- `EtronDI_Init` works;
- `eSPAEAWB_EnumDevice` sees Touch+;
- `eSPAEAWB_SelectDevice` works;
- `eSPAEAWB_SetSensorType(OV7740)` works;
- `eSPAEAWB_SWUnlock(0x0107)` works;
- accelerometer values are real and change physically;
- exposure / gain / GPIO / LED calls succeed;
- recovered Ractiv LegacyInit succeeds completely.

Critical runtime behavior: video state is session-sensitive. Reliable code must **unlock first and keep the stream open in the same controlled flow**. Reopening/switching camera clients can return the Touch+ to a gray stream until it is unlocked again.

Stereo video facts:

- native mode advertises `1280x480 @ 60 fps MJPG`;
- LEFT = 640x480;
- RIGHT = 640x480;
- two real physical views with parallax;
- persistent live viewing is stable;
- historical vertical flip is required for canonical upright orientation.

### 60 fps investigation — CLOSED FOR NOW

Measured on the physical device:

- Media Foundation raw MJPEG: ~29.9 samples/s;
- median timestamp interval: ~32 ms;
- DirectShow forced to 60: ~30.6 callbacks/s;
- DirectShow + full recovered sensor initializer: ~30.7 callbacks/s.

Therefore `60 fps` is advertised/negotiated but physical delivery on this unit/driver path is ~30 fps. Accepted Revival baseline: **stable stereo ~30 fps**.

---

## 3. Factory calibration recovery — CLOSED

Flash identity:

- raw decimal: `0,1,0,1,0,0,7,3,7,9`
- raw hex: `00 01 00 01 00 00 07 03 07 09`
- Ractiv serial: `0101007379`
- cloud key: `7379`

Historical assets (`0.jpg`, `1.jpg`, `stereoCalibData.txt`, later `rect0.txt` / `rect1.txt`) are no longer recoverable because the old CloudFront hostname is dead.

Canonical fallback is the new local metric stereo calibration.

---

## 4. Calibration target — PHYSICALLY VALIDATED

Target geometry:

- 10 x 7 printed squares;
- **9 x 6 inner corners**;
- square size: **25.0 mm**;
- board size: 250 x 175 mm;
- A4 landscape at Actual Size / 100%.

The printed 100 mm reference bar was physically measured by the user and is **exactly 100 mm**. Metric target scale is accepted.

Do not ask for a reprint unless a later physical depth check proves the scale wrong.

---

## 5. Deprecated calibration capture path — DO NOT REUSE

The original helper `touchplus-calibration-capture.ps1` around repeated one-shot `touchplus_atomic_probe.exe` runs is deprecated.

Physical smoke showed:

1. an incorrect minimum-8 guard blocked a 3-pair smoke;
2. no real live preview;
3. repeated camera open/close produced gray/unusable output.

This was a workflow design failure, not a target/camera failure.

Never return to the repeated PowerShell one-shot loop for calibration collection.

---

## 6. Phase 1B.2a — accepted persistent capture

Canonical capture executable:

`touchplus_calibration_capture.exe`

Behavior:

1. Etron unlock once;
2. open Touch+ stereo stream once;
3. keep stream open continuously;
4. show persistent LEFT / RIGHT preview;
5. apply historical Ractiv vertical flip;
6. SPACE saves the current synchronized stereo frame;
7. reject nearly uniform/gray frames;
8. continue without camera reopen;
9. Q / ESC quits.

### Physical 3-pair smoke — PASS

Real device `0101007379` produced three synchronized poses. All six LEFT/RIGHT images:

- were non-gray and usable;
- had the complete useful 9x6 inner-corner grid;
- produced **54/54 detected corners per eye**;
- showed real stereo parallax;
- produced a coherent smoke-scale baseline around 60 mm and sub-pixel rectified vertical error.

PR #4 was then accepted and merged.

### Full dataset — CAPTURED

A follow-up **20-pose** dataset was captured in one persistent session.

- 20 synchronized stereo pairs;
- 40 eye images;
- all **20/20 pairs** yield complete 9x6 corner detection in both eyes.

The source dataset remains the evidence set. Do not silently delete difficult poses.

---

## 7. Phase 1B.2b — solver implementation and real result

Canonical solver:

```text
revival/tools/touchplus-calibration-solver.py
```

Input can be either the capture directory or a ZIP of the captured dataset.

Solver outputs:

- `K1`, `D1`, `K2`, `D2`;
- stereo `R` and metric `T_mm`;
- `E`, `F`;
- `R1`, `R2`;
- `P1`, `P2`;
- `Q`;
- per-pair detection and reprojection diagnostics;
- robust outlier selection report;
- rectified preview composites with horizontal guide lines;
- JSON calibration bundle;
- OpenCV YAML bundle;
- solved ZIP archive.

### Robust outlier policy

The solver first calibrates all complete detected pairs, then scores each pair from combined LEFT/RIGHT mono reprojection RMSE.

Gross outlier threshold:

`median + 2.5 × robust MAD sigma`

Outliers are documented and excluded from the final solve; they are **not deleted** from the original dataset.

### Real 20-pose solve — PASS

On the user's physical 20-pose dataset:

- input pairs: **20**
- full corner-detected pairs: **20**
- accepted final pairs: **17**
- robust outliers: **016, 017, 018**
- robust threshold: **1.0305 px**

Final accepted-set metrics:

- mono RMS LEFT: **0.3472 px**
- mono RMS RIGHT: **0.3912 px**
- stereo RMS: **0.3799 px**
- recovered stereo baseline: **59.953 mm**
- rectified vertical epipolar mean: **0.1195 px**
- rectified vertical epipolar p95: **0.3096 px**
- rectified vertical epipolar max: **1.2763 px**

Numeric solver acceptance: **PASS**.

Important: numeric PASS is not by itself permission to install calibration into the live runtime.

---

## 8. NEXT ACTION — exact continuation point

### Step A — finish Phase 1B.2b PR / CI

Inspect the active solver branch and PR #5:

- verify solver CI (`py_compile` + dependency import + CLI smoke) is green;
- inspect changed files;
- keep the real 20-pose result documented;
- do not commit the user's raw 20-pose photo dataset into the public repo.

### Step B — visually accept rectification

Use the solver-produced rectified previews from the user's real dataset.

Acceptance:

- matching checkerboard corners/features in LEFT/RIGHT align horizontally;
- no gross warping or eye swap;
- no orientation reversal;
- useful image field remains after rectification.

### Step C — first metric depth validation

Before runtime integration, build/run a small disparity/depth diagnostic using the accepted bundle:

1. rectify a synchronized pair;
2. compute disparity (StereoSGBM is acceptable as first baseline);
3. reproject with `Q`;
4. verify nearer objects produce smaller Z than farther objects;
5. measure a few known distances with ruler/tape and compare estimated Z.

Only after physical depth sanity passes should the calibration be promoted into the runtime.

---

## 9. Phase 1C — rectified stereo + metric depth

After calibration acceptance:

- load calibration by serial `0101007379`;
- rectify live LEFT/RIGHT frames;
- implement/test disparity;
- convert disparity to metric depth via `Q`;
- add diagnostics:
  - raw stereo;
  - rectified stereo;
  - disparity;
  - depth visualization;
  - point-depth readout under cursor;
- verify known-distance objects physically.

Goal: stable metric Z estimates in the useful Touch+ working volume.

---

## 10. Phase 2 — modern tracking core

Only after stereo geometry is physically trustworthy.

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

Potential outputs:

- Windows mouse pointer;
- virtual touch / click plane;
- pinch / gesture controls;
- OSC;
- MIDI;
- Unity/Unreal bridge;
- custom app integrations;
- debug telemetry / recording.

Historical Ractiv code already contained pointer mapping, UDP IPC, pinch logic and a Unity-style tool mode; these can be reused conceptually without reviving the old UI wholesale.

---

## 12. Known technical constraints

- historical Etron DLL stack is Win32 / 32-bit;
- Revival executables that link the vendor-control SDK use Win32 builds;
- Etron can initialize COM internally, so apartment conflicts need compatibility handling;
- Windows Camera is diagnostic only, never part of runtime;
- software unlock is required before useful video;
- switching/reopening clients can return a gray stream;
- advertised 60 fps is not measured physical 60 fps on the real unit;
- accepted physical baseline is ~30 fps stereo;
- raw personal calibration photos should not be committed to the public repository.

---

## 13. Licensing / redistribution caution

Historical Ractiv code uses the Aladdin Free Public License v9 and Etron binaries may have separate redistribution constraints.

Personal revival / experimentation is the current goal. Before publishing a public installer bundling historical proprietary DLLs, review redistribution rights separately.

---

## 14. One-line handoff for a new chat

> `@GitHub Reprends TouchPlus Revival depuis revival/REVIVAL-ROADMAP.md. Phase 1B.2a est mergée. Vérifie la PR #5 / branche revival/phase1b2b-calibration-solver et sa CI. Le dataset physique 20 poses a 20/20 détections; le solveur robuste garde 17 paires, exclut 016/017/018, sort 0.3799 px stereo RMS, baseline 59.953 mm et epipolar p95 0.3096 px. Prochaine boundary: accepter visuellement les rectifications puis faire un diagnostic disparity/depth métrique avant intégration runtime.`
