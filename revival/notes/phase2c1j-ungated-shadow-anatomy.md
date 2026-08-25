# Phase 2C.1J / 2C.1J.1 — ungated shadow anatomy probe

## Why this slice exists

The physical Phase 2C.1I dataset was valid as instrumentation but not usable for contact tuning. Across the labelled run, the diagnostic target never became FUSED or ANATOMY; the few raw-dense samples all came from GEOMETRY and were explicitly untrusted. During real contact the geometry point was visibly on the wrong branch of the hand.

The accepted Phase 2B.9C sidecar is gated before model inference by `hand_valid`. When the accepted V5/V6/V8 hand pipeline fails near the surface, the sidecar therefore reports UNAVAILABLE without asking OpenCV Zoo whether a visible index still exists.

Phase 2C.1J isolates that question only:

> When the accepted hand gate is false near/at contact, can the same landmark/anatomy logic still recover a plausible index fingertip from the live LEFT image?

## 2C.1J.1 low-impact correction

The first physical launch of 2C.1J showed severe horizontal tearing/corruption in the viewer and reduced the accepted capture/source rates from the normal ~30 fps range to roughly 17–23 fps. Relaunching the same exact-head viewer with only the accepted `start-touchplus-phase2b9c.ps1` sidecar immediately restored a clean image at about 29.6 fps capture / 30.4 Hz source. The Touch+ camera/USB/rectification path is therefore not treated as failed; the double-sidecar diagnostic load is the suspect.

2C.1J.1 keeps the same isolated IPC architecture but makes the shadow path intentionally sparse:

```text
probe eligibility:
  background_ready = true
  accepted_hand_valid = false
  accepted_anatomy = UNAVAILABLE

probe cadence:
  one frame in six (~5 Hz on the accepted ~30 fps stream)

shadow mask:
  computed only on a scheduled eligible probe frame
  not computed while accepted_hand_valid=true

shadow OpenCV process:
  OpenCV worker budget = 1 thread
  idle poll interval = 12 ms
```

Plausibility gates also reject shadow masks smaller than 120 half-resolution cells or larger than 65% of the 320x240 shadow grid before model inference.

This is deliberately narrower than original 2C.1J. It is designed only to observe the exact failure case discovered by 2C.1I: accepted hand ownership has failed and accepted anatomy is unavailable. It does not run a second full-rate anatomy model alongside a healthy accepted hand.

Telemetry adds:

```text
probe=RUN|SKIP
gate=DUE|ACCEPTED_HAND_VALID|ACCEPTED_ANATOMY_NOT_UNAVAILABLE|THROTTLED|...
period_frames=6
```

The 2C.1J CSV is also reduced to actual probe rows plus a ~1 Hz gate heartbeat; the full per-frame 2C.1I dataset remains available separately.

## Hard isolation boundary

The accepted Phase 2B.9C IPC remains unchanged:

```text
Local\TouchPlusRevival2B9C_Frame_v1
Local\TouchPlusRevival2B9C_Result_v1
```

2C.1J uses separate named memory:

```text
Local\TouchPlusRevival2C1J_ShadowFrame_v1
Local\TouchPlusRevival2C1J_ShadowResult_v1
```

The accepted V9 tracker never reads the shadow result map. The shadow result cannot enter:

- accepted anatomy;
- V9 fusion / identity;
- stereo refinement or Q;
- calibration or surface state;
- 2B.10D promotion/smoothing;
- Phase 2C contact semantics;
- mouse/touch/PointerMapper/UDP/OS output.

`accepted_pipeline_consumes_shadow=NO` and `OS_INJECTION=DISABLED` are explicit telemetry invariants.

## Shadow input mask

2C.1J does not modify or expose the accepted selected hand mask. After the complete accepted + 2C.1G/H/I stack has run, it independently derives a half-resolution mask from the already-learned hybrid LEFT background:

```text
appearance_delta_v4(current, learned_background) >= 24
```

The threshold is the existing V5 `kV5AppearanceOnlyDelta=24.0`; no new matcher/surface/contact threshold is introduced. This mask is labelled:

```text
APPEARANCE_ONLY_24
```

It is intentionally not treated as a physically accepted hand. It exists only to give the shadow sidecar a Touch+-derived region in which to test anatomy when accepted `hand_valid=0`.

## Shadow sidecar behavior

`touchplus_landmark_sidecar_shadow_v2c1j.py` reuses the accepted OpenCV Zoo/anatomy functions, but runs in a second process on the shadow IPC.

On that shadow channel only:

- background READY remains mandatory;
- a non-empty APPEARANCE_ONLY_24 mask remains mandatory;
- the accepted `hand_valid` bit is preserved in telemetry;
- a local copy passed into the anatomy evaluator sets `hand_valid=true` so model inference is allowed to run.

This bypass never changes the original frame and cannot affect the accepted sidecar process.

## Telemetry

Console:

```text
[ANATOMY_SHADOW]
frame=...
label=HIGH|NEAR|CONTACT|NONE
probe=RUN|SKIP
gate=...
period_frames=6
mask_cells=...
accepted_hand=0|1
accepted_anatomy=...
accepted_fusion=0|1
shadow_status=GUIDED_DISTAL|GUIDED_REJECTED|UNAVAILABLE|STALE|ERROR
shadow_age=...
shadow_source=...
shadow_pose=...
tip=x,y
axis_q=...
hand_conf=...
continuity=...
accepted_pipeline_consumes_shadow=NO
shadow_only=YES
authoritative=UNCHANGED
OS_INJECTION=DISABLED
```

CSV:

```text
touchplus-phase2c1j-shadow-anatomy-YYYYMMDD-HHMMSS.csv
```

Numeric operator labels remain the non-conflicting Phase 2C.1I scheme:

```text
1 = HIGH
2 = NEAR
3 = CONTACT
0 = NONE
```

Labels are ground truth only and are not used by the shadow anatomy decision.

## Physical interpretation

A strong positive result is repeated scheduled probe rows with:

```text
probe=RUN
accepted_hand=0
accepted_anatomy=UNAVAILABLE
shadow_status=GUIDED_DISTAL
```

with the reported shadow tip visibly following the real index through NEAR and CONTACT.

That would show that the main failure is upstream gating/hand-support ownership, not necessarily the anatomy model itself. Any future use in an accepted path would still require a new, separately reviewed and physically gated adapter; 2C.1J does not grant promotion.

A negative result is shadow anatomy also becoming UNAVAILABLE/REJECTED or producing an obviously wrong stable/high-confidence fingertip in the same contact pose. In that case, bypassing the accepted hand gate is not a viable contact solution.

## Merge / safety status

Phase 2C.1J/2C.1J.1 is diagnostic-only. It does not satisfy the Phase 2C contact physical gate. PR #17 remains **Draft / DO NOT MERGE** until real Touch+ contact semantics pass the established hover/down/held/up safety criteria.
