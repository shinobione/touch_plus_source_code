# Phase 2C.1C.1 — deferred pre-contact identity handoff

Status: **IMPLEMENTED / SYNTHETIC CI PENDING / PHYSICAL RETEST REQUIRED / NO OS INJECTION**

This slice addresses the integration blocker discovered in the physical Phase 2C.1C smoke. The 2D contact-occlusion proxy worked on the real Touch+, but the contact bridge did not arm because a valid near-surface metric sample arrived under a new Phase 2C.1B adapter identity after a transient cross-mode gap.

No personal hardware video or frame is committed. Only derived observations are recorded here.

## Physical regression that motivates this slice

Representative real-hardware sequence:

```text
adapter contact id 7
source = ANATOMY_ONLY
VALID metric H ~= 13.8 mm
        ↓
transient fail-closed gap
        ↓
source changes to GEOMETRY+ANATOMY
2C.1B correctly returns:
identity_alias = cross-mode-after-gap-reset
NEW adapter contact id 8
VALID metric H ~= 6.2 mm
        ↓
contact_bridge = DISARMED
precontact_valid = 1
        ↓
metric disappears at physical contact
occlusion_proxy = VALID
```

The blocker was therefore not the 2D occlusion proxy itself. The detector lost the older 13.8 mm metric sample because its contact identity changed from the old 2C.1B epoch to the new one. The 6.2 mm sample became the first metric sample of the new detector history, so the two-sample terminal descent required to arm the occlusion bridge was unavailable.

## Boundary

Phase 2C.1B remains authoritative for identity epochs. In particular:

```text
cross-mode-after-gap-reset
```

still creates a **new adapter contact identity**. This slice does not silently convert that transition into a normal 2C.1B alias.

Instead, 2C.1C.1 adds a separate contact-only mapping:

```text
2C.1B adapter identity epoch
        ↓
DeferredPrecontactIdentityHandoff
        ↓
detector contact identity
```

Only the detector identity may remain stable for one tightly-gated pre-contact handoff. This preserves the bounded metric history already earned before the gap while keeping the stricter 2C.1B epoch transition visible in telemetry.

## Handoff gate

A handoff is considered only when all of the following are true:

```text
identity reason               = cross-mode-after-gap-reset
old/new sources               = GEOMETRY+ANATOMY <-> ANATOMY_ONLY
both observations             = real VALID metric fingertips
last valid age                <= 4 semantic frames
current H                     <= 10 mm
terminal H drop               >= 5 mm
predicted next H              <= 2 mm
metric XY delta               <= 16 mm
2D fingertip delta            <= 22 px
raw-id source encoding        = valid
previous raw binding          = not contradicted
```

If accepted, the adapter identity still changes, but the detector continues to use the previous detector identity and therefore retains the old metric bridge history.

The intended physical case becomes:

```text
adapter id 7 / detector id 7 / H=13.8
        ↓ transient gap
adapter id 8 / detector id 7 / H=6.2
precontact_history=HANDOFF
precontact_valid=2
terminal_drop=7.6
predicted_H=-1.4
contact_bridge=ARMED
        ↓
occlusion_proxy=VALID
CONFIRMING
        ↓
second coherent proxy
TOUCH_DOWN
```

The invalid 2D frames still never supply H/XY and never increment `near_count`.

## Explicit non-handoff cases

The following remain hard boundaries and start a new detector identity / discard inherited bridge history:

```text
contact-identity-motion-reset
same-source-raw-id-switch
cross-mode bound raw-id contradiction
large metric XY delta
large 2D tip delta
insufficient terminal descent
current sample not near surface
stale old metric sample
NO_HAND
SURFACE_INVALID
TRACKING_DISABLED
```

This specifically keeps the more aggressive physical case around `H ~= 38.2 -> 5.9 mm` with `contact-identity-motion-reset` blocked instead of treating every low-H epoch change as continuity.

## Occlusion proxy ownership

After a deferred handoff, proxy contradiction checks use the raw geometry/anatomy bindings preserved by the handoff layer, not only the freshly-created 2C.1B adapter epoch. This prevents the history transfer from weakening the raw-id contradiction gates.

The special `TIP_NOT_CURRENT_DISTAL` contact-boundary proxy remains current-frame-only (`age=0`). A stale `TIP_NOT_CURRENT_DISTAL age>0` is still forbidden.

## Telemetry

Runtime now reports both identity layers:

```text
raw_identity_id=...
adapter_contact_identity_id=...
contact_identity_id=...
identity_alias=cross-mode-after-gap-reset
precontact_history=HANDOFF|NONE
handoff_reason=...
handoff_from_adapter_id=...
handoff_age=...
handoff_drop=...
handoff_predicted_H=...
handoff_xy=...
handoff_tip_delta=...
```

This makes it explicit that a new 2C.1B adapter epoch can coexist with a preserved detector pre-contact history.

## Hard ownership remains

```text
K/D/R/T/P/Q              = UNCHANGED
surface frame            = UNCHANGED
persistent capture       = UNCHANGED
Phase 2B identity V8/V9  = UNCHANGED
Phase 2B raw ids         = UNCHANGED
landmark sidecar role    = UNCHANGED
metric_z_source          = TOUCHPLUS_STEREO_ONLY
stereo matcher           = UNCHANGED
DOWN / RELEASE thresholds= UNCHANGED
candidate XY gate        = UNCHANGED
OS injection             = DISABLED
```

## Synthetic regressions

The dedicated 2C.1C.1 self-test covers:

- exact `13.8 -> gap -> cross-mode-after-gap-reset -> 6.2` physical class;
- 2C.1B adapter identity must change while detector identity remains stable only for the safe handoff;
- resulting history must expose two real metric pre-contact samples and arm the occlusion bridge;
- two coherent non-metric proxies then produce exactly one DOWN;
- metric reacquisition becomes HELD and lift produces exactly one UP;
- `contact-identity-motion-reset` never handoffs;
- large 2D tip jump never handoffs;
- same-source raw-id switch never handoffs;
- `NO_HAND` destroys handoff memory;
- a cross-gap sample still above the near-surface band never handoffs.

Synthetic CI is permission to retest hardware only.

## Physical acceptance target

The next real Touch+ smoke should reproduce at least one clean sequence similar to:

```text
identity_alias=cross-mode-after-gap-reset
adapter_contact_identity_id changes
contact_identity_id remains stable
precontact_history=HANDOFF
precontact_valid=2
contact_bridge=ARMED
occlusion_proxy=VALID
contact_bridge=CONFIRMING
TOUCH_DOWN / event=DOWN
TOUCH_HELD
TOUCH_UP / event=UP
```

Any false semantic DOWN remains a blocker. PR #10 stays Draft / non-merged until reliable physical `DOWN / HELD / UP` is demonstrated. Windows injection remains a later Phase 2C boundary.
