# Phase 2C.1C — contact occlusion bridge

Status: **IMPLEMENTED / SYNTHETIC CI PENDING / PHYSICAL RETEST REQUIRED / NO OS INJECTION**

Phase 2C.1C addresses the real-hardware “last centimetre” failure class observed after Phase 2C.1B passed its cross-fusion identity boundary.

No personal hardware video or frame is committed. This note records derived observations only.

## Physical regression that motivates this slice

On the real Touch+ with one diagonal index approaching the validated working surface, the accepted upstream stack produced a trajectory approximately like:

```text
VALID metric H ~= 30 mm
VALID metric H ~= 21.5 mm
short fail-closed gaps
VALID metric H ~= 6.3 mm
physical fingertip reaches / remains on the surface
metric fingertip becomes unavailable while the hand remains visually stable
...
metric returns on lift around H ~= 19.3 mm
then H rises above 50 mm
```

During the actual contact hold, Phase 2B correctly preferred `UNKNOWN` over publishing an anatomically uncertain fingertip. Representative upstream reasons included `STEREO_LOW`, `ANATOMY_REJECT`, `IDENTITY_UNKNOWN`, `geometry-jump-reject`, and `geometry-anatomy-disagree`.

The run did **not** demonstrate that the existing 16 mm candidate XY gate was the root cause. During the stable physical hold the semantic detector usually did not reach that gate at all because the fingertip metric stream disappeared first.

Therefore 2C.1C does **not** loosen the XY gate and does **not** modify Phase 2B.

## Ownership boundary

Unchanged and still authoritative:

```text
camera K/D/R/T/P/Q        = UNCHANGED
working surface frame     = UNCHANGED
persistent capture        = UNCHANGED
Phase 2B identity gates   = UNCHANGED
landmark sidecar role     = UNCHANGED
metric XYZ source         = TOUCHPLUS_STEREO/Q ONLY
stereo matcher            = UNCHANGED
OS injection              = DISABLED
```

A Phase 2B rejected/unknown fingertip is still **not** promoted back into a metric fingertip.

## Principle

Contact itself can remove the free-space distal boundary that Phase 2B normally requires. 2C.1C treats that disappearance as a separate **non-metric semantic cue**, but only after a strong metric pre-contact trajectory has already armed the bridge.

```text
recent VALID metric samples
same semantic contact identity
terminal H close to surface
strong descending terminal step
predicted next H crosses toward the surface
        ↓
CONTACT BRIDGE = ARMED
        ↓
current 2D fingertip proxy remains coherent
while metric fingertip becomes unavailable
        ↓
CONFIRMING
        ↓
second coherent occlusion frame
        ↓
TOUCH_DOWN
```

The invalid frames never provide H/XY and never increment `near_count`.

## Metric arming gate

Default candidate gates in `ContactConfigV1`:

```text
last VALID H                    <= 10 mm
terminal validated H drop      >= 5 mm
predicted next H                <= 2 mm
metric XY step                  <= 16 mm
last two metric samples spacing <= 4 semantic frames
last metric age at confirmation <= 3 semantic frames
```

The existing ordinary metric contact path remains:

```text
DOWN band              <= 12 mm
3 VALID near samples
RELEASE hysteresis     >= 22 mm
candidate XY step max  16 mm
```

## Non-metric 2D proxy gate

A contact-occlusion proxy is allowed only during a transient upstream gap and only while the current Touch+ hand/palm remains valid.

Preferred proxy:

```text
current locked anatomy candidate
sync = CURRENT or MOTION_COMPENSATED
age <= 1
```

Special contact-boundary proxy:

```text
anatomy sync rejection = TIP_NOT_CURRENT_DISTAL
age = 0 ONLY
prior anatomy track remains LOCKED
hand confidence >= 0.95
axis quality >= 0.85
continuity >= 0.80
```

The special case is explicitly **non-metric**. It exists because physical contact can erase the free-space distal clearance test. It may not publish a fingertip or Z.

Additional checks:

```text
proxy remains on/near current Touch+ silhouette
proxy pixel delta from last VALID fingertip <= 22 px
known anatomy/raw identity must not contradict the bound 2C.1B contact identity
hard NO_HAND / surface / tracking loss is never bridgeable
```

Critically, `TIP_NOT_CURRENT_DISTAL age>0` is never allowed into the bridge. This keeps the old 2B.9C.1 stale-tip-inside-hand failure class blocked.

## Confirmation and hold semantics

Two coherent occlusion proxy frames are required after arming.

The first produces only:

```text
contact_bridge=CONFIRMING
event=NONE
near_count unchanged
```

The second may produce:

```text
contact_bridge=HELD
event=DOWN
reason=touch-confirmed-contact-occlusion-bridge
```

Once a touch is active, a current coherent 2D occlusion proxy may sustain `HELD` while metric Z remains unavailable. A hard interruption, identity contradiction, or loss of the 2D proxy still produces fail-safe `UP`.

When metric fingertip data returns, the ordinary release hysteresis / jump safety logic resumes. The bridge never synthesizes a fake H value.

## Telemetry

The runtime adds:

```text
contact_bridge=DISARMED|ARMED|CONFIRMING|HELD
occlusion_proxy=VALID|NONE
proxy_reason=...
precontact_valid=...
last_valid_H=...
terminal_drop=...
predicted_H=...
occlusion_age=...
occlusion_confirm=...
occlusion_tip_delta=...
```

This is intended to make the next physical smoke directly auditable.

## Synthetic regression target

The central regression reproduces the physical class:

```text
30.8 mm
21.5 mm
transient gap
transient gap
6.3 mm
2D-coherent metric dropout
2D-coherent metric dropout
held occlusion
19.3 mm metric reacquisition
52+ mm lift
```

Expected:

```text
exactly one DOWN
no metric evidence invented during dropout
HELD can survive while the current 2D proxy remains coherent
exactly one fail-safe/release UP
```

Negative regressions include:

- large 2D proxy jump -> no bridge DOWN;
- stationary near-surface hover without terminal descent -> no bridge DOWN;
- `NO_HAND` interruption -> bridge history destroyed;
- ordinary metric contact path remains intact;
- active contact with no current 2D proxy -> fail-safe UP.

Synthetic CI only grants permission for a new physical smoke. PR #10 must remain Draft / non-merged until the real Touch+ produces reliable DOWN/HELD/UP without false semantic contact.
