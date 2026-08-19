# TouchPlus Revival — canonical roadmap / handoff

Last updated: **2026-08-19 20:23 CEST**

This file is the canonical resume point for TouchPlus Revival. Read it first in a new ChatGPT/Codex session, then inspect the active branch/PR before changing code.

## 0. Project intent

Revive the abandoned Ractiv Touch+ as a modern stereo/3D input device without resurrecting the complete 2015 UI stack.

Canonical pipeline:

`Etron/USB unlock → persistent stereo capture → local metric calibration → rectification → disparity/depth → working-surface frame → 3D hand/finger tracking → useful runtime outputs`

Revival code lives under `revival/`; canonical integration branch is `revival/main`.

---

## 1. Canonical Git state

Repository: `shinobione/touch_plus_source_code`

Canonical base: `revival/main`

### Completed / merged

- **Phase 0 — Hardware Probe / Atomic Capture**
  - Touch+ detected and unlocked with Etron vendor control + `SWUnlock(0x0107)`;
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
  - 20-pose physical calibration dataset captured.

- **Phase 1B.2b — Robust local metric calibration solver** — PR #5 merged at `b04d703499d1af8cea14416c2eec4fb5420868bd`.
  - reproducible Python solver;
  - robust outlier filtering;
  - JSON/YAML calibration bundle + rectification diagnostics.

- **Phase 1C.1 — Physical metric-depth validation** — PR #6 merged at `ae0efe18dcd0616628b83af432909ee01499d507`.
  - solved baseline `59.953 mm` vs physical lens spacing `60–61 mm`;
  - clean 350/600 mm physical depth test preserves distance delta to `+0.82%`;
  - calibration state: `candidate_physical_depth_validated`.

- **Phase 1C.2 — Live rectified metric depth viewer** — PR #7 merged at `2c1575efe8f9fc139a553818cb8282d15450855c`.
  - persistent live rectification + diagnostic heatmap;
  - hardened full-resolution cursor matcher: texture gate, NCC, uniqueness, LEFT↔RIGHT consistency, local disparity consensus, temporal rejection;
  - `P` locked probe reports valid %, median Z, MAD and confidence;
  - physical long-run smoke removed previous catastrophic false depths;
  - same fixed planar target, seven independent valid probe locations: fitted 3D plane `R²≈0.9984`, RMS residual `≈0.41 mm`, max residual `≈0.57 mm`;
  - one texture-poor location correctly returned 0 valid samples instead of a false finite Z.

### Active slice

**Phase 2A — working-surface frame calibration**

- branch: `revival/phase2a-surface-frame`
- PR: **#8** (Draft until physical smoke)
- notes: `revival/notes/phase2a-surface-frame.md`
- CI: `.github/workflows/revival-surface-frame.yml`

Goal: learn the table/work-surface pose separately from immutable stereo calibration so the Touch+ mechanical pitch hinge is a supported setup variable rather than a geometry bug.

Runtime surface coordinates:

- `Xsurface`, `Ysurface`: coordinates inside the fitted work plane;
- `H`: signed perpendicular distance from that plane;
- `H = 0`: surface;
- `H > 0`: point above surface, toward the cameras.

---

## 2. Physical device facts

Physical unit:

- USB: `VID_1E4E / PID_0107`
- friendly name: `Touch+ Camera`
- historical sensor: `OV7740`
- serial: **`0101007379`**
- historical cloud key: **`7379`**
- two lens centers measured at approximately **60–61 mm** apart.

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

The physical camera bar has a mechanical pitch hinge. Moving that hinge does **not** invalidate intrinsic/stereo calibration, but it **does** invalidate the camera-to-working-surface transform. Refit Phase 2A after moving the hinge or Touch+ base.

---

## 3. Calibration / metric geometry facts

Printed target:

- 10x7 squares;
- 9x6 inner corners;
- 25.0 mm square size;
- A4 landscape at 100% actual size;
- printed 100 mm reference physically measured exact.

Physical dataset:

- 20 synchronized stereo pairs;
- all 20 pairs detect 54/54 internal corners in both eyes;
- robust final solve uses 17 pairs, excluding 016/017/018.

Accepted solve:

- mono RMS LEFT: **0.3472 px**;
- mono RMS RIGHT: **0.3912 px**;
- stereo RMS: **0.3799 px**;
- baseline: **59.953 mm**;
- rectified vertical epipolar mean: **0.1195 px**;
- p95: **0.3096 px**;
- max: **1.2763 px**.

Camera-coordinate depth physical validation:

- ruler/front-plane 350±5 mm → camera-Z 380.63 mm;
- ruler/front-plane 600±5 mm → camera-Z 632.68 mm;
- physical delta 250 mm vs stereo delta 252.05 mm → **+0.82%** scale error;
- approximately fixed front-reference to camera-origin offset ~31.7 mm is documented but never baked into `K/D/R/T/P/Q`.

Raw personal calibration/test images are evidence only and must not be committed publicly.

---

## 4. Phase 2A design — surface frame

The surface frame is a **separate per-setup artifact** saved beside the runtime as:

`surface/<serial>.json`

It must never mutate the per-serial camera calibration.

Current controls layered into `touchplus_depth_viewer.exe`:

- `C` — capture one fixed surface point for 45 frames with the hardened matcher;
- `F` — robust plane fit from pending points; MAD outlier rejection; save MEDIUM/HIGH fits only;
- `R` — reset pending surface samples only;
- `H` — print camera XYZ + `Xsurface / Ysurface / H`;
- `P` — existing Phase 1C locked probe diagnostic;
- `D` / `S` / `Q` — existing depth / rectified stereo / quit.

Recommended calibration: **8–12 well-spread textured points** over the intended work area, including left/right and near/far image regions.

Surface confidence uses:

- sample/inlier count;
- plane RMS and max residual;
- spatial X/Y coverage.

LOW fits are not saved.

### Physical acceptance required before merge PR #8

1. keep Touch+ base + pitch hinge fixed;
2. capture >=8 broad surface points with `C`;
3. press `F`; require MEDIUM/HIGH fit;
4. check `H` on several bare-surface textured positions → close to 0 mm;
5. put one rigid textured object of known thickness on the surface → top-face `H` positive and plausibly near thickness;
6. no regression in persistent capture, rectification or Phase 1C camera-Z behavior.

---

## 5. Phase 2B — hand/fingertip tracking (after Phase 2A)

Only after the working-surface frame is physically accepted:

1. derive surface-relative foreground from `H` rather than raw camera-Z;
2. isolate hand region above the plane;
3. extract index/fingertip candidates;
4. produce live `(Xsurface, Ysurface, H)` fingertip position;
5. add temporal smoothing and confidence;
6. distinguish hover / touch-down / release from surface-relative height and motion.

Start geometry-first. Do not jump to a giant AI model unless needed.

---

## 6. Phase 3 — useful outputs

Potential outputs:

- Windows pointer/touch;
- gestures / pinch;
- OSC / MIDI;
- Unity/Unreal bridge;
- custom app integrations;
- debug telemetry / recording.

---

## 7. Constraints / cautions

- historical Etron stack is Win32/32-bit;
- Etron COM initialization needs compatibility handling;
- Windows Camera is diagnostic only;
- software unlock is required before useful video;
- camera reopen can cause gray state;
- accepted physical rate baseline is ~30 fps;
- never reuse the deprecated repeated PowerShell one-shot calibration workflow;
- raw personal calibration/test photos must not be committed publicly;
- moving the Touch+ base or pitch hinge requires **surface-frame** recalibration, not stereo recalibration;
- historical Ractiv/Etron redistribution rights must be reviewed before any public bundled installer.

---

## 8. One-line handoff

> `@GitHub Reprends TouchPlus Revival depuis revival/REVIVAL-ROADMAP.md. PR #7 live metric depth is merged at 2c1575e. Active Phase 2A is PR #8 / revival/phase2a-surface-frame: keep immutable camera calibration, fit a separate working-surface frame so Xsurface/Ysurface/H absorb the Touch+ mechanical pitch hinge. Current runtime adds C=capture surface point, F=fit/save, R=reset pending, H=measure surface-relative height. Do not merge #8 until physical surface smoke passes.`
