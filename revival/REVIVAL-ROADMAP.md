# TouchPlus Revival — canonical roadmap / handoff

Last updated: **2026-08-19 21:41 CEST**

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

- **Phase 2A — Working-surface frame calibration** — PR #8 merged at `92a34b13f7ac75c6f2362a800ad06c78cb8876fe`.
  - immutable camera calibration is preserved; surface pose is a separate per-setup artifact;
  - deterministic dominant-plane consensus rejects coherent wrong-surface samples and gross stereo outliers;
  - runtime controls: `C` capture, `F` fit/save, `R` reset pending, `U` undo last pending point, `H` surface-relative height, `P` locked depth probe;
  - physical fit on unit `0101007379`: `19 / 17` samples/inliers, RMS `0.924 mm`, max residual `2.021 mm`, coverage `403.9 × 356.9 mm`, confidence `HIGH`;
  - seven bare-surface H checks: `+1.7, -0.4, +1.4, +2.6, -0.9, -0.4, +0.3 mm`;
  - one texture-poor H location correctly returned invalid instead of a false height;
  - rigid book measured `53 mm` thick produced top-face H readings `54.1 mm` and `55.3 mm`.

### Active / next canonical slice

**Phase 2B — hand / fingertip 3D tracking**

No Phase 2B branch/PR should be assumed until it is created from the current `revival/main`.

Goal: use the physically accepted surface frame to detect a hand above the work plane and emit a stable fingertip position as `(Xsurface, Ysurface, H)`.

Start geometry-first and keep the already validated camera/depth/surface layers immutable unless a regression is demonstrated.

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

The physical camera bar has a mechanical pitch hinge. Moving that hinge does **not** invalidate intrinsic/stereo calibration, but it **does** invalidate the camera-to-working-surface transform. Refit the working-surface frame after moving the hinge or Touch+ base.

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

Working-surface physical validation:

- dominant-plane fit: 19 samples / 17 inliers;
- plane RMS: **0.924 mm**;
- plane max residual: **2.021 mm**;
- coverage: **403.9 × 356.9 mm**;
- bare-surface H measurements remain within **-0.9 to +2.6 mm** on seven valid locations;
- 53 mm rigid object top face measured **54.1 mm** and **55.3 mm**.

Raw personal calibration/test images are evidence only and must not be committed publicly.

---

## 4. Accepted Phase 2A surface-frame behavior

The surface frame is a **separate per-setup artifact** saved beside the runtime as:

`surface/<serial>.json`

It must never mutate the per-serial camera calibration.

Controls layered into `touchplus_depth_viewer.exe`:

- `C` — capture one fixed surface point for 45 frames with the hardened matcher;
- `F` — deterministic dominant-plane fit + robust refinement; save MEDIUM/HIGH fits only;
- `R` — reset pending surface samples only;
- `U` — undo the latest accepted pending surface point;
- `H` — print camera XYZ + `Xsurface / Ysurface / H`;
- `P` — existing Phase 1C locked probe diagnostic;
- `D` / `S` / `Q` — existing depth / rectified stereo / quit.

Recommended recalibration after moving the device: **8–12 well-spread textured points** over the intended work area, including left/right and near/far image regions. A flat printed checkerboard can be used as temporary table texture.

The dominant-plane fitter uses a physically capped consensus threshold so 50–100 mm wrong-depth samples cannot self-justify as inliers by inflating a global MAD threshold.

---

## 5. Phase 2B — hand/fingertip tracking

Next canonical work:

1. derive surface-relative foreground from `H` rather than raw camera-Z;
2. isolate hand region above the plane;
3. extract index/fingertip candidates;
4. produce live `(Xsurface, Ysurface, H)` fingertip position;
5. add temporal smoothing and confidence;
6. distinguish hover / touch-down / release from surface-relative height and motion.

Start geometry-first. Do not jump to a giant AI model unless needed.

Important Phase 2B acceptance principle: missing/low-confidence stereo data should be rejected or marked unknown rather than silently turned into a false fingertip or false touch.

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

> `@GitHub Reprends TouchPlus Revival depuis revival/REVIVAL-ROADMAP.md. Phase 2A working-surface frame is physically accepted and merged via PR #8 at 92a34b13. Bare-surface H is near zero and a 53 mm object measures 54.1–55.3 mm. The next canonical slice is Phase 2B: create a fresh branch from current revival/main and implement geometry-first hand/fingertip tracking in Xsurface/Ysurface/H. Keep K/D/R/T/P/Q and the accepted surface-frame layer immutable unless a regression is demonstrated.`
