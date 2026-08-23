# Phase 2B.10B — shadow stereo A/B

Date: 2026-08-23

## Status

Implementation complete on `revival/hybrid-ractiv-refiner`.

This slice is **shadow-only**. The accepted modern Phase 2B.9C.2 result remains authoritative and unchanged.

Physical smoke completed on the real Touch+ on 2026-08-23.

**Verdict: PHYSICAL PASS / PROMISING — B remains shadow-only; no promotion in this slice.**

Personal video/frames are not committed.

## Boundary

```text
A = modern fused tip
    -> accepted existing stereo path
    -> official raw/smoothed XYZ/H

B = Ractiv-style refined tip
    -> same robust stereo primitives in parallel
    -> raw XYZ/H telemetry only
```

B never replaces A, never updates the accepted smoothing state, never feeds contact, and never reaches OS output.

## Telemetry

The runtime reports:

- `A=VALID/INVALID` + existing stereo confidence/support;
- `B=VALID/INVALID` + shadow stereo confidence/support;
- raw A and B `XYZ/H` when available;
- `BminusA=(dX,dY,dH,dXYZ)` when both are valid;
- cumulative `both`, `A_only`, and `B_only` counts;
- explicit `authoritative=A metric_output=UNCHANGED OS_INJECTION=DISABLED` boundary text.

`B_only` is evidence only.

## Physical result — PASS / PROMISING

Observed end-of-run telemetry:

```text
refiner accepts / attempts = 30 / 62
shadow valid / attempted   = 28 / 30
both A+B valid             = 26
A_only                     = 1
B_only                     = 2
```

Representative useful case:

```text
coarse 307,154 -> refined 300,150
shift = 8.1 px

A = VALID / MEDIUM
support = 4
XYZ = (1.2, -54.7, H=74.6)

B = VALID / HIGH
support = 7
XYZ = (0.7, -54.5, H=75.8)

B - A: dXYZ ~= 1.4 mm, dH ~= +1.3 mm
```

This is encouraging because the refined distal pixel increased stereo support/confidence while remaining metrically coherent with A rather than producing a spurious depth jump.

Another representative case:

```text
coarse 285,186 -> refined 279,188
A H = 68.1 mm
B H = 68.2 mm
```

Here the 2D refinement mostly changed surface X while preserving H, which is consistent with moving toward the visible distal fingertip rather than hallucinating depth.

A rejected inward refinement produced `MOVED_TOWARD_PALM`, and B did not run for that candidate while A remained valid.

No obviously wrong finite MEDIUM/HIGH shadow fingertip was observed during this smoke. This does **not** authorize B to become authoritative; it only clears 2B.10B as a useful shadow experiment and justifies a later counterfactual promotion-gate slice.

## Exact-head CI

The 2B.10B implementation CI was green before the physical smoke. Subsequent documentation/governance commits do not alter the runtime boundary described above.

## Follow-up gate — 2B.10C counterfactual promotion selector

The next minimal slice should remain non-authoritative and produce only:

```text
KEEP_A
WOULD_SELECT_B
```

`WOULD_SELECT_B` is allowed only when A and B are both valid, modern identity remains accepted, and B strictly improves the confidence/support evidence without violating bounded 2D displacement or metric-coherence rules.

Hard rules for 2B.10C:

- `B_only` is always ineligible;
- UNKNOWN/stale identity is always ineligible;
- non-finite or excessive metric delta is always ineligible;
- runtime output, smoothing and official XYZ/H continue to use A only;
- any anatomically wrong finite candidate remains a BLOCKER;
- the later physical gate inspects only `WOULD_SELECT_B` cases.

Phase 2C remains paused. OS injection remains disabled.

The diagnostic-only 2B.10C implementation is documented in:

`revival/notes/phase2b10c-counterfactual-promotion-gate.md`
