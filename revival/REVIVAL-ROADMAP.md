# TouchPlus Revival — canonical roadmap / handoff

Last updated: **2026-08-20 20:47 CEST**

This file is the canonical resume point for TouchPlus Revival. Read it first in a new ChatGPT/Codex session, then inspect `revival/main`, the active PR and the latest GitHub Actions runs before changing code.

## 0. Project intent

Revive the abandoned Ractiv Touch+ as a modern stereo/3D input device without resurrecting the complete 2015 UI stack.

Canonical pipeline:

`Etron/USB unlock → persistent stereo capture → local metric calibration → rectification → disparity/depth → working-surface frame → fingertip identity → touch/contact semantics → useful runtime outputs`

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
  - calibration physically validated without rescaling `K/D/R/T/P/Q`.

- **Phase 1C.2 — Live rectified metric depth viewer** — PR #7 merged at `2c1575efe8f9fc139a553818cb8282d15450855c`.
  - persistent live rectification + diagnostic heatmap;
  - hardened full-resolution matcher with texture gate, NCC, uniqueness, LEFT↔RIGHT consistency, local consensus and temporal rejection;
  - `P` locked probe reports valid %, median Z, MAD and confidence;
  - physical long-run smoke removed catastrophic false depths.

- **Phase 2A — Working-surface frame calibration** — PR #8 merged at `92a34b13f7ac75c6f2362a800ad06c78cb8876fe`.
  - camera calibration remains immutable; surface pose is a separate per-setup artifact;
  - dominant-plane consensus rejects coherent wrong-surface samples and gross stereo outliers;
  - physical fit on unit `0101007379`: `19 / 17` samples/inliers, RMS `0.924 mm`, max residual `2.021 mm`, confidence `HIGH`;
  - bare-table H remains near zero;
  - 53 mm rigid object measured `54.1 mm` / `55.3 mm`.

- **Phase 2B — Hand / Fingertip 3D tracking** — PR #9 squash-merged at `e43e6445708b4fd27d431956e404ae4fd0d8ceae` after physical acceptance.
  - learned background + Touch+ appearance silhouette;
  - V8 temporal palm/branch safety and persistent identity;
  - OpenCV Zoo / MediaPipe hand landmarks used only as a separate 2D anatomical sidecar;
  - raw landmark 8 is diagnostic only because physical evaluation showed confident proximal errors;
  - landmark chain supplies index anatomy/direction while the Touch+ silhouette owns the actual distal boundary;
  - ROI-guided landmark reacquisition raised the real offline ten-pose gate from 5/10 to 8/10 guided distal, with 0 observed wrong published guided tips;
  - live sidecar / shared-memory IPC physically validated;
  - frame-synchronous anatomy fusion rejects stale anatomy after fast pose changes before stereo;
  - physical 2B.9C.2 closeout observed no wrong finite MEDIUM/HIGH fingertip in the final stress smoke;
  - final binding safety rule remains: **wrong finite/HIGH fingertip = BLOCKER; UNKNOWN is acceptable when identity is uncertain**.

Phase 2B physical closeout note:

- `revival/notes/phase2b9c2-physical-smoke-2026-08-20.md`

Historical Phase 2B progression and regressions remain documented under `revival/notes/phase2b*.md`.

### Active canonical slice

**Phase 2C — touch/contact detection**

Phase 2C is now unblocked by the Phase 2B physical pass.

Do **not** start by injecting mouse clicks. First define and validate contact semantics in surface coordinates.

### Next canonical action

Create a dedicated Phase 2C branch/PR and implement a conservative touch-state machine on top of the accepted fingertip stream.

Minimum design boundary:

1. input only accepted current fingertip identity + `Xsurface / Ysurface / H`;
2. identity `UNKNOWN`, stale anatomy or stereo invalidity can never create/continue a touch;
3. use explicit states such as `NO_FINGER → HOVER → APPROACHING → CONTACT_CANDIDATE → TOUCH_DOWN → TOUCH_HELD → RELEASE`;
4. contact must require more than one low-H sample: use temporal persistence + downward/approach context + hysteresis;
5. release threshold must be higher than touch-down threshold to avoid chatter;
6. reject impossible H jumps and identity/branch changes;
7. keep detection and OS injection separate: Phase 2C first proves reliable semantic `TOUCH_DOWN / HOLD / UP` events, then a later slice may map them to Windows touch/mouse;
8. preserve all accepted camera calibration, surface frame, capture, sidecar and stereo boundaries unchanged.

Physical acceptance target for Phase 2C should include:

- hover above the table never clicks;
- slow approach creates exactly one touch-down only near the physical surface;
- holding the finger down produces no click spam;
- lifting produces exactly one release;
- repeated taps produce one down/up pair each;
- lateral motion while touching stays one held contact;
- identity loss / ambiguity immediately fails safe rather than inventing contact;
- no-hand learned scene never emits touch events.

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

## 4. Accepted surface-frame behavior

The surface frame is a **separate per-setup artifact** saved beside the runtime as:

`surface/<serial>.json`

It must never mutate the per-serial camera calibration.

Moving the Touch+ base or pitch hinge requires **surface-frame recalibration**, not camera recalibration.

Recommended recalibration after moving the device: **8–12 well-spread textured points** over the intended work area, including left/right and near/far image regions. A flat printed checkerboard can be used as temporary table texture.

---

## 5. Accepted Phase 2B fingertip architecture

Accepted conceptual separation:

```text
learned background / appearance silhouette
            ↓
V8 palm + branch temporal safety
            ↓
2D landmark anatomy sidecar
            ↓
ROI reacquisition when needed
            ↓
landmark-guided distal projection onto Touch+ silhouette
            ↓
frame-synchronous current-distal validation
            ↓
conservative identity fusion
            ↓
robust Touch+ stereo refinement
            ↓
Xsurface / Ysurface / H
```

Critical ownership rules:

```text
metric_z_source          = TOUCHPLUS_STEREO_ONLY
raw MediaPipe landmark 8 = DIAGNOSTIC ONLY
model Z                  = DISCARDED
OpenCV/ONNX in Etron EXE = NO
K/D/R/T/P/Q              = IMMUTABLE ACCEPTED STACK
surface frame            = SEPARATE PER-SETUP ARTIFACT
```

The asynchronous anatomy sidecar may contribute a candidate only if its source/current-frame relationship remains meaningful. Current-distal validation and identity fusion fail closed on excessive pose/shape changes.

A correct stereo match for the wrong pixel is still a wrong fingertip. Stereo confidence must never override identity confidence.

---

## 6. Phase 2C — touch/contact semantics

Phase 2C consumes the accepted Phase 2B output; it does not redefine fingertip identity or metric depth.

Recommended first implementation:

```text
VALID FINGERTIP
(Xsurface, Ysurface, H)
        ↓
contact temporal state machine
        ↓
semantic events only
HOVER / TOUCH_DOWN / TOUCH_HELD / TOUCH_UP
```

Safety requirements:

- invalid/unknown identity → no contact;
- invalid stereo → no contact;
- stale anatomy → no contact;
- touch-down requires sustained near-surface evidence, not one frame;
- hysteresis separates down/up thresholds;
- unexpected H or XY jumps reset/fail safe;
- branch/identity change resets contact candidate;
- OS mouse/touch injection is a separate later boundary after semantic events physically pass.

---

## 7. Phase 3 — useful outputs

Potential outputs after reliable touch semantics:

- Windows pointer/touch;
- gestures / pinch;
- OSC / MIDI;
- Unity/Unreal bridge;
- custom app integrations;
- debug telemetry / recording.

---

## 8. Constraints / cautions

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
- do not treat green synthetic CI as physical acceptance;
- do not rely on old `sandbox:/...` artifact links across chat windows; retrieve current artifacts from GitHub Actions.

---

## 9. One-line handoff

> `@GitHub Reprends TouchPlus Revival depuis revival/REVIVAL-ROADMAP.md. Vérifie revival/main et l'active PR avant toute modification. Phases 0→2B sont physiquement acceptées. PR #9 a été squash-mergée à e43e6445708b4fd27d431956e404ae4fd0d8ceae après le PASS physique 2B.9C.2. Ne touche pas K/D/R/T/P/Q, au surface frame accepté, au capture pipeline ni au matcher stéréo. Prochaine phase: 2C touch/contact semantics, d'abord en événements HOVER/DOWN/HOLD/UP fail-closed, sans injection Windows tant que le smoke physique n'est pas passé.`
