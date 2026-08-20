# Phase 2B — Hand / Fingertip 3D

## Current slice: Phase 2B.7 — SCOPA-inspired palm-core finger branch

Physical unit: `0101007379`.

Phase 2A already proved a useful working-surface frame on real hardware: bare table near `H=0`, 53 mm book measured about 54–55 mm. Phase 2B sits on top of that accepted metric stack; it does not modify camera calibration or the saved surface transform.

## Physical progression

1. **2B.1 wiring PASS** — tracker runtime, `T`, surface-model load and heartbeat work.
2. **2B.1 foreground FAIL** — real scene produced ~19k–26k-cell pseudo-hands.
3. **2B.2 hardened segmentation PARTIAL PASS** — stronger dense-depth gates removed the giant 20k-cell continent; real components dropped to hundreds/few-thousands and full-res XYZ could succeed.
4. **2B.2 fingertip identity FAIL** — finite candidates landed on wrist/back-of-hand.
5. **2B.3 top-entry geodesic FAIL** — no-hand pseudo-hands and jumping coarse endpoints remained.
6. **2B.4 learned background PARTIAL PASS** — physical video proved `NOT_READY -> LEARNING -> READY` works and reduced no-hand components to roughly 40–110 cells instead of hundreds/thousands. However the visible distal index was still not the tracked pixel because the hand mask required reliable half-resolution dense disparity all the way to distal skin.
7. **2B.5 appearance silhouette PARTIAL PASS / fingertip FAIL** — the yellow silhouette can cover the full hand and low-texture distal finger, but `tip_pixel` still jumped inside the palm/proximal finger and toward connected photometric tails.
8. **2B.6 support-bounded skeleton PARTIAL PASS / fingertip FAIL** — ambiguity handling improved, but the latest physical benchmark still emitted anatomically wrong single-index candidates and could attach HIGH confidence to them. Representative failure: `tip_pixel=389,201 | support=8 | confidence=HIGH` while the visible distal index was substantially lower/left. Horizontal/diagonal poses also produced proximal candidates (`415,283`, `441,244`, etc.). Wrong finite/HIGH identity is a hard blocker.

## Why 2B.7 changes the identity model

The recovered Ractiv SCOPA implementation did **not** equate the longest wrist-rooted skeleton branch with the index. It estimated `palm_point` / `palm_radius` using an interior distance transform, used that palm geometry to separate arm/palm from distal structure, then continued into contour/pose labeling before producing explicit finger points. The recovered `HandResolver` then refined those coarse labeled points locally against the learned background at higher resolution.

2B.7 keeps the useful anatomical principle — **find the palm first, then reason about finger branches outside it** — without importing the legacy OpenCV pose/DTW stack.

## Phase 2B.7 architecture

The accepted preprocessing remains unchanged:

1. `B` learns a clean grayscale background;
2. V5 appearance change produces a 2D hand silhouette so low-texture distal skin remains visible;
3. V6 physical support bounding trims long appearance-only tails using current above-plane stereo support.

Identity is then replaced by a palm-centric pipeline:

1. compute an 8-neighbour chamfer distance-to-boundary map inside the bounded hand silhouette;
2. estimate the **palm core** as the largest interior region below the top-entry forearm;
3. convert that interior distance into `palm_center + palm_radius`;
4. skeletonize the bounded hand mask;
5. remove the palm disk from the skeleton so the remaining external components represent forearm/finger branches;
6. explicitly reject the branch that reconnects to the top-entry band as **forearm**;
7. require one sufficiently long external branch attached to the palm;
8. if two distal branches have near-equal length, return **ambiguous / unknown**;
9. extend the winning branch direction to the visible silhouette boundary;
10. only then run the proven full-resolution NCC + LEFT↔RIGHT matcher and Q/surface transform for metric `(Xsurface,Ysurface,H)`.

This remains deliberately a controlled boundary: one top-entry hand with one clearly dominant extended index. It is not a general hand-pose recognizer.

## Diagnostics

Expected banner:

```text
[TRACK] PHASE 2B.7 RUNTIME ACTIVE | tracker=PALM-CORE-BRANCH
```

Viewer diagnostics:

- **cyan circle + cyan plus** — estimated palm core;
- **white cross** — selected fingertip identity when metric refinement is not yet valid;
- console reports `palm=x,y r=... | branches=N` on each heartbeat.

These diagnostics deliberately separate three physical failure classes: wrong palm, wrong finger branch, or correct 2D identity but insufficient stereo refinement.

## Synthetic regressions

2B.7 must cover more than the V6 diagonal-only success case:

- diagonal dominant index with distal appearance but missing dense support;
- long connected appearance-only tail;
- top-entry forearm must be explicitly excluded;
- **horizontal dominant index** (direct regression for the latest physical benchmark);
- two similarly long fingers must become ambiguous;
- tiny no-hand noise must remain rejected.

CI synthetic PASS remains necessary but is explicitly **not sufficient**. Physical fingertip identity is the merge gate.

## Runtime controls

- `B` — learn/relearn clean background;
- `T` — enable/disable Phase 2B tracker;
- existing Phase 1C/2A controls remain unchanged.

## Acceptance boundary

PR #9 remains **DO NOT MERGE** until real hardware shows all of the following:

1. clear scene after background learning produces no persistent accepted hand;
2. one top-entry hand produces a compact supported silhouette;
3. cyan palm core stays inside the actual palm rather than wrist/finger/background;
4. white `tip_pixel` cross stays on the visible distal index for vertical, horizontal and diagonal poses;
5. two comparable distal fingers may return `ambiguous/unknown` rather than arbitrary selection;
6. finite `(Xsurface,Ysurface,H)` remains attached to that same fingertip and moves continuously;
7. lowering the index toward the work plane lowers `H`;
8. low texture degrades to `unknown`, never an anatomically wrong finite/HIGH point.

Touch/click thresholds remain Phase 2C.
