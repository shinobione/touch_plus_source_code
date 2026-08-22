# Phase 2B.10A / 2B.10B — hybrid Ractiv-style distal refiner

Dates: 2026-08-22 to 2026-08-23

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

The helper is C++20-only with no OpenCV 2.4 dependency. It ports the Ractiv *idea*, not the historical implementation.

## 2B.10A physical result — PASS

A clean-background physical retest on the real Touch+ was completed on 2026-08-23. Personal video/frames are not committed.

Observed telemetry over the smoke:

```text
refiner attempts = 94
refiner accepts  = 62
accept rate      ~= 66%
```

Representative accepted refinements included:

```text
coarse 369,178 -> refined 359,188 | shift 14.1 px | forward 10.1 px
coarse 190,135 -> refined 184,140 | shift  7.8 px | forward  7.3 px
coarse 565,203 -> refined 564,204 | already-near-distal minimal correction
```

A representative inward/toward-palm candidate was rejected with `MOVED_TOWARD_PALM` rather than published.

Visual review found the accepted green candidate materially more distal on multiple pointing poses, with no obvious accepted jump to another digit in the clean-background retest. Modern identity still remained the authority and could independently fail closed to UNKNOWN.

Verdict:

**2B.10A PHYSICAL PASS — the Ractiv-style local distal-refinement concept is useful when driven by accepted Revival identity.**

## 2B.10B boundary — shadow A/B stereo only

2B.10B deliberately does **not** promote the refined pixel into the official runtime path yet.

Instead:

```text
A = accepted modern fused tip
        -> existing accepted stereo path
        -> official raw/smoothed XYZ/H

B = accepted hybrid refined tip
        -> same robust Touch+ stereo primitives in parallel
        -> SHADOW raw XYZ/H telemetry only
```

A remains authoritative even when B is valid and A is invalid.

The shadow evaluator records:

- A valid/invalid + existing stereo confidence/support;
- B valid/invalid + shadow stereo confidence/support;
- B raw surface XYZ/H;
- `B - A` raw metric delta when both are valid;
- cumulative `both`, `A_only`, and `B_only` counters.

A `B_only` result is evidence for later evaluation only. It cannot create a finished fingertip, change smoothing, feed contact, or affect any runtime output in this slice.

## Hard ownership rules

For 2B.10A/10B:

- hybrid refinement cannot create or reacquire identity;
- it runs only when `FusedIdentityV9::publish == true`;
- disconnected neighboring blobs/fingers are rejected rather than re-identified;
- a rejected hybrid result falls closed;
- the accepted Phase 2B.9C.2 fused pixel remains unchanged;
- A remains the sole authoritative metric fingertip;
- B stereo/XYZ is shadow telemetry only;
- Touch+ stereo/Q remains the only metric XYZ source for both A and shadow B;
- K/D/R/T/P/Q are unchanged;
- the validated surface frame is unchanged;
- no contact state machine is changed;
- no PointerMapper, mouse/touch injection, UDP cursor path, or OS output is added.

## Background

Pressing `B` starts the existing modern 30-frame clean-background learning and, in parallel, a separate 30-frame full-resolution LEFT grayscale background for the hybrid refiner.

The work area must remain clear until both backgrounds report READY.

## Synthetic regression

`touchplus_fingertip_refiner_selftest` covers:

- clean index-like component refines outward to its distal cap;
- a larger disconnected neighboring digit is not re-identified;
- disconnected foreground with no coarse/inward anchor is rejected;
- missing anatomy axis may fall back to the modern palm -> coarse ray;
- a proposed distal point without current modern silhouette support fails closed.

The existing V8 identity and V9 fusion self-tests remain unchanged and must still pass on x64 and Win32. The full Win32 runtime build is the compile gate for the shadow stereo integration because 2B.10B intentionally reuses the existing production stereo primitives rather than introducing a second independent matcher implementation.

## Physical gate for 2B.10B

After exact-head CI passes, run the same clean-background pointing smoke and compare A/B telemetry.

Promotion remains blocked until physical evidence shows that B:

- preserves the zero-wrong-finite safety rule;
- does not reduce stereo support/validity materially;
- improves distal metric placement or useful B-only recovery;
- remains stable across left/right/diagonal pointing and an open-hand ambiguity case.

Only after that evidence may a later promotion slice consider replacing A's pre-stereo pixel.

Phase 2C contact remains paused. OS input injection remains disabled.
