#include "touch_contact_identity_v1.h"
#include "touch_contact_precontact_handoff_v1.h"
#include "touch_contact_v1.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace touchplus::contact;

namespace {

struct Counts { int down = 0; int up = 0; int held = 0; };

void require(bool ok, const std::string& message) {
    if (!ok) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

void accumulate(const ContactResultV1& r, Counts& counts) {
    if (r.event == ContactEventV1::Down) ++counts.down;
    if (r.event == ContactEventV1::Up) ++counts.up;
    if (r.event == ContactEventV1::Held) ++counts.held;
}

ContactIdentityObservationV1 obs(ContactIdentitySourceV1 source, std::uint64_t raw,
                                 double x, double y, double h, int px, int py) {
    ContactIdentityObservationV1 out;
    out.valid = true;
    out.source = source;
    out.raw_identity_id = raw;
    out.x_mm = x;
    out.y_mm = y;
    out.h_mm = h;
    out.tip_x = px;
    out.tip_y = py;
    return out;
}

ContactSampleV1 metric_sample(std::uint64_t detector_id, const ContactIdentityObservationV1& o) {
    ContactSampleV1 out;
    out.valid = true;
    out.input_status = ContactInputStatusV1::Valid;
    out.identity_id = detector_id;
    out.x_mm = o.x_mm;
    out.y_mm = o.y_mm;
    out.h_mm = o.h_mm;
    out.tip_x = o.tip_x;
    out.tip_y = o.tip_y;
    return out;
}

ContactSampleV1 gap(ContactInputStatusV1 status, int px = -1, int py = -1, bool proxy = false) {
    ContactSampleV1 out;
    out.valid = false;
    out.input_status = status;
    out.tip_x = px;
    out.tip_y = py;
    out.contact_occlusion_proxy = proxy;
    out.occlusion_identity_compatible = proxy;
    return out;
}

struct Harness {
    ContactIdentityContinuityV1 ids;
    ContactPrecontactIdentityHandoffV1 handoff;
    TouchContactDetectorV1 detector;
    Counts counts;
    ContactIdentityDecisionV1 last_identity;
    ContactPrecontactHandoffDecisionV1 last_handoff;

    ContactResultV1 valid(const ContactIdentityObservationV1& o) {
        last_identity = ids.update(o);
        last_handoff = handoff.update(last_identity, o);
        require(last_identity.contact_identity_id != 0, "identity adapter must accept valid metric observation");
        require(last_handoff.detector_contact_identity_id != 0, "handoff layer must provide detector identity");
        const auto r = detector.update(metric_sample(last_handoff.detector_contact_identity_id, o));
        accumulate(r, counts);
        return r;
    }

    ContactResultV1 invalid(ContactInputStatusV1 status, int px = -1, int py = -1, bool proxy = false) {
        const bool hard = status == ContactInputStatusV1::NoHand ||
            status == ContactInputStatusV1::SurfaceInvalid ||
            status == ContactInputStatusV1::TrackingDisabled;
        ids.note_invalid(hard ? ContactIdentityInterruptionV1::Hard : ContactIdentityInterruptionV1::Transient);
        handoff.note_invalid(hard ? ContactIdentityInterruptionV1::Hard : ContactIdentityInterruptionV1::Transient);
        const auto r = detector.update(gap(status, px, py, proxy));
        accumulate(r, counts);
        return r;
    }
};

} // namespace

int main() {
    constexpr std::uint64_t anatomy7 = 0x8000000000000007ULL;
    constexpr std::uint64_t anatomy12 = 0x800000000000000cULL;

    // Central physical regression from the 2C.1C smoke:
    // old contact epoch / H=13.8 -> transient gap -> opposite fusion source
    // creates a NEW 2C.1B adapter epoch / H=6.2. 2C.1C.1 must preserve only
    // the detector's bounded pre-contact metric history so the occlusion bridge
    // can arm. The adapter identities must remain different.
    {
        Harness h;
        const auto first = h.valid(obs(ContactIdentitySourceV1::AnatomyOnly, anatomy7,
                                       0.0, 0.0, 13.8, 317, 321));
        const auto old_adapter_id = h.last_identity.contact_identity_id;
        const auto old_detector_id = h.last_handoff.detector_contact_identity_id;
        require(first.event == ContactEventV1::None, "13.8 mm pre-contact sample must not create DOWN");

        h.invalid(ContactInputStatusV1::StereoLow);

        const auto near = h.valid(obs(ContactIdentitySourceV1::GeometryAnatomy, 9,
                                      0.8, 0.1, 6.2, 319, 322));
        require(h.last_identity.reason == "cross-mode-after-gap-reset",
                "physical regression must create the expected new 2C.1B adapter epoch");
        require(h.last_identity.contact_identity_id != old_adapter_id,
                "2C.1B adapter identity must remain a new epoch across the gap");
        require(h.last_handoff.handoff_applied &&
                    h.last_handoff.detector_contact_identity_id == old_detector_id,
                "2C.1C.1 must defer only pre-contact history across the safe cross-mode epoch change");
        require(near.contact_bridge == ContactOcclusionStateV1::Armed && near.precontact_valid_count == 2,
                "13.8 -> 6.2 mm handoff must arm the contact occlusion bridge with two real metric samples");
        require(near.near_count == 0,
                "deferred identity handoff must not invent ordinary near_count evidence");

        const auto c1 = h.invalid(ContactInputStatusV1::AnatomyRejected, 320, 322, true);
        require(c1.event == ContactEventV1::None && c1.contact_bridge == ContactOcclusionStateV1::Confirming,
                "first coherent occlusion proxy after handoff must only confirm");
        const auto c2 = h.invalid(ContactInputStatusV1::StereoLow, 320, 323, true);
        require(c2.event == ContactEventV1::Down && c2.contact_from_occlusion_bridge,
                "second coherent proxy must produce exactly one bridge DOWN after deferred handoff");

        const auto held = h.valid(obs(ContactIdentitySourceV1::GeometryAnatomy, 9,
                                      1.0, 0.1, 19.3, 320, 319));
        require(held.state == ContactStateV1::TouchHeld && h.counts.down == 1,
                "metric reacquisition after handoff bridge DOWN must remain HELD with no duplicate DOWN");
        h.valid(obs(ContactIdentitySourceV1::GeometryAnatomy, 9, 1.1, 0.1, 24.0, 320, 316));
        h.valid(obs(ContactIdentitySourceV1::GeometryAnatomy, 9, 1.2, 0.1, 26.0, 320, 314));
        require(h.counts.down == 1 && h.counts.up == 1,
                "deferred handoff contact must end in exactly one UP");
    }

    // A large motion reset is explicitly NOT eligible, even if the current H is low.
    {
        Harness h;
        h.valid(obs(ContactIdentitySourceV1::AnatomyOnly, anatomy7, 0, 0, 13.8, 300, 200));
        h.invalid(ContactInputStatusV1::StereoLow);
        const auto r = h.valid(obs(ContactIdentitySourceV1::GeometryAnatomy, 21, 60, 0, 6.2, 302, 201));
        require(h.last_identity.reason == "contact-identity-motion-reset",
                "large motion case must be classified as identity motion reset");
        require(!h.last_handoff.handoff_applied && r.contact_bridge != ContactOcclusionStateV1::Armed,
                "contact-identity-motion-reset must never inherit pre-contact bridge history");
        h.invalid(ContactInputStatusV1::StereoLow, 303, 202, true);
        h.invalid(ContactInputStatusV1::StereoLow, 304, 202, true);
        require(h.counts.down == 0, "motion-reset path must never create bridge DOWN");
    }

    // Even the exact cross-mode-after-gap reason is insufficient if the 2D tip
    // moved too far; this protects the historical stale/palm failure class.
    {
        Harness h;
        h.valid(obs(ContactIdentitySourceV1::AnatomyOnly, anatomy7, 0, 0, 13.8, 300, 200));
        h.invalid(ContactInputStatusV1::IdentityUnknown);
        const auto r = h.valid(obs(ContactIdentitySourceV1::GeometryAnatomy, 21, 0.5, 0, 6.2, 340, 200));
        require(h.last_identity.reason == "cross-mode-after-gap-reset",
                "tip-jump negative must still exercise exact cross-mode-after-gap-reset class");
        require(!h.last_handoff.handoff_applied &&
                    h.last_handoff.reason == "precontact-handoff-reject-tip" &&
                    r.contact_bridge != ContactOcclusionStateV1::Armed,
                "large 2D tip delta must reject deferred history handoff");
    }

    // Same-source raw-id switch remains a real identity change.
    {
        Harness h;
        h.valid(obs(ContactIdentitySourceV1::AnatomyOnly, anatomy7, 0, 0, 13.8, 300, 200));
        h.invalid(ContactInputStatusV1::StereoLow);
        const auto r = h.valid(obs(ContactIdentitySourceV1::AnatomyOnly, anatomy12, 0.5, 0, 6.2, 302, 201));
        require(h.last_identity.reason == "same-source-raw-id-switch" && !h.last_handoff.handoff_applied,
                "same-source raw-id switch must not use deferred pre-contact handoff");
        require(r.contact_bridge != ContactOcclusionStateV1::Armed,
                "same-source identity switch must discard previous bridge history");
    }

    // Hard interruption destroys both identity and handoff memory.
    {
        Harness h;
        h.valid(obs(ContactIdentitySourceV1::AnatomyOnly, anatomy7, 0, 0, 13.8, 300, 200));
        h.invalid(ContactInputStatusV1::NoHand);
        const auto r = h.valid(obs(ContactIdentitySourceV1::GeometryAnatomy, 21, 0.5, 0, 6.2, 302, 201));
        require(h.last_identity.reason == "contact-identity-acquired" && !h.last_handoff.handoff_applied,
                "NO_HAND must destroy any deferred identity handoff opportunity");
        require(r.contact_bridge != ContactOcclusionStateV1::Armed,
                "hard interruption must leave no inherited pre-contact bridge history");
    }

    // A cross-gap sample that is still too high above the surface must not hand off.
    {
        Harness h;
        h.valid(obs(ContactIdentitySourceV1::AnatomyOnly, anatomy7, 0, 0, 25.0, 300, 200));
        h.invalid(ContactInputStatusV1::StereoLow);
        h.valid(obs(ContactIdentitySourceV1::GeometryAnatomy, 21, 0.5, 0, 14.0, 302, 201));
        require(!h.last_handoff.handoff_applied &&
                    h.last_handoff.reason == "precontact-handoff-reject-not-near-surface",
                "deferred handoff must only occur at the near-surface terminal approach");
    }

    std::cout << "Phase 2C.1C.1 deferred precontact identity handoff self-test: PASS\n";
    return 0;
}
