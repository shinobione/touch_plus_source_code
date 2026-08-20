# TouchPlus Revival — canonical roadmap / handoff

Last updated: **2026-08-20 11:38 CEST**

This file is the canonical resume point for TouchPlus Revival. Read it first in a new ChatGPT/Codex session, then inspect `revival/main`, the active PR and the latest GitHub Actions runs before changing code.

## 0. Project intent

Revive the abandoned Ractiv Touch+ as a modern stereo/3D input device without resurrecting the complete 2015 UI stack.

Canonical pipeline:

`Etron/USB unlock → persistent stereo capture → local metric calibration → rectification → disparity/depth → working-surface frame → 3D hand/finger identity → useful runtime outputs`

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
  - reproducible local solver;
  - robust outlier filtering;
  - per-device calibration bundle + rectification diagnostics.

- **Phase 1C.1 — Physical metric-depth validation** — PR #6 merged at `ae0efe18dcd0616628b83af432909ee01499d507`.
  - solved baseline `59.953 mm` vs physical lens spacing `60–61 mm`;
  - clean 350/600 mm physical depth test preserves distance delta to `+0.82%`;
  - calibration state physically validated.

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

### Active canonical slice

**Phase 2B — hand / fingertip 3D tracking**

Active branch: `revival/phase2b-fingertip-3d`

Active PR: **#9** — Draft / **DO NOT MERGE**.

Latest documented branch head at this handoff: `148caa52ae5585996a475e1e8e7580802dfb9805`. Always re-read PR #9 before relying on that SHA because the branch is actively iterated.

Current experimental revision: **Phase 2B.7 — SCOPA-inspired palm-core finger branch**.

Physical status after the 2026-08-20 bench: **PARTIAL PASS / FINGERTIP IDENTITY FAIL**.

What is accepted inside Phase 2B so far:

- runtime wiring / `T` toggle / surface artifact load;
- learned background with `B`;
- no-hand baseline after learning can remain clean (`changed_cells=0` observed);
- appearance silhouette preserves low-texture distal skin better than dense-depth-only masks;
- physical support bounding removes the early giant-component / long-tail failure class;
- ambiguity handling can reject some multi-branch cases;
- robust stereo refinement can produce metric XYZ when given a valid pixel.

What is **not** accepted:

- anatomical fingertip identity.

In the latest 2B.7 physical bench, a single clearly extended index still produced materially different `tip_pixel` candidates between adjacent frames while the downstream stereo matcher could report MEDIUM/HIGH confidence. Representative sequences include:

```text
231,193 -> 257,207 -> 373,245 -> 243,167
263,173 -> 265,177 -> 189,87
```

Therefore **metric refinement confidence must not be interpreted as identity confidence**. A wrong branch can still lead to a strong stereo match.

Detailed active notes:

- `revival/notes/phase2b-fingertip-3d.md`
- `revival/notes/phase2b7-physical-smoke-2026-08-20.md` on the active branch.

### Next canonical action

Do not merge #9 and do not loosen the stereo matcher.

Continue Phase 2B by strengthening only the 2D identity stage while preserving accepted lower layers:

1. validate the palm core before branch scoring;
2. add short temporal palm persistence;
3. persist finger-branch identity across adjacent frames instead of re-electing independently;
4. require finger-like branch width/length geometry leaving the palm boundary;
5. reject unexplained 2D fingertip jumps;
6. separate **identity confidence** from **stereo refinement confidence**;
7. return `unknown` whenever identity is unstable, even with excellent stereo support;
8. compare the geometry path against a lightweight modern 2D hand-landmark fallback before accumulating more endpoint heuristics.

Do not use temporal smoothing to hide a wrong anatomical choice. Temporal logic starts only after a plausible identity exists.

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

Raw personal calibration/test images and videos are evidence only and must not be committed publicly.

---

## 4. Accepted Phase 2A surface-frame behavior

The surface frame is a **separate per-setup artifact** saved beside the runtime as:

`surface/<serial>.json`

It must never mutate the per-serial camera calibration.

Controls layered into the accepted depth/surface viewer:

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

Current accepted conceptual separation:

```text
background / appearance silhouette
            ↓
physical support bounding
            ↓
2D hand / palm / fingertip IDENTITY
            ↓
robust stereo refinement
            ↓
Xsurface / Ysurface / H
```

The order matters. A correct stereo match for the wrong pixel is still a wrong fingertip.

Phase 2B runtime controls:

- `B` — learn/relearn clean background;
- `T` — tracker ON/OFF;
- existing Phase 1C/2A controls remain available.

Phase 2B acceptance principle: missing, ambiguous or anatomically unstable data must be rejected or marked `unknown` rather than silently turned into a finite fingertip or touch.

Current merge gate for PR #9:

- no persistent hand in a clear learned scene;
- palm diagnostic anatomically plausible;
- one clearly extended index remains the same distal fingertip through vertical, horizontal and diagonal motion;
- two comparable fingers may become ambiguous/unknown;
- finite XYZ stays attached to that same fingertip;
- lowering the index lowers H;
- low texture or identity instability becomes unknown, never wrong finite HIGH.

Touch/click semantics remain Phase 2C and must not be implemented before Phase 2B identity is physically trustworthy.

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
- raw personal calibration/test photos/videos must not be committed publicly;
- moving the Touch+ base or pitch hinge requires **surface-frame** recalibration, not stereo recalibration;
- historical Ractiv/Etron redistribution rights must be reviewed before any public bundled installer;
- do not treat green synthetic CI as physical acceptance for tracking identity;
- do not rely on old `sandbox:/...` artifact links across chat windows; retrieve current artifacts from GitHub Actions.

---

## 8. One-line handoff

> `@GitHub Reprends TouchPlus Revival depuis revival/REVIVAL-ROADMAP.md. Vérifie revival/main puis la PR #9 / branche revival/phase2b-fingertip-3d et ses derniers checks avant toute modification. Phases 0→2A sont physiquement acceptées. Phase 2B.7 palm-core est un PARTIAL PASS mais le bench physique du 2026-08-20 échoue encore sur l'identité fingertip: no-hand/background et silhouette sont bons, mais un index unique peut recevoir des tip_pixel différents avec MEDIUM/HIGH stereo confidence. Ne merge pas #9, ne touche pas K/D/R/T/P/Q ni au surface frame validé. Prochaine action: renforcer l'identité 2D (palm validation + temporal palm/branch persistence + finger-like branch geometry + identity confidence séparée du stereo confidence), puis refaire un smoke physique.`
