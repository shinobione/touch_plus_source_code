# TouchPlus Revival — canonical roadmap / handoff

Last updated: **2026-08-19 17:08 CEST**

This file is the canonical resume point for TouchPlus Revival. Read it first in a new ChatGPT/Codex session, then inspect the active PR/branch before changing code.

## 0. Project intent

Revive the abandoned Ractiv Touch+ as a modern stereo/3D input device. Do not resurrect the complete old 2015 UI stack. Revival code lives under `revival/`; canonical integration branch is `revival/main`.

Target pipeline:

`Etron/USB unlock → persistent stereo capture → local metric calibration → rectification → disparity/depth → 3D tracking → useful runtime outputs`.

---

## 1. Canonical Git state

Repository: `shinobione/touch_plus_source_code`

Canonical base: `revival/main`

### Completed / merged

- **Phase 0 — Hardware Probe / Atomic Capture**
  - physical device detected;
  - Etron vendor control + SWUnlock work;
  - IMU works;
  - real 1280x480 stereo frame splits into two 640x480 eyes.

- **Phase 1A — Stable Stereo Viewer**
  - persistent LEFT/RIGHT live viewer;
  - vertical Ractiv flip reproduced;
  - GDI flicker fixed;
  - physical delivery measured ~30 fps despite advertised 60.

- **Phase 1B.1 — Factory calibration identity recovery**
  - serial `0101007379` recovered from flash;
  - historical cloud key `7379` derived;
  - old factory CDN confirmed dead.

- **Phase 1B.2a — Persistent live calibration capture** — PR #4 merged at `45a1eda7dfaf8f5adafcf556b69b9ad1b8dabcb5`.
  - canonical capture executable: `touchplus_calibration_capture.exe`;
  - one unlock + one persistent stream;
  - LEFT/RIGHT preview remains live;
  - SPACE saves synchronized pairs without reopen;
  - gray/uniform guard;
  - physical 3-pair smoke PASS;
  - 20-pose physical dataset subsequently captured.

- **Phase 1B.2b — Robust local metric calibration solver** — PR #5 merged at `b04d703499d1af8cea14416c2eec4fb5420868bd`.
  - reproducible Python solver;
  - robust outlier filtering;
  - rectification diagnostics;
  - JSON/YAML calibration bundle outputs;
  - solver CI green;
  - real 20-pose numeric solve PASS.

### Active slice

**Phase 1C — candidate calibration + physical metric depth validation**

- branch: `revival/phase1c-depth-validation`
- expected PR: **#6**
- candidate calibration: `revival/calibration/candidates/0101007379.json`
- offline depth diagnostic: `revival/tools/touchplus-depth-sanity.py`
- notes: `revival/notes/phase1c-depth-validation.md`
- CI: `.github/workflows/revival-depth-sanity.yml`

The candidate is explicitly guarded as `candidate_pending_physical_depth_validation`. Do not promote it to the live runtime until known-distance depth sanity passes.

---

## 2. Physical device facts

Physical unit:

- USB: `VID_1E4E / PID_0107`
- friendly name: `Touch+ Camera`
- historical sensor: `OV7740`
- serial: **`0101007379`**
- historical cloud key: **`7379`**

Confirmed control path:

- Etron init/enumeration/select works;
- `eSPAEAWB_SetSensorType(OV7740)` works;
- `eSPAEAWB_SWUnlock(0x0107)` works;
- IMU/accelerometer works;
- exposure/gain/GPIO/LED calls work;
- recovered Ractiv LegacyInit works.

Critical behavior: Touch+ video is session-sensitive. Reliable software must unlock then keep the video stream open in one controlled flow. Reopening/switching clients can return a gray stream.

Stereo facts:

- advertised native mode: `1280x480 @ 60 fps MJPG`;
- LEFT / RIGHT: 640x480 each;
- real stereo parallax confirmed;
- canonical orientation requires historical vertical flip;
- accepted physical cadence: ~30 fps.

Do not reopen the 60-fps investigation unless a later phase truly needs it.

---

## 3. Calibration target / capture evidence

Metric target:

- 10x7 printed squares;
- 9x6 inner corners;
- 25.0 mm squares;
- 250x175 mm board;
- A4 landscape at 100% Actual Size.

The physical 100 mm reference bar measured exactly 100 mm.

The deprecated repeated PowerShell one-shot capture loop must never be reused. The accepted capture is `touchplus_calibration_capture.exe`, which keeps one persistent stereo session open.

### Physical dataset

- 20 synchronized stereo pairs captured;
- 40 eye PNGs;
- **20/20 pairs** produce complete 54/54 checkerboard corner detection in both eyes.

Raw user photos are evidence and should not be committed to the public repo.

---

## 4. Phase 1B.2b real solver result — PASS

Robust policy:

`combined LEFT/RIGHT mono reprojection RMSE → median + 2.5 × robust MAD sigma`

Physical 20-pose result:

- input: **20**
- complete detected pairs: **20**
- accepted final pairs: **17**
- excluded gross outliers: **016, 017, 018**
- threshold: **1.0305 px**

Final metrics:

- mono RMS LEFT: **0.3472 px**
- mono RMS RIGHT: **0.3912 px**
- stereo RMS: **0.3799 px**
- stereo baseline: **59.953 mm**
- rectified vertical epipolar mean: **0.1195 px**
- p95: **0.3096 px**
- max: **1.2763 px**

Rectified contact-sheet review PASS: matching checkerboard features align horizontally, no eye swap, no orientation regression.

The checkerboard also gives coherent sparse triangulation / near-far ordering. However, its repeating texture is a poor final target for **dense** StereoSGBM because repeated squares can cause disparity phase ambiguity. Physical dense-depth validation must therefore use a textured non-repeating real scene.

---

## 5. Phase 1C implementation

### Candidate calibration

`revival/calibration/candidates/0101007379.json`

Contains the accepted Phase 1B.2b matrices and quality metrics:

- `K1`, `D1`, `K2`, `D2`;
- stereo `R`, `T_mm`;
- `E`, `F`;
- `R1`, `R2`;
- `P1`, `P2`;
- `Q`;
- ROIs;
- accepted/rejected source pair numbers.

Promotion state remains:

`candidate_pending_physical_depth_validation`

### Offline metric-depth diagnostic

`revival/tools/touchplus-depth-sanity.py`

Inputs:

- candidate JSON;
- one synchronized LEFT image;
- one synchronized RIGHT image.

It:

1. rectifies both eyes;
2. computes StereoSGBM disparity;
3. reprojects disparity through `Q` into millimetres;
4. writes rectified images, disparity, depth visualization and NumPy depth/disparity arrays;
5. supports known-distance point samples;
6. can gate absolute percent error when a threshold is supplied.

This remains an offline diagnostic. Live runtime integration is intentionally blocked until physical validation passes.

---

## 6. NEXT ACTION — physical metric depth smoke

Use a **textured non-repeating scene**, not the checkerboard.

Suggested setup:

- near textured object front face at roughly **300–400 mm** from the Touch+ lens/front plane;
- far textured object front face at roughly **600–800 mm**;
- both visible in LEFT and RIGHT;
- avoid shiny surfaces and large blank areas;
- measure each distance with a ruler/tape to about 5–10 mm accuracy.

Capture one synchronized pair using the accepted persistent executable, keeping this test separate from calibration data:

```powershell
.\touchplus_calibration_capture.exe --pairs 1 --output .\depth-validation\raw
```

The capture executable does **not** require a checkerboard to save; it simply saves synchronized full/LEFT/RIGHT frames when SPACE is pressed.

Upload/ZIP the resulting `depth-validation` folder and report the measured near/far distances.

Then run/review `touchplus-depth-sanity.py` on the pair.

### Acceptance boundary

Advance toward live Phase 1C integration only if:

- rectified orientation/alignment remains correct;
- near object yields smaller Z than far object;
- useful textured regions have coherent disparity;
- metric scale is plausible against tape/ruler distances;
- there is no gross sign, scale, eye-order or Q failure.

Do not promise final accuracy yet. First establish physically coherent metric depth, then characterize error over the intended working volume.

---

## 7. After physical depth PASS

Next Phase 1C runtime work:

- load calibration by serial;
- rectify live LEFT/RIGHT frames;
- disparity diagnostic mode;
- metric depth via `Q`;
- depth visualization;
- point-depth readout;
- characterize known-distance error over the useful Touch+ volume.

Only after geometry/depth is trustworthy should Phase 2 begin hand/finger/touch-plane tracking.

---

## 8. Later phases

### Phase 2 — modern tracking core

1. foreground / working-surface mask;
2. hand region detection;
3. finger/index candidate extraction;
4. stereo correspondence;
5. triangulated 3D fingertip;
6. smoothing/confidence;
7. touch-plane calibration;
8. hover/touch/release states.

### Phase 3 — outputs

- Windows pointer/touch;
- gestures/pinch;
- OSC/MIDI;
- Unity/Unreal bridge;
- custom integrations.

---

## 9. Constraints / cautions

- old Etron stack is Win32/32-bit;
- Etron COM initialization requires compatibility care;
- Windows Camera is diagnostic only;
- software unlock required before useful video;
- camera reopen can cause gray state;
- physical rate baseline ~30 fps;
- raw personal calibration/test photos must not be committed publicly;
- historical Ractiv and Etron redistribution rights must be reviewed before any public bundled installer.

---

## 10. One-line handoff

> `@GitHub Reprends TouchPlus Revival depuis revival/REVIVAL-ROADMAP.md. Phase 1B.2a PR#4 et Phase 1B.2b PR#5 sont mergées. Active slice: branche revival/phase1c-depth-validation / PR #6, candidate calibration 0101007379 + touchplus-depth-sanity.py. Prochaine boundary: smoke physique sur scène texturée à deux distances connues, capture persistante --pairs 1 --output .\\depth-validation\\raw, puis validation du Z métrique avant toute intégration runtime.`
