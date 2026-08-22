# Phase 2B.10B — shadow stereo A/B

Date: 2026-08-23

## Status

Implementation complete on `revival/hybrid-ractiv-refiner`.

This slice is **shadow-only**. The accepted modern Phase 2B.9C.2 result remains authoritative and unchanged.

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

## Exact-head CI

Head:

`56ac40db29f339248d4772e0439cbdf54bd9e545`

All four workflows completed successfully:

- Revival Fingertip 3D #201 — SUCCESS
- Revival Windows Build #375 — SUCCESS
- Revival Surface Frame #219 — SUCCESS
- Revival Calibration Capture Kit #42 — SUCCESS

The Win32 Phase 2B job passed V8, V9, refiner self-test, full Revival build, Phase 2A surface regression, Phase 1C/Q regression, packaging and artifact upload.

## Artifact

GitHub artifact id: `9483864866`

Artifact source name (workflow still uses the older 2B.10A label):

`touchplus-phase2b10a-hybrid-refiner-windows`

SHA-256:

`06d279ed98368a67345dc561ce479b3a974d6c453836e6f632f5b66a100d6551`

The packaged EXE was checked to contain the 2B.10B shadow telemetry strings, including `2B.10B_SHADOW_AB`, `authoritative=A`, `B_only`, and `OS_INJECTION=DISABLED`.

## Physical gate

Use the same clean-background protocol as the successful 2B.10A retest:

1. clear the work area;
2. press `B`;
3. keep hands out until both modern and hybrid backgrounds report READY;
4. test left, right and diagonal index poses;
5. include a brief open-hand ambiguity case;
6. inspect A/B support, validity and raw XYZ/H.

Promotion remains blocked unless physical evidence shows that B improves useful stereo/metric placement without producing any wrong finite MEDIUM/HIGH fingertip.

Phase 2C remains paused. OS injection remains disabled.
