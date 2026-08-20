# Phase 2B — Hand / Fingertip 3D

## Current slice: Phase 2B.6 — support-bounded skeleton fingertip

Physical unit: `0101007379`.

Phase 2A already proved a useful working-surface frame on real hardware: bare table near `H=0`, 53 mm book measured about 54–55 mm. Phase 2B sits on top of that accepted metric stack; it does not modify camera calibration or the saved surface transform.

## Physical progression

1. **2B.1 wiring PASS** — tracker runtime, `T`, surface-model load and heartbeat work.
2. **2B.1 foreground FAIL** — real scene produced ~19k–26k-cell pseudo-hands.
3. **2B.2 hardened segmentation PARTIAL PASS** — stronger dense-depth gates removed the giant 20k-cell continent; real components dropped to hundreds/few-thousands and full-res XYZ could succeed.
4. **2B.2 fingertip identity FAIL** — finite candidates landed on wrist/back-of-hand.
5. **2B.3 top-entry geodesic FAIL** — no-hand pseudo-hands and jumping coarse endpoints remained.
6. **2B.4 learned background PARTIAL PASS** — `NOT_READY -> LEARNING -> READY` works and no-hand residual components dropped to roughly 40–110 cells, but depth-supported palm/proximal pixels still won over the visible distal index.
7. **2B.5 appearance silhouette PARTIAL PASS / fingertip FAIL** — the real benchmark proved the yellow appearance silhouette can cover the full hand and distal finger even when dense depth is missing there. However `tip_pixel` still jumped inside the palm/proximal finger and occasionally toward long photometric tails/frame-edge directions. During a clear diagonal extended-index segment, finite telemetry around `tip_pixel≈397,185` then `433,191` remained substantially before the visible distal fingertip. The benchmark also produced very large changed silhouettes (up to ~17k cells), consistent with connected shadow/illumination tails. Therefore “appearance silhouette + score every pixel” is still not anatomically sufficient.

## Phase 2B.6 correction

2B.6 keeps the successful learned-background / appearance-silhouette architecture from 2B.5, but changes the final identity stage instead of tuning another y/radius coefficient.

### 1. Preserve V5 learned background and initial silhouette

`B` still learns 30 clean grayscale + dense-depth frames. V5 still produces the initial supported appearance component. This preserves the physically useful background rejection already demonstrated on hardware.

### 2. Bound appearance-only extensions around physical 3D support

Inside the selected V5 silhouette, V6 rebuilds a robust current dense-depth support core from points that reconstruct plausibly above the accepted work plane. Appearance-only cells are then allowed only within a bounded geodesic distance from that physical support.

This deliberately keeps a low-texture distal fingertip that may be tens of half-resolution cells beyond its last valid depth sample, while trimming long connected shadows / exposure-change tails that can otherwise run toward the frame edge.

After this trim, V6 re-selects one supported top-entry component.

### 3. Skeletonize the hand and consider endpoints only

The support-bounded silhouette is thinned with a Zhang–Suen skeleton. The top-entry wrist band seeds graph distance along that skeleton.

Unlike 2B.5, **interior silhouette pixels are no longer fingertip candidates**. Only distal skeleton endpoints can win. The primary signal is geodesic branch length from the wrist; radial displacement allows diagonal fingers to compete; downward image position is only a weak tie-breaker.

The winning skeleton endpoint is extended along its outgoing branch direction to the visible silhouette boundary before stereo refinement.

### 4. Reject multi-finger ambiguity

If two spatially separated distal skeleton branches have near-equal geodesic/score evidence, V6 returns `unknown` instead of silently choosing one arbitrary finger. This is intentional: the current boundary is one clearly dominant extended index, not arbitrary multi-finger pose understanding.

### 5. Metric depth stays conservative

Only after the 2D skeleton fingertip is identified does the proven full-resolution NCC + LEFT↔RIGHT matcher estimate `(Xsurface,Ysurface,H)` in a small neighborhood. Missing texture may still return `unknown`; anatomically wrong finite points remain blockers.

## Synthetic regression

The 2B.6 self-test now models the two failure modes visible in the real benchmark:

- one index extends **diagonally** down-right;
- its distal section has appearance but no dense-depth support;
- a long connected appearance-only shadow/tail runs down-left from the palm;
- a second short folded branch exists;
- a separate two-equal-long-fingers case must be rejected as ambiguous.

Current x64 CI result:

```text
bounded hand cells       : 5954
support cells            : 3609
skeleton cells           : 288
endpoint candidates      : 3
skeleton tip grid        : 225,202
geodesic steps           : 154
far shadow removed       : 1
diagonal tip recovered   : 1
equal branches ambiguous : 1
small noise rejected     : 1
PHASE 2B.6 ... PASS
```

## Runtime controls

- `B` — learn/relearn 30 clean background frames;
- `T` — enable/disable Phase 2B tracker;
- existing Phase 1C/2A controls remain unchanged.

Expected banner:

```text
[TRACK] PHASE 2B.6 RUNTIME ACTIVE | tracker=SUPPORT-SKELETON
```

When no unique endpoint exists, telemetry explicitly reports an ambiguous/no-dominant skeleton endpoint rather than drawing a misleading coarse cross.

## Acceptance boundary

Do not merge PR #9 until real hardware shows all of the following:

1. clear scene after background learning produces no persistent accepted hand;
2. one top-entry hand with one clearly dominant extended index yields a support-bounded silhouette without long shadow/frame-edge tails;
3. `tip_pixel` / white diagnostic cross stays on the visible distal index for vertical **and diagonal** index poses;
4. splayed / near-equal distal branches may become `unknown` rather than arbitrary-finger output;
5. finite `(Xsurface,Ysurface,H)` remains attached to the same fingertip and moves continuously;
6. lowering the index toward the work plane lowers `H`;
7. low texture degrades to `unknown`, not an anatomically wrong finite point.

Touch/click thresholds remain Phase 2C.
