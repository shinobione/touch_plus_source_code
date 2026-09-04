# Phase 2B.10M.2 — MediaPipe advisory axis + Ractiv-style distal refiner

Status: **ARCHIVED-DATASET OFFLINE COMPLETE / NO MATERIAL MEDIAPIPE AXIS ADVANTAGE / SHADOW-ONLY / DO NOT MERGE**

## Question

Phase 2B.10M proved that Google MediaPipe cannot own index identity: the original pointing set contained two visually confirmed wrong-finger cases.

Phase 2B.10M.1 then proved a narrower safety architecture on the same archived physical data:

- MediaPipe cannot rescue a rejected baseline;
- MediaPipe cannot publish a fingertip;
- only a 5→6→7→8 chain that agrees with the conservative 2B.9B.1 identity baseline may expose an advisory axis;
- the two known wrong-finger stress cases were neutralized.

M.2 asks the next narrow question:

> When M.1 accepts the MediaPipe axis, does that axis help the existing Ractiv-style local distal-refinement idea recover the conservative distal reference at least as well as the baseline anatomy axis?

## Important archived-data limitation

The archived image dataset does **not** contain the exact modern live A pixel, live V6 downscaled hand mask, live stereo support or frame state that `fingertip_refiner_v10.h` receives in the runtime.

Therefore M.2 is deliberately **not** presented as a bit-for-bit C++ runtime replay.

The offline tool uses:

- the existing 2B.9B.1 result as conservative identity;
- `index_tip_model` as the same proximal/coarse seed for both refiner runs;
- `GUIDED_DISTAL` as a **comparison reference**, not ground truth;
- the archived full-resolution learned-background primary hand component as the local support mask;
- a Python behavioral mirror of the accepted V10 bounded corridor/component/distal-cap logic and constants.

This isolates the value of the **axis** without claiming data we do not possess.

## Experiment

Only frames with:

```text
M.1 decision = ADVISORY_AXIS_ACCEPT
```

are allowed to enter M.2.

For each eligible frame:

```text
                         same proximal seed
                     2B.9B.1 index_tip_model
                              |
                 +------------+------------+
                 |                         |
                 v                         v
       baseline anatomy axis        MediaPipe advisory axis
                 |                         |
                 v                         v
       V10-style local refiner      V10-style local refiner
                 |                         |
                 +------------+------------+
                              |
                              v
                 compare both shadow points
                     to 2B.9B.1 GUIDED_DISTAL
```

The baseline `GUIDED_DISTAL` point stays unchanged. Neither refined point is authoritative.

## Safety gate

- any M.1 reject/no-rescue frame → `NOT_RUN_M1_GATE`;
- MediaPipe cannot create/reacquire identity;
- MediaPipe cannot rescue a missing baseline;
- known standalone wrong-finger cases must never enter the refiner;
- any visually wrong finite magenta MediaPipe-axis shadow candidate is a **hard fail**;
- distance to `GUIDED_DISTAL` is comparative evidence only and must not be called physical ground truth;
- no stereo, Q, surface `H`, Phase 2C or OS injection is run or modified.

## Archived physical dataset replay — 2026-09-04

Dataset: original 10-frame LEFT pointing set used by M and M.1.

Observed outcomes:

```text
NOT_RUN_M1_GATE                 : 4 / 10
MP_ONLY_SHADOW_ACCEPT           : 1 / 10
AXIS_PARITY_TO_BASELINE_REFERENCE: 3 / 10
MP_AXIS_REFINER_REJECT          : 2 / 10
```

Frame-level result:

```text
pair-001 : NOT_RUN_M1_GATE
pair-002 : MP_ONLY_SHADOW_ACCEPT
pair-003 : AXIS_PARITY_TO_BASELINE_REFERENCE
pair-004 : AXIS_PARITY_TO_BASELINE_REFERENCE
pair-005 : NOT_RUN_M1_GATE
pair-006 : NOT_RUN_M1_GATE
pair-007 : AXIS_PARITY_TO_BASELINE_REFERENCE
pair-008 : MP_AXIS_REFINER_REJECT
pair-009 : MP_AXIS_REFINER_REJECT
pair-010 : NOT_RUN_M1_GATE
```

### Safety-critical result

The two known standalone MediaPipe wrong-finger cases remained outside M.2:

- `pair-005` -> `NOT_RUN_M1_GATE`;
- `pair-010` -> `NOT_RUN_M1_GATE`.

No visually wrong finite MediaPipe-axis shadow candidate was observed in the M.2-eligible frames.

### Comparative result on eligible frames

`pair-003` produced the exact same refined pixel for both axes: `(581,131)`, about `2.24 px` from the conservative comparison reference.

`pair-004` produced two nearby refined pixels with identical `10.0 px` reference distance.

`pair-007` gave the MediaPipe-axis path only a sub-pixel-scale comparative edge: about `1.41 px` versus `2.24 px` from the comparison reference (`~0.82 px` gain). This is too small to establish a material advantage.

`pair-008` and `pair-009` rejected both local refiners as `NO_FOREGROUND`, so MediaPipe did not recover availability there.

`pair-002` is the only apparent MediaPipe-only acceptance. However, the legacy-axis path missed the V10 `31 px` maximum-shift threshold by only about `0.064 px` (`31.064 px`), while the MediaPipe-axis path landed exactly at `31.0 px`. The MediaPipe candidate still remained about `19 px` from the conservative comparison reference. This is therefore a threshold-edge artifact / weak signal, **not convincing evidence of a superior anatomical axis**.

## M.2 verdict

**SAFETY: PASS. MATERIAL VALUE: NOT DEMONSTRATED.**

The accepted M.1 gate successfully kept known wrong-finger MediaPipe cases away from the refiner, but the surviving MediaPipe axis did not demonstrate a meaningful improvement over the baseline anatomy axis on this archived set:

- 3 parity cases;
- 2 shared refiner failures;
- 1 nominal MediaPipe-only acceptance explained by a ~0.064 px threshold edge and still materially short of the comparison reference;
- no convincing multi-pixel accuracy win attributable to MediaPipe.

Therefore this dataset does **not** justify adding MediaPipe to the live Touch+ runtime merely as an extra refiner-axis dependency. The existing modern identity + Ractiv-style refiner path remains simpler and already has stronger live physical evidence.

MediaPipe may remain a diagnostic research option, but there is currently no evidence-backed reason to spend runtime complexity, frame-synchronization surface or maintenance cost on it.

## Tooling

- `revival/tools/touchplus_mediapipe_refiner_benchmark.py`
- `revival/tools/run-touchplus-mediapipe-refiner-benchmark.ps1`

Outputs stay under `%LOCALAPPDATA%\TouchPlus\MediaPipeBenchmark` by default.

Raw user captures and generated overlays remain local and are not committed.

## Project consequence

M/M.1/M.2 answer the MediaPipe question sufficiently for now:

1. standalone identity oracle — **FAIL**;
2. conservative advisory gate — **SAFETY PASS**;
3. advisory-axis refiner value — **NO MATERIAL ADVANTAGE DEMONSTRATED**.

Do not promote MediaPipe into the live runtime from these results. Any future revisit requires materially new evidence or a different role, not threshold tuning on this same dataset.
