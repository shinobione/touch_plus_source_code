# Phase 2C.1H — shadow contact proxy

## Why this slice exists

The real 2C.1G.1 smoke showed that raw dense pre-support H is useful as an
**approach signal**, but not as a literal skin/table contact measurement.
Representative physically labelled values were approximately:

```text
HIGH      H_p25/H_median ~ 90–95 mm
NEAR      H_p25/H_median ~ 35–38 mm
CONTACT   H_p25/H_median ~ 15–23 mm on clean frames
```

Some contact frames also contained large negative `H_min`/`H_p25` values from
surface/edge contamination. Therefore `H_min` must not be used as a contact
criterion and the existing 6/4/8 mm authoritative contact thresholds must not
be raised to match the raw-dense body-of-finger height.

## Goal

Test, in shadow only, whether physical contact can be separated from low hover
by combining:

1. a robust low raw-dense H distribution (`H_p25` + `H_median`, not `H_min`);
2. recent downward approach in the dense H signal;
3. a short terminal plateau in both H and 2D fingertip motion;
4. a trusted provisional 2D target (`FUSED` or `ANATOMY`).

The operator HIGH/NEAR/CONTACT label is printed for physical evaluation but is
**never consumed by the decision**.

## First-smoke shadow constants

```text
minimum dense samples           = 8
candidate median H band         = 8..32 mm
candidate p25 H band            = 4..28 mm
candidate max (median-p25)      = 14 mm
minimum recent approach drop    = 14 mm
terminal plateau samples        = 6
terminal plateau max frame span = 10 frames
terminal H span                 <= 7.5 mm
terminal 2D motion              <= 14 px
confirmation                    = 3 consecutive frames
hold median H                   <= 36 mm
hold p25 H                      >= 0 mm
hold max (median-p25)           = 18 mm
release                         = 2 frames
```

These are diagnostic hypotheses, not accepted contact facts.

## Safety boundary

2C.1H does **not** change:

- accepted A/B selection or promotion;
- V8/V9 identity/fusion;
- anatomy sidecar;
- full-resolution stereo matcher;
- dense stereo construction;
- calibration or Q;
- accepted surface frame or ROI;
- authoritative fingertip smoothing;
- `ContactStateMachineV2C1` or its 6/4/8 mm thresholds;
- OS injection (remains disabled).

The runtime emits only:

```text
[CONTACT_SHADOW]
would_contact=YES|NO
event=NONE|WOULD_DOWN|WOULD_UP
reason=...
```

`GEOMETRY`-only provisional targeting cannot create or continue shadow contact.
A downgrade from a latched FUSED/ANATOMY shadow contact to GEOMETRY fails safe
with `WOULD_UP`.

## Synthetic regression

`touchplus_contact_proxy_shadow_v2c1h_selftest` covers:

- HIGH -> NEAR -> terminal CONTACT produces exactly one `WOULD_DOWN`;
- static low hover without recent approach never contacts;
- contaminated negative-p25 distributions never contact;
- GEOMETRY-only targeting never contacts;
- a latched contact releases exactly once on lift;
- GEOMETRY identity downgrade releases immediately.

## Physical gate

CI only grants permission to run the real Touch+ smoke. The physical test must
compare `[CONTACT_SHADOW]` against operator-labelled HIGH / NEAR / CONTACT and
check at minimum:

- no `WOULD_DOWN` during HIGH;
- no `WOULD_DOWN` during stable low hover / NEAR;
- exactly one `WOULD_DOWN` on a deliberate physical contact;
- `would_contact=YES` remains stable while held;
- exactly one `WOULD_UP` on lift or identity downgrade;
- no event in a no-hand scene.

PR #17 remains **DRAFT / DO NOT MERGE** until the real contact layer, not merely
this shadow proxy, passes its physical acceptance gate.
