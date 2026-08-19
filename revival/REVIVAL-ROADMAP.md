# TouchPlus Revival — canonical roadmap / handoff

Last updated: **2026-08-19 18:10 CEST**

This file is the canonical resume point for TouchPlus Revival. Read it first in a new ChatGPT/Codex session, then inspect the active branch/PR before changing code.

## 0. Project intent

Revive the abandoned Ractiv Touch+ as a modern stereo/3D input device without resurrecting the complete 2015 UI stack.

Canonical pipeline:

`Etron/USB unlock → persistent stereo capture → local metric calibration → rectification → disparity/depth → 3D tracking → useful runtime outputs`

Revival code lives under `revival/`; canonical integration branch is `revival/main`.

---

## 1. Canonical Git state

Repository: `shinobione/touch_plus_source_code`

Canonical base: `revival/main`

### Completed / merged

- **Phase 0 — Hardware Probe / Atomic Capture**
  - physical Touch+ detected;
  - Etron vendor control + `SWUnlock(0x0107)` work;
  - IMU works;
  - real 1280x480 stereo frame splits into two 640x480 eyes.

- **Phase 1A — Stable Stereo Viewer**
  - persistent LEFT/RIGHT live viewer;
  - historical vertical flip reproduced;
  - GDI flicker fixed;
  - device advertises 60 fps but physically delivers ~30 fps on this unit/driver path.

- **Phase 1B.1 — Factory calibration identity recovery**
  - Ractiv serial `0101007379` recovered from flash;
  - historical cloud key `7379` derived;
  - old factory CDN confirmed unavailable.

- **Phase 1B.2a — Persistent live calibration capture** — PR #4 merged at `45a1eda7dfaf8f5adafcf556b69b9ad1b8dabcb5`.
  - canonical executable: `touchplus_calibration_capture.exe`;
  - one unlock + one persistent stream;
  - LEFT/RIGHT preview remains live;
  - SPACE saves synchronized pairs without camera reopen;
  - gray/uniform guard;
  - physical 3-pair smoke PASS;
  - 20-pose physical calibration dataset captured.

- **Phase 1B.2b — Robust local metric calibration solver** — PR #5 merged at `b04d703499d1af8cea14416c2eec4fb5420868bd`.
  - reproducible Python solver;
  - robust outlier filtering;
  - rectification diagnostics;
  - JSON/YAML bundle outputs;
  - real 20-pose numeric solve PASS.

### Active / closing slice

**Phase 1C — candidate calibration + physical metric depth validation**

- branch: `revival/phase1c-depth-validation`
- PR: **#6**
- candidate: `revival/calibration/candidates/0101007379.json`
- diagnostic: `revival/tools/touchplus-depth-sanity.py`
- notes: `revival/notes/phase1c-depth-validation.md`
- CI: `.github/workflows/revival-depth-sanity.yml`

Physical metric-depth smoke is now **PASS**. Candidate state is:

`candidate_physical_depth_validated`

The calibration is accepted for the next live runtime integration slice but is not yet consumed by the live runtime.

---

## 2. Physical device facts

Physical unit:

- USB: `VID_1E4E / PID_0107`
- friendly name: `Touch+ Camera`
- historical sensor: `OV7740`
- serial: **`0101007379`**
- historical cloud key: **`7379`**

Confirmed control behavior:

- Etron init/enumeration/select works;
- `eSPAEAWB_SetSensorType(OV7740)` works;
- `eSPAEAWB_SWUnlock(0x0107)` works;
- IMU/accelerometer works;
- exposure/gain/GPIO/LED calls work;
- recovered Ractiv LegacyInit works.

Critical behavior: video state is session-sensitive. Reliable software must unlock and keep the stream open in one controlled flow. Reopening/switching camera clients can return gray video.

Stereo facts:

- advertised mode: `1280x480 @ 60 fps MJPG`;
- LEFT / RIGHT: 640x480 each;
- real physical stereo parallax confirmed;
- canonical orientation requires historical vertical flip;
- accepted measured cadence: ~30 fps.

Do not reopen the 60-fps investigation unless later functionality truly requires it.

---

## 3. Calibration target / physical dataset

Metric target:

- 10x7 printed squares;
- 9x6 inner corners;
- 25.0 mm square size;
- 250x175 mm board;
- A4 landscape at 100% / Actual Size.

The printed 100 mm reference bar was physically measured as exactly 100 mm.

The deprecated repeated PowerShell one-shot capture loop must never be reused. Canonical capture is `touchplus_calibration_capture.exe` with one persistent camera session.

Physical calibration dataset:

- 20 synchronized stereo pairs;
- 40 eye PNGs;
- 20/20 pairs yield complete 54/54 checkerboard-corner detection in both eyes.

Raw user photos remain evidence only and are not committed publicly.

---

## 4. Phase 1B.2b accepted solver result

Robust policy:

`combined LEFT/RIGHT mono reprojection RMSE → median + 2.5 × robust MAD sigma`

Physical 20-pose result:

- input: 20;
- complete detected: 20;
- final accepted: 17;
- excluded gross outliers: 016, 017, 018;
- robust threshold: 1.0305 px;
- mono RMS LEFT: **0.3472 px**;
- mono RMS RIGHT: **0.3912 px**;
- stereo RMS: **0.3799 px**;
- solved baseline: **59.953 mm**;
- rectified vertical epipolar mean: **0.1195 px**;
- p95: **0.3096 px**;
- max: **1.2763 px**.

Rectified previews visually PASS: matching features align horizontally, no eye swap, no orientation regression.

Independent physical lens-center spacing was measured at approximately **60–61 mm**, confirming the solved baseline/metric target scale.

---

## 5. Phase 1C physical metric-depth smoke — PASS

The first two-box smoke used an incorrectly packaged ruler and its absolute reference values were invalidated. Do not reuse those distances.

A clean repeat used one centered textured Chocapic box at two independently measured front-plane distances:

- pair 001: **350 mm ±5 mm**;
- pair 002: **600 mm ±5 mm**.

Both pairs are serial `0101007379`, 640x480 LEFT/RIGHT and already in canonical upright orientation.

Validation method:

- candidate rectification;
- dense StereoSGBM coherence check;
- epipolar-constrained SIFT correspondences on the planar textured box face;
- robust disparity-plane fit;
- evaluate Z at the rectified principal point / stereo axis.

Camera-coordinate Z results:

- pair 001: **380.63 mm**;
- pair 002: **632.68 mm**.

The absolute values differ from ruler readings because `Q` reports camera-coordinate Z while the ruler was referenced to the Touch+ lens/front plane. The implied additive origin offsets are +30.63 mm and +32.68 mm, consistent with a fixed ~31.66 mm reference-origin difference within the physical measurement uncertainty.

The decisive scale test is the distance change:

- physical delta: `600 - 350 = 250 mm`;
- stereo delta: `632.68 - 380.63 = 252.05 mm`;
- delta-scale error: **+0.82%**.

Acceptance gates:

- rectified orientation/alignment: PASS;
- near/far ordering: PASS;
- useful textured dense disparity: PASS;
- solved baseline vs physical lens spacing: PASS;
- metric delta scale: PASS;
- no gross sign / eye-order / scale / Q failure: PASS.

Important: the inferred ~31.66 mm front-plane-to-camera-origin offset is documented but **not baked into K/D/R/T/P/Q**. Camera-coordinate Z remains canonical geometry.

---

## 6. NEXT ACTION — live rectified depth runtime

After PR #6 closeout, open the next Phase 1C runtime slice.

Canonical goals:

1. load validated calibration by serial `0101007379`;
2. retain the accepted Etron unlock + single persistent stereo stream;
3. rectify live LEFT/RIGHT frames;
4. compute live disparity;
5. reproject with `Q` to metric camera-coordinate Z;
6. add diagnostic views:
   - raw stereo;
   - rectified stereo;
   - disparity;
   - depth visualization;
   - point/cursor depth readout;
7. characterize depth stability/error at several known distances over the intended working volume.

Do not begin hand/finger tracking until live metric geometry is stable.

---

## 7. Phase 2 — modern tracking core

After live depth is trustworthy:

1. foreground / working-surface mask;
2. hand region detection;
3. finger/index candidate extraction;
4. stereo correspondence using rectified geometry;
5. triangulated 3D fingertip position;
6. temporal smoothing / confidence;
7. touch-plane calibration;
8. hover / touch-down / release state.

Start with geometry-first methods; do not jump to a giant AI model unless needed.

---

## 8. Phase 3 — useful outputs

Potential outputs:

- Windows pointer/touch;
- gestures / pinch;
- OSC;
- MIDI;
- Unity/Unreal bridge;
- custom app integrations;
- debug telemetry / recording.

---

## 9. Constraints / cautions

- historical Etron stack is Win32/32-bit;
- Etron COM initialization needs compatibility handling;
- Windows Camera is diagnostic only;
- software unlock is required before useful video;
- camera reopen can cause gray state;
- accepted physical rate baseline is ~30 fps;
- raw personal calibration/test photos must not be committed publicly;
- historical Ractiv/Etron redistribution rights must be reviewed before any public bundled installer.

---

## 10. One-line handoff

> `@GitHub Reprends TouchPlus Revival depuis revival/REVIVAL-ROADMAP.md. PR #4 (persistent calibration capture) et PR #5 (robust metric solver) sont mergées. PR #6 / revival/phase1c-depth-validation has physical metric-depth PASS: solved baseline 59.953 mm vs physical 60–61 mm; clean centered Chocapic test at 350±5 and 600±5 mm gives camera-Z 380.63 and 632.68 mm, delta 252.05 mm vs physical 250 mm (+0.82%). Candidate state = candidate_physical_depth_validated; next boundary = live rectified disparity/depth runtime, without reusing the deprecated PowerShell one-shot calibration path.`
