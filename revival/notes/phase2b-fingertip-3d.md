# Phase 2B — Hand / Fingertip 3D

## Current slice: Phase 2B.7 — SCOPA-inspired palm-core finger branch

Physical unit: `0101007379`.

Current physical status: **2B.7 PARTIAL PASS / FINGERTIP IDENTITY FAIL / DO NOT MERGE**.

Phase 2A already proved a useful working-surface frame on real hardware: bare table near `H=0`, 53 mm book measured about 54–55 mm. Phase 2B sits on top of that accepted metric stack; it does not modify camera calibration or the saved surface transform.

Detailed latest physical smoke note: `revival/notes/phase2b7-physical-smoke-2026-08-20.md`.

## Physical progression

1. **2B.1 wiring PASS** — tracker runtime, `T`, surface-model load and heartbeat work.
2. **2B.1 foreground FAIL** — real scene produced ~19k–26k-cell pseudo-hands.
3. **2B.2 hardened segmentation PARTIAL PASS** — stronger dense-depth gates removed the giant 20k-cell continent; real components dropped to hundreds/few-thousands and full-res XYZ could succeed.
4. **2B.2 fingertip identity FAIL** — finite candidates landed on wrist/back-of-hand.
5. **2B.3 top-entry geodesic FAIL** — no-hand pseudo-hands and jumping coarse endpoints remained.
6. **2B.4 learned background PARTIAL PASS** — physical video proved `NOT_READY -> LEARNING -> READY` works and reduced no-hand components to roughly 40–110 cells instead of hundreds/thousands. However the visible distal index was still not the tracked pixel because the hand mask required reliable half-resolution dense disparity all the way to distal skin.
7. **2B.5 appearance silhouette PARTIAL PASS / fingertip FAIL** — the yellow silhouette can cover the full hand and low-texture distal finger, but `tip_pixel` still jumped inside the palm/proximal finger and toward connected photometric tails.
8. **2B.6 support-bounded skeleton PARTIAL PASS / fingertip FAIL** — ambiguity handling improved, but the physical benchmark still emitted anatomically wrong single-index candidates and could attach HIGH confidence to them. Representative failure: `tip_pixel=389,201 | support=8 | confidence=HIGH` while the visible distal index was substantially lower/left. Horizontal/diagonal poses also produced proximal candidates (`415,283`, `441,244`, etc.). Wrong finite/HIGH identity is a hard blocker.
9. **2B.7 palm-core branch PARTIAL PASS / fingertip FAIL** — learned background and no-hand rejection remain good, and the new palm/branch diagnostics are useful, but one clearly extended index still does not retain one stable anatomical identity. In the 2026-08-20 physical bench, adjacent single-index frames can jump among substantially different `tip_pixel` values while still reporting MEDIUM/HIGH stereo confidence. Representative sequences include `231,193 -> 257,207 -> 373,245 -> 243,167` and later `263,173 -> 265,177 -> 189,87`. Multi-branch ambiguity can correctly degrade to unknown, but single-index identity is still not mergeable.

## Why 2B.7 changed the identity model

The recovered Ractiv SCOPA implementation did **not** equate the longest wrist-rooted skeleton branch with the index. It estimated `palm_point` / `palm_radius` using an interior distance transform, used that palm geometry to separate arm/palm from distal structure, then continued into contour/pose labeling before producing explicit finger points. The recovered `HandResolver` then refined those coarse labeled points locally against the learned background at higher resolution.

2B.7 keeps the useful anatomical principle — **find the palm first, then reason about finger branches outside it** — without importing the legacy OpenCV pose/DTW stack.

## Phase 2B.7 architecture

The accepted preprocessing remains unchanged:

1. `B` learns a clean grayscale background;
2. V5 appearance change produces a 2D hand silhouette so low-texture distal skin remains visible;
3. V6 physical support bounding trims long appearance-only tails using current above-plane stereo support.

Identity then uses a palm-centric pipeline:

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

2B.7 covers more than the V6 diagonal-only success case:

- diagonal dominant index with distal appearance but missing dense support;
- long connected appearance-only tail;
- top-entry forearm explicitly excluded;
- **horizontal dominant index**;
- two similarly long fingers become ambiguous;
- tiny no-hand noise remains rejected.

The synthetic test passes on x64 and Win32, but is explicitly **synthetic only**. Physical fingertip identity remains the merge gate.

## Latest physical bench — 2026-08-20

What passed:

- `B` background learning works;
- an empty learned scene can remain `background=READY | no palm-supported hand | changed_cells=0`;
- the supported appearance silhouette follows the hand much better than early Phase 2B revisions;
- the new palm/branch telemetry is useful;
- multi-branch cases can return `ambiguous/palm-branches` or `no-dominant-palm-branch` instead of forcing an answer.

What failed:

- the palm/branch identity stage still re-elects different candidates too aggressively;
- a stable single-index pose can produce materially different `tip_pixel` locations in adjacent heartbeats;
- strong stereo support can still produce `confidence=HIGH` for a candidate whose **2D anatomical identity is not trustworthy**;
- therefore MEDIUM/HIGH currently means only “the stereo matcher likes this selected pixel,” not “this pixel is certainly the distal index.”

This distinction is critical: metric refinement is downstream of identity and cannot repair a wrong 2D branch selection.

## Next design requirement

Do **not** fix 2B.7 by loosening stereo gates or only retuning branch weights.

The next identity iteration should preserve camera calibration, depth, surface frame, learned background, appearance silhouette and physical support bounding, while adding stronger identity constraints:

1. validate the palm core against plausible hand geometry before branch scoring;
2. keep a short temporal palm track instead of solving palm center independently every frame;
3. keep persistent branch identity over adjacent frames;
4. require finger-like branch width/length when leaving the palm boundary;
5. reject large 2D fingertip jumps unless explained by matching palm/hand motion;
6. separate **identity confidence** from **stereo refinement confidence**;
7. return `unknown` whenever identity is unstable, even if stereo support is excellent;
8. compare this controlled geometry path with a lightweight modern 2D hand-landmark fallback before accumulating more ad-hoc endpoint heuristics.

Temporal smoothing must only stabilize an already plausible anatomical candidate; it must never hide a wrong branch choice.

## Runtime controls

- `B` — learn/relearn clean background;
- `T` — enable/disable Phase 2B tracker;
- existing Phase 1C/2A controls remain unchanged.

## Acceptance boundary

PR #9 remains **DO NOT MERGE** until real hardware shows all of the following:

1. clear scene after background learning produces no persistent accepted hand;
2. one top-entry hand produces a compact supported silhouette;
3. palm core stays inside the actual palm rather than wrist/finger/background;
4. `tip_pixel` stays on the visible distal index for vertical, horizontal and diagonal poses;
5. two comparable distal fingers may return `ambiguous/unknown` rather than arbitrary selection;
6. finite `(Xsurface,Ysurface,H)` remains attached to that same fingertip and moves continuously;
7. lowering the index toward the work plane lowers `H`;
8. low texture or unstable identity degrades to `unknown`, never an anatomically wrong finite/HIGH point.

Touch/click thresholds remain Phase 2C.
