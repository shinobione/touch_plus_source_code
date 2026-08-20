# Phase 2B.9A — offline landmark oracle evaluation

Date: 2026-08-20  
Physical unit: `0101007379`

## Verdict

**MODEL RUNTIME PASS / EXACT INDEX-TIP ORACLE FAIL / DO NOT INTEGRATE LANDMARK 8 AS A HARD VETO**

The OpenCV Zoo MediaPipe palm + hand-pose stack runs correctly on Touch+ captures and often recognizes the hand/index anatomy, but the raw `INDEX_FINGER_TIP` landmark is **not reliable enough as an exact distal-pixel oracle** on this sensor.

Raw personal imagery remains outside the repository. This note records only derived engineering observations from the user's local 2B.9A dataset.

## Why this boundary existed

Phase 2B.8 physically proved two important things:

1. the temporal geometry safety gate can reject implausible identity changes before stereo;
2. a correct index lock is possible, but geometry-only recall and continuity remain too intermittent.

2B.9A therefore asked whether a modern anatomical model could provide an independent exact 2D index-tip oracle without entering the stable Win32 Etron runtime.

## Candidate tested

The probe uses the official OpenCV Zoo MediaPipe models:

- `palm_detection_mediapipe_2023feb.onnx`
- `handpose_estimation_mediapipe_2023feb.onnx`

plus the official OpenCV Zoo Python pre/post-processing modules.

The HandPose model estimates 21 hand landmarks after palm detection.

## Hard architecture rules retained

The landmark probe:

- consumes local LEFT-eye images for the decision boundary;
- never supplies Touch+ metric Z;
- never modifies `K/D/R/T/P/Q`;
- never modifies the accepted surface frame;
- never loosens the robust stereo matcher;
- does not run inside the Win32 Etron process in 2B.9A.

Touch+ stereo/Q remains the only metric XYZ source.

## Physical 2B.9A dataset result

The binding subset is the **10 LEFT-eye captures**.

Derived probe statistics:

```text
LEFT frames evaluated          : 10
hand landmarks found           : 8 / 10
index_extended_2d              : 7 / 8 detected hands
ORACLE_NON_INDEX_POSE          : 1 / 10
ORACLE_UNAVAILABLE             : 2 / 10
median detected hand confidence: 0.9883
minimum detected confidence    : 0.9271
```

The raw detection numbers look encouraging, but visual anatomical review is the binding gate.

### Confident exact-tip failures

Representative observations:

- pair 001: hand confidence about `0.988`; index recognized, but landmark 8 is visibly **too proximal** on the index rather than at the distal fingertip;
- pair 002: hand confidence about `0.998`; same failure class despite extremely high confidence;
- pair 003: a visually extended index can become `ORACLE_NON_INDEX_POSE`;
- pairs 004/005: index direction is often plausible, while the raw tip remains materially short of the visible distal boundary;
- pairs 007–009 are better and can land near the true distal tip, but not consistently enough to satisfy the gate.

Therefore:

> **model confidence != exact distal-tip correctness**

This is intentionally treated as the same class of engineering lesson learned earlier with stereo confidence: a subsystem can be internally confident while solving the wrong boundary for the product.

## Why the original 2B.9A veto policy is retired

The original candidate policy was:

```text
geometry tip near landmark 8
    -> strengthen identity

geometry tip far from landmark 8
    -> veto to UNKNOWN
```

The physical dataset disproves that policy.

A correct Touch+ geometry tip could be far from a **confident but proximal** landmark 8. Using raw landmark-tip distance as a hard veto could therefore reject the better candidate.

From this point forward:

- raw landmark 8 is diagnostic only;
- exact-tip distance is not allowed to veto V8 identity;
- high hand-pose confidence cannot by itself publish or reject a fingertip;
- model-relative Z remains ignored.

## What 2B.9A *did* prove useful

The model often appears substantially more useful for **index anatomy and distal direction** than for the exact terminal pixel.

The promising information is the chain:

```text
INDEX_MCP -> INDEX_PIP -> INDEX_DIP -> approximate INDEX_TIP direction
```

Even when landmark 8 is too proximal, the model can still identify which anatomical branch is the index and which direction is distal.

That motivates a different next slice rather than abandoning landmarks entirely.

## Next boundary: Phase 2B.9B — landmark-guided distal projection

2B.9B does **not** trust landmark 8 as the endpoint.

Instead:

```text
MediaPipe index anatomy
MCP -> PIP -> DIP -> distal direction
             |
             v
Touch+ learned-background appearance silhouette
             |
             v
project along the anatomical distal corridor
             |
             v
real visible silhouette boundary
             |
             v
GUIDED 2D distal candidate
```

The DNN supplies **anatomical direction**. The Touch+ image silhouette supplies the **actual visible distal boundary**.

Only after that concept passes physical evaluation should a later live integration be considered.

## 2B.9A acceptance-gate result

Original gate:

> at least 8/10 clearly visible single-index LEFT frames with landmark 8 anatomically on the real distal tip.

Result: **FAIL**.

The model runtime itself is healthy, so the result is not an infrastructure failure. It is a domain/endpoint-representation failure.

## Merge rule

PR #9 remains **Draft / DO NOT MERGE**.

Phase 2C touch/click remains blocked until fingertip identity is physically reliable.