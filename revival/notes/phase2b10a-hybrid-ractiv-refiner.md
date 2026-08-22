# Phase 2B.10A — hybrid Ractiv-style distal refiner diagnostic

Date: 2026-08-22

## Why this branch exists

The isolated `ractiv-recovery/main` experiment physically proved two different facts on the real Touch+:

1. the July 2015 Ractiv pipeline can still run and its full-resolution local fingertip refinement idea is useful when the coarse index identity is already correct;
2. Ractiv's own coarse index identity / `pose=point` is not reliable enough to own fingertip identity. It can accept the wrong digit coherently in both eyes and can miss an obvious pointing index.

Therefore the useful historical idea is **local distal refinement**, not the historical tracker as a whole.

This branch starts from exact accepted `revival/main`:

`b08f4c39b610a59aab0a72a7046f9c3ef96512f3`

Branch:

`revival/hybrid-ractiv-refiner`

## 2B.10A boundary

2B.10A is intentionally **diagnostic only**.

Accepted modern Phase 2B.9C.2 remains authoritative:

```text
learned background / silhouette
        -> V8 palm + persistent branch identity
        -> frame-synchronized 2D anatomy
        -> modern fusion publishes an accepted 2D fingertip
```

Only after that modern fusion publishes may the hybrid helper run:

```text
accepted modern fused 2D tip
        + modern palm
        + modern anatomy index axis
        + current LEFT grayscale
        + learned clean LEFT background
        + current modern hand mask
        -> small full-resolution local foreground window
        -> anchored component only
        -> distal cap along modern index axis
        -> GREEN diagnostic candidate
```

The helper is implemented in C++20 with no OpenCV 2.4 dependency. It ports the Ractiv *idea*, not the historical implementation.

## Hard ownership rules

For 2B.10A:

- the hybrid refiner **cannot create or reacquire identity**;
- it runs only when `FusedIdentityV9::publish == true`;
- disconnected neighboring blobs/fingers are rejected rather than re-identified;
- a rejected hybrid result falls closed to *no green diagnostic point*;
- the accepted Phase 2B.9C.2 fused pixel remains unchanged;
- the hybrid point does **not** feed the stereo matcher yet;
- Touch+ stereo/Q remains the only metric XYZ source;
- K/D/R/T/P/Q are unchanged;
- the validated surface frame is unchanged;
- no contact state machine is changed;
- no PointerMapper, mouse/touch injection, UDP cursor path, or OS output is added.

This diagnostic-only split is deliberate: physical evidence must show that the green point is materially more distal and no less anatomically safe before a later 2B.10B may move it into the pre-stereo path.

## Background

Pressing `B` starts the existing modern 30-frame clean-background learning and, in parallel, a separate 30-frame full-resolution LEFT grayscale background for the hybrid refiner.

This keeps the accepted V5 background implementation untouched while allowing a full-resolution local subtraction experiment.

## Synthetic regression

`touchplus_fingertip_refiner_selftest` covers:

- clean index-like component refines outward to its distal cap;
- a larger disconnected neighboring digit is not re-identified;
- disconnected foreground with no coarse/inward anchor is rejected;
- missing anatomy axis may fall back to the modern palm -> coarse ray;
- a proposed distal point without current modern silhouette support fails closed.

The existing V8 identity and V9 fusion self-tests remain unchanged and must still pass on x64 and Win32.

## Physical gate for 2B.10A

Only after CI and a local Win32 build pass should the real Touch+ be used.

Physical test expectation:

- cyan = modern palm;
- white = V8 geometry candidate;
- magenta = frame-synchronized modern anatomy candidate;
- green X = 2B.10A hybrid refined candidate.

The green point should move an already-correct modern candidate toward the visible distal fingertip. It must disappear rather than jump to another digit when the local evidence is not anchored/coherent.

Any evidence that the green point consistently degrades identity is a blocker and the experiment should be discarded without changing accepted Phase 2B.

## Explicitly deferred

A later **2B.10B** may feed the physically validated refined pixel into the existing modern stereo matcher. That is *not* part of this slice.

Phase 2C contact remains paused. OS input injection remains disabled.
