# Phase 2B.10D — gated authoritative promotion

Date: 2026-08-23

## Status

Implemented on `revival/phase2b10d-gated-promotion` as an experimental,
explicit-opt-in slice. Local synthetic regressions and the real Win32 runtime
build pass. Exact-head CI and the promotion-enabled physical smoke remain
required; this status is not a physical acceptance.

PR #14 merged the physically validated 2B.10A/10B/10C hybrid-refiner diagnostics into `revival/main` at:

`74e28c329256185eee543ae9d8b865f7788f0b54`

2B.10D is the first slice allowed to test B as an authoritative metric source, but only behind an explicit opt-in runtime gate and only on frames that already satisfy the physically validated 2B.10C selector.

## Objective

Add an explicit runtime promotion mode in which the existing 2B.10C decision can select the B refined fingertip as the authoritative same-frame metric sample.

Default behavior must remain identical to accepted Revival:

```text
promotion disabled -> authoritative source = A only
```

Experimental behavior must be opt-in:

```text
promotion enabled
    + 2B.10C decision = WOULD_SELECT_B
    + all A/B validity and coherence rules already passed
        -> authoritative same-frame raw fingertip = B
otherwise
        -> authoritative same-frame raw fingertip = A
```

## Hard constraints

- modern Phase 2B identity/fusion remains authoritative for which finger is the index;
- the Ractiv-style refiner may not create or reacquire identity;
- `B_only` remains ineligible;
- UNKNOWN/stale/non-current anatomy remains ineligible;
- rejected/inward refiner output remains ineligible;
- reuse the 2B.10C conservative gate; do not silently loosen its thresholds in this slice;
- A/B 2D displacement remains bounded by the 2B.10C limit;
- A/B metric coherence remains bounded by the 2B.10C limits;
- K/D/R/T/P/Q remain unchanged;
- accepted camera calibration remains unchanged;
- accepted surface frame remains unchanged;
- Phase 2C remains paused and untouched;
- PointerMapper / UDP / mouse / touch / OS injection remain disabled.

## Opt-in requirement

The promoted path must be disabled by default.

Use one explicit runtime control (CLI flag or equivalent) whose absence guarantees exact accepted-A behavior. The chosen control must be obvious in startup telemetry.

Suggested semantics:

```text
HYBRID_PROMOTION=DISABLED   # default
HYBRID_PROMOTION=ENABLED    # explicit bench-only opt-in
```

Do not make promotion sticky across launches.

Implemented runtime control:

```powershell
.\touchplus_phase2b_tracker.exe --enable-hybrid-promotion
```

Without `--enable-hybrid-promotion`, startup reports
`promotion_mode=DISABLED` and the accepted A result/smoothing path is not
mutated by the 2B.10D layer. The flag is process-local and is not persisted.

## Authoritative selection boundary

Promotion should occur only after A and B have independently completed their existing same-frame stereo evaluation and after `evaluate_promotion_gate_v10c(...)` returns `WOULD_SELECT_B`.

When promotion is disabled, the current accepted A raw/smoothed output path must remain byte-for-behavior equivalent apart from diagnostic logging.

When promotion is enabled and the gate selects B, keep one internally coherent source for that frame:

- selected 2D fingertip pixel = B refined pixel;
- selected raw `Xsurface / Ysurface / H` = B raw metric result;
- downstream smoothing may consume the selected metric sample only in enabled experimental mode;
- identity id and identity confidence remain the accepted modern identity, not a B-owned identity.

Do not mix A pixel with B metric XYZ/H or the reverse.

## Telemetry

Add explicit per-frame/source telemetry sufficient for physical review:

```text
promotion_mode=DISABLED|ENABLED
promotion_gate=KEEP_A|WOULD_SELECT_B
selected_source=A|B
selected_reason=...
A_confidence/support=...
B_confidence/support=...
shift_px=...
dXYZ=...
dH=...
```

Maintain cumulative counts:

```text
selected_A
selected_B
source_switches
```

A source switch is diagnostic evidence only; it must never reset/reacquire finger identity.

The runtime reports these counts cumulatively for the current refiner
background-learning session and resets them only when a new refiner background
capture is explicitly started.

## Implemented selection boundary

- `evaluate_promotion_gate_v10c(...)` and all 2B.10C thresholds are unchanged;
- source selection copies pixel, raw `Xsurface/Ysurface/H`, stereo confidence
  and support as one atomic A-or-B sample;
- OFF performs no write to the accepted `modern.result` or its accepted A
  smoothing path;
- ON uses a separate selected-sample smoother, reset on identity loss/change;
- B cannot create or reacquire identity because selection still requires the
  current published modern identity and the unchanged 2B.10C gate;
- OS injection remains compile-time disabled for this slice.

## Local verification

Passed in both x64 and Win32:

- V8 temporal palm/branch identity self-test;
- V9 / 2B.9C.2 frame-synchronous fusion self-test;
- 2B.10A distal refiner self-test;
- unchanged 2B.10C promotion-gate self-test;
- 2B.10D gated authoritative selection self-test.

Also passed:

- Phase 2A surface-frame regression in x64 and Win32;
- Phase 1C calibration/Q self-test in Win32 with promotion OFF and ON;
- full Win32 Revival build, including `touchplus_depth_viewer.exe`.

## Synthetic regression

At minimum cover:

1. promotion disabled + WOULD_SELECT_B -> selected A;
2. promotion enabled + WOULD_SELECT_B -> selected B;
3. promotion enabled + KEEP_A -> selected A;
4. `B_only` -> selected A;
5. UNKNOWN/stale/non-current identity -> selected A;
6. excessive/non-finite A/B delta -> selected A;
7. inward/rejected refiner -> selected A;
8. selected pixel and selected metric source always match;
9. Phase 2C / OS injection remains disabled.

Keep existing V8 identity, V9 fusion, 2B.10A refiner and 2B.10C gate regressions green in x64 and Win32.

## Physical gate

The first hardware smoke must run with promotion explicitly enabled and must review every `selected_source=B` interval.

PASS requires:

- B is selected only where the same physically validated 2B.10C rule would have emitted `WOULD_SELECT_B`;
- every selected B point is anatomically the real distal index fingertip;
- no wrong finite MEDIUM/HIGH promoted fingertip is observed;
- no large output jump is introduced at A<->B source switches;
- H remains metrically coherent with the surface frame;
- identity loss/ambiguity still fails closed;
- no contact or OS event is emitted.

Any anatomically wrong finite promoted B sample is a BLOCKER.

## Merge rule

Do not merge 2B.10D based on CI alone.

The branch stays Draft until exact-head CI passes and the explicit promotion-enabled physical smoke passes on the real Touch+.
