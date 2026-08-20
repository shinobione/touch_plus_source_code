# Phase 2B — Hand / Fingertip 3D

## Current slice: Phase 2B.8 — temporal palm + persistent finger identity

Physical unit: `0101007379`.

Current status: **2B.8 SYNTHETIC CI PASS / PHYSICAL SMOKE REQUIRED / DO NOT MERGE**.

The binding physical result remains the 2026-08-20 Phase 2B.7 bench: **PARTIAL PASS / FINGERTIP IDENTITY FAIL**. Phase 2B.8 exists specifically to reject that failure class before stereo refinement.

Phase 2A already proved a useful working-surface frame on real hardware: bare table near `H=0`, 53 mm book measured about 54–55 mm. Phase 2B sits on top of that accepted metric stack; it does not modify camera calibration or the saved surface transform.

Historical physical evidence:

- `revival/notes/phase2b7-physical-smoke-2026-08-20.md`

Modern-landmark comparison note:

- `revival/notes/phase2b8-landmark-fallback-evaluation.md`

## Physical progression

1. **2B.1 wiring PASS** — tracker runtime, `T`, surface-model load and heartbeat work.
2. **2B.1 foreground FAIL** — real scene produced ~19k–26k-cell pseudo-hands.
3. **2B.2 hardened segmentation PARTIAL PASS** — stronger dense-depth gates removed the giant 20k-cell continent; real components dropped to hundreds/few-thousands and full-res XYZ could succeed.
4. **2B.2 fingertip identity FAIL** — finite candidates landed on wrist/back-of-hand.
5. **2B.3 top-entry geodesic FAIL** — no-hand pseudo-hands and jumping coarse endpoints remained.
6. **2B.4 learned background PARTIAL PASS** — physical video proved `NOT_READY -> LEARNING -> READY` works and dramatically improved no-hand rejection.
7. **2B.5 appearance silhouette PARTIAL PASS / fingertip FAIL** — low-texture distal skin became visible independently of dense-depth support, but identity still jumped.
8. **2B.6 support-bounded skeleton PARTIAL PASS / fingertip FAIL** — ambiguity improved, but anatomically wrong candidates could still receive HIGH stereo confidence.
9. **2B.7 palm-core branch PARTIAL PASS / fingertip FAIL** — SCOPA-inspired palm-first decomposition improved diagnostics, but a clearly extended single index could still be re-elected as materially different 2D pixels between adjacent frames.
10. **2B.8 temporal palm/branch identity CANDIDATE** — identity is now persisted and gated in 2D before stereo refinement; synthetic x64/Win32 regressions are green, physical validation pending.

## Critical lesson from 2B.7

Representative physical single-index sequence:

```text
263,173 -> 265,177 -> 189,87
```

The downstream stereo matcher could report MEDIUM/HIGH on these selected pixels, but the imagery did not support treating the final jump as the same distal index.

Therefore:

> **stereo refinement confidence != fingertip identity confidence**

A stereo matcher can be correct for the pixel it receives while the 2D anatomical stage selected the wrong pixel. Phase 2B.8 moves identity continuity upstream so a bad anatomical jump becomes `UNKNOWN` before NCC/stereo can legitimize it.

## Accepted preprocessing retained unchanged

1. `B` learns a clean grayscale background;
2. V5 appearance change produces a 2D hand silhouette so low-texture distal skin remains visible;
3. V6 physical support bounding trims long appearance-only tails using current above-plane stereo support;
4. accepted Phase 1C matcher remains responsible only for metric refinement after identity passes;
5. accepted Phase 2A surface transform remains a separate per-setup artifact.

Do not loosen `K/D/R/T/P/Q`, the accepted surface frame, or stereo gates to improve identity yield.

## Phase 2B.8 architecture

```text
learned background / appearance silhouette
        |
        v
V6 physical support bounding
        |
        v
palm observation + palm validation
        |
        v
finger-like branch descriptors
  - palm-boundary attachment
  - extension / normalized length
  - width relative to palm
  - branch linearity
  - distal taper
        |
        v
temporal palm persistence
        |
        v
persistent branch association
        |
        v
2D jump rejection after palm-motion compensation
        |
        v
UNKNOWN -> ACQUIRING -> LOCKED
        |
        +---- unstable / ambiguous ----> UNKNOWN
        |
        v
ONLY LOCKED identity reaches stereo refinement
        |
        v
identity_confidence + stereo_confidence
        |
        v
finite XYZ only if BOTH pass
```

### Palm validation

V8 still uses the useful V7 interior-distance palm proposal, but does not trust it automatically. It checks:

- palm center is inside the supported silhouette;
- palm radius is plausible relative to the hand bounding box;
- a compact interior disk around the palm is sufficiently filled;
- palm position is plausible relative to the top-entry forearm;
- subsequent observations remain compatible with a short temporal palm track.

A sudden palm relocation is rejected rather than silently changing the anatomical root of the finger track.

### Finger-like geometry

Branches outside the palm are described instead of being ranked only by length. V8 considers:

- connection near the palm boundary;
- normalized extension beyond the palm;
- branch width from the silhouette distance transform;
- width relative to palm radius;
- PCA-style branch linearity;
- proximal/mid/distal width and taper;
- terminal direction.

The visible fingertip extension uses a narrow terminal corridor around the branch axis rather than V7's broader palm-origin cone, reducing attraction to appearance tails.

### Persistent branch identity

Identity state is explicit:

- `UNKNOWN`
- `ACQUIRING`
- `LOCKED`

A branch must remain compatible for multiple observations before it becomes `LOCKED`. Association uses:

- root angle around the palm;
- branch direction;
- normalized extension;
- normalized width;
- geometry score;
- expected fingertip location after compensating for palm motion.

A locked branch is not replaced by a different endpoint in one frame simply because the new endpoint happens to look longer.

### Unexplained jump rejection

Large fingertip motion that is not explained by matching palm movement becomes `UNKNOWN` for that frame. It is rejected **before stereo refinement**.

Short gaps retain branch memory for reacquisition, but no stale fingertip is published during the gap. After repeated misses the branch identity resets and must be acquired again.

## Two independent confidences

Runtime diagnostics now expose:

```text
identity_state=UNKNOWN|ACQUIRING|LOCKED
identity_confidence=LOW|MEDIUM|HIGH
stereo_confidence=NOT_RUN|LOW|MEDIUM|HIGH
```

The final fingertip is valid only when both identity and stereo pass.

Examples:

```text
identity=LOW + stereo=HIGH     -> UNKNOWN
identity=MEDIUM + stereo=HIGH  -> finite candidate allowed
identity=HIGH + stereo=LOW     -> UNKNOWN
```

A rejected identity normally leaves stereo as `NOT_RUN`, because the matcher is not called for an anatomically rejected candidate.

The existing metric smoothing is scoped to the currently locked branch ID and resets when identity is lost or changes. Smoothing therefore cannot bridge two different anatomical identities.

## Diagnostics

Expected banner:

```text
[TRACK] PHASE 2B.8 RUNTIME ACTIVE | tracker=TEMPORAL-PALM-BRANCH-ID
```

Viewer diagnostics:

- **cyan circle + cyan plus** — current validated palm observation;
- **white cross** — current 2D candidate diagnostic when no final metric fingertip is available;
- a white cross during `ACQUIRING` is not yet an accepted fingertip.

Console reject reasons include:

- `palm-invalid`
- `palm-temporal-reject`
- `tip-jump-reject`
- `ambiguous-branch`
- `branch-association-reject`
- `identity-acquiring`
- `no-finger-like-branch`

## Synthetic regressions

Phase 2B.8 self-test currently covers:

- diagonal dominant index with distal appearance beyond dense support;
- horizontal dominant index;
- palm-core validation;
- finger width/linearity gates;
- long unsupported appearance tail removal;
- two similarly strong fingers remain ambiguous;
- tiny no-hand noise rejected;
- stable branch progresses through `ACQUIRING` to `LOCKED`;
- physical 2B.7 jump regression `263,173 -> 265,177 -> 189,87` becomes `UNKNOWN` on the bad jump;
- synthetic `stereo=HIGH` cannot override `identity=LOW`;
- sudden palm teleport is rejected.

These tests pass on x64 and Win32, but remain explicitly **SYNTHETIC ONLY**. Physical fingertip identity is still the merge gate.

## Lightweight modern landmark fallback

OpenCV Zoo / MediaPipe Handpose is documented as a possible independent 2D anatomical oracle/veto because it exposes 21 hand landmarks including the index fingertip.

It is deliberately **not** linked into the current physical Win32 runtime. The next evaluation, only if geometry still fails physically, should compare its LEFT-eye 2D index-tip hypothesis with the geometry candidate.

Even if a landmark model is adopted later:

- its Z is not Touch+ metric depth;
- Touch+ stereo/Q remains the metric XYZ source;
- strong geometry/model disagreement should favor `UNKNOWN`, not forced selection.

## Physical acceptance boundary

PR #9 remains **Draft / DO NOT MERGE** until real hardware shows all of the following:

1. learned empty scene remains no-hand;
2. palm diagnostic stays inside the actual palm;
3. one clearly extended index reaches `LOCKED` and keeps one branch ID through vertical, horizontal and diagonal motion;
4. old large tip jumps become `UNKNOWN` / explicit reject, not a new finite fingertip;
5. two comparable distal fingers may safely become ambiguous/unknown;
6. identity confidence and stereo confidence both pass before finite XYZ is emitted;
7. finite `(Xsurface,Ysurface,H)` stays attached to the same visible distal index;
8. lowering the index toward the work plane lowers `H`;
9. low texture or unstable identity becomes `UNKNOWN`, never an anatomically wrong finite/HIGH point.

Touch/click thresholds remain Phase 2C and must not be implemented before this hardware identity gate passes.
