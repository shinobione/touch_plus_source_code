#include "touch_contact_identity_v1.h"
#include "touch_contact_v1.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace touchplus::contact;

struct Counts { int down = 0; int up = 0; int held = 0; };

static ContactSampleV1 sample(std::uint64_t id, double x, double y, double h) {
    ContactSampleV1 out;
    out.valid = true;
    out.input_status = ContactInputStatusV1::Valid;
    out.identity_id = id;
    out.x_mm = x;
    out.y_mm = y;
    out.h_mm = h;
    return out;
}

static ContactSampleV1 gap(ContactInputStatusV1 status) {
    ContactSampleV1 out;
    out.valid = false;
    out.input_status = status;
    return out;
}

static ContactIdentityObservationV1 identity_obs(ContactIdentitySourceV1 source, std::uint64_t raw_id,
                                                 double x, double y, double h, int px, int py) {
    ContactIdentityObservationV1 out;
    out.valid = true;
    out.source = source;
    out.raw_identity_id = raw_id;
    out.x_mm = x;
    out.y_mm = y;
    out.h_mm = h;
    out.tip_x = px;
    out.tip_y = py;
    return out;
}

static void accumulate(const ContactResultV1& r, Counts& counts) {
    if (r.event == ContactEventV1::Down) ++counts.down;
    if (r.event == ContactEventV1::Up) ++counts.up;
    if (r.event == ContactEventV1::Held) ++counts.held;
}

static void require(bool ok, const std::string& message) {
    if (!ok) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

static void approach_and_down(TouchContactDetectorV1& detector, Counts& counts,
                              std::uint64_t id = 1, double x = 0.0, double y = 0.0) {
    for (double h = 55.0; h >= 11.0; h -= 2.0) accumulate(detector.update(sample(id, x, y, h)), counts);
    accumulate(detector.update(sample(id, x, y, 9.0)), counts);
    accumulate(detector.update(sample(id, x, y, 8.0)), counts);
    accumulate(detector.update(sample(id, x, y, 7.0)), counts);
    accumulate(detector.update(sample(id, x, y, 7.0)), counts);
}

int main() {
    {
        TouchContactDetectorV1 detector; Counts counts;
        for (int i = 0; i < 150; ++i) accumulate(detector.update(sample(1, 0, 0, 30)), counts);
        require(counts.down == 0, "long hover must not create DOWN");
    }
    {
        TouchContactDetectorV1 detector; Counts counts;
        for (int i = 0; i < 20; ++i) accumulate(detector.update(sample(1, 0, 0, 30)), counts);
        accumulate(detector.update(sample(1, 0, 0, 6)), counts);
        for (int i = 0; i < 20; ++i) accumulate(detector.update(sample(1, 0, 0, 30)), counts);
        require(counts.down == 0, "one-frame low-H spike must not create DOWN");
    }
    {
        TouchContactDetectorV1 detector; Counts counts;
        approach_and_down(detector, counts);
        require(counts.down == 1, "smooth approach must create exactly one DOWN");
        for (int i = 0; i < 90; ++i) accumulate(detector.update(sample(1, 0, 0, 7)), counts);
        require(counts.down == 1 && counts.held > 0, "long hold must not repeat DOWN");
        accumulate(detector.update(sample(1, 0, 0, 24)), counts);
        accumulate(detector.update(sample(1, 0, 0, 26)), counts);
        require(counts.up == 1, "lift must create exactly one UP");
    }
    {
        TouchContactDetectorV1 detector; Counts counts;
        for (int tap = 0; tap < 5; ++tap) {
            for (double h = 35.0; h >= 11.0; h -= 3.0) accumulate(detector.update(sample(1, 0, 0, h)), counts);
            accumulate(detector.update(sample(1, 0, 0, 9)), counts);
            accumulate(detector.update(sample(1, 0, 0, 8)), counts);
            accumulate(detector.update(sample(1, 0, 0, 7)), counts);
            accumulate(detector.update(sample(1, 0, 0, 7)), counts);
            accumulate(detector.update(sample(1, 0, 0, 24)), counts);
            accumulate(detector.update(sample(1, 0, 0, 26)), counts);
            accumulate(detector.update(sample(1, 0, 0, 30)), counts);
        }
        require(counts.down == 5 && counts.up == 5, "repeated taps must produce one DOWN/UP pair each");
    }
    {
        TouchContactDetectorV1 detector; Counts counts;
        approach_and_down(detector, counts);
        for (int i = 1; i <= 20; ++i) accumulate(detector.update(sample(1, i * 4.0, 0, 7)), counts);
        require(counts.down == 1 && counts.up == 0, "lateral drag while low-H must remain HELD");
        accumulate(detector.update(sample(1, 80, 0, 24)), counts);
        accumulate(detector.update(sample(1, 80, 0, 26)), counts);
        require(counts.up == 1, "drag lift must create one UP");
    }
    {
        TouchContactDetectorV1 detector; Counts counts;
        for (double h = 45.0; h >= 15.0; h -= 3.0) accumulate(detector.update(sample(1, 0, 0, h)), counts);
        accumulate(detector.update(gap(ContactInputStatusV1::NoHand)), counts);
        for (int i = 0; i < 5; ++i) accumulate(detector.update(sample(1, 0, 0, 7)), counts);
        require(counts.down == 0, "NO_HAND during approach must hard-reset without DOWN");
    }
    {
        TouchContactDetectorV1 detector; Counts counts;
        for (double h = 55.0; h >= 14.0; h -= 3.0) accumulate(detector.update(sample(7, 10, 5, h)), counts);
        accumulate(detector.update(sample(7, 10, 5, 10.0)), counts);
        auto g1 = detector.update(gap(ContactInputStatusV1::IdentityUnknown)); accumulate(g1, counts);
        require(g1.event == ContactEventV1::None && g1.reason == "transient-gap-preserved",
                "one transient identity gap must preserve evidence but emit no event");
        accumulate(detector.update(sample(7, 10.5, 5, 9.0)), counts);
        auto g2 = detector.update(gap(ContactInputStatusV1::StereoLow)); accumulate(g2, counts);
        require(g2.event == ContactEventV1::None && g2.input_status == ContactInputStatusV1::StereoLow,
                "stereo gap telemetry must be preserved without inventing contact");
        accumulate(detector.update(sample(7, 11.0, 5, 8.0)), counts);
        require(counts.down == 1, "three validated near samples across isolated transient gaps must create one DOWN");
    }
    {
        TouchContactDetectorV1 detector; Counts counts;
        for (double h = 45.0; h >= 13.0; h -= 4.0) accumulate(detector.update(sample(3, 0, 0, h)), counts);
        accumulate(detector.update(sample(3, 0, 0, 10)), counts);
        accumulate(detector.update(gap(ContactInputStatusV1::AnatomyRejected)), counts);
        auto expired = detector.update(gap(ContactInputStatusV1::NoFreshMetric)); accumulate(expired, counts);
        require(expired.state == ContactStateV1::NoFinger && counts.down == 0,
                "two consecutive transient gaps must expire sparse candidate evidence");
        for (int i = 0; i < 6; ++i) accumulate(detector.update(sample(3, 0, 0, 8)), counts);
        require(counts.down == 0, "expired evidence must not resurrect DOWN from stationary near samples");
    }
    {
        TouchContactDetectorV1 detector; Counts counts;
        approach_and_down(detector, counts);
        accumulate(detector.update(gap(ContactInputStatusV1::StereoLow)), counts);
        require(counts.down == 1 && counts.up == 1, "any upstream invalidity while held must fail-safe UP immediately");
    }
    {
        TouchContactDetectorV1 detector; Counts counts;
        approach_and_down(detector, counts);
        accumulate(detector.update(sample(2, 0, 0, 7)), counts);
        require(counts.down == 1 && counts.up == 1, "identity switch while held must fail-safe UP");
    }
    {
        TouchContactDetectorV1 detector; Counts counts;
        for (int i = 0; i < 30; ++i) accumulate(detector.update(gap(ContactInputStatusV1::NoHand)), counts);
        require(counts.down == 0 && counts.up == 0, "no-hand input must invent no contact");
    }
    {
        TouchContactDetectorV1 detector; Counts counts;
        for (double h = 40.0; h >= 18.0; h -= 2.0) accumulate(detector.update(sample(1, 0, 0, h)), counts);
        accumulate(detector.update(sample(1, 100, 0, 8)), counts);
        for (int i = 0; i < 10; ++i) accumulate(detector.update(sample(1, 100, 0, 8)), counts);
        require(counts.down == 0, "violent precontact XY jump must reset candidate");
    }
    {
        TouchContactDetectorV1 detector; Counts counts;
        for (int i = 0; i < 90; ++i) accumulate(detector.update(sample(1, 0, 0, 8)), counts);
        require(counts.down == 0, "already-near stationary finger without approach must not create DOWN");
    }

    // Phase 2C.1B: accepted Phase 2B intentionally uses different raw-id
    // namespaces for GEOMETRY+ANATOMY and ANATOMY_ONLY. The same physical finger
    // must be allowed to cross that source boundary without fooling 2C into an
    // identity switch.
    constexpr std::uint64_t anatomy7 = 0x8000000000000007ULL;
    constexpr std::uint64_t anatomy12 = 0x800000000000000cULL;
    {
        ContactIdentityContinuityV1 ids;
        const auto g1 = ids.update(identity_obs(ContactIdentitySourceV1::GeometryAnatomy, 21, 1.0, 2.0, 4.5, 400, 200));
        const auto a1 = ids.update(identity_obs(ContactIdentitySourceV1::AnatomyOnly, anatomy7, 1.4, 2.2, 4.5, 402, 201));
        const auto g2 = ids.update(identity_obs(ContactIdentitySourceV1::GeometryAnatomy, 21, 1.8, 2.5, 4.6, 403, 202));
        require(g1.contact_identity_id != 0 && g1.contact_identity_id == a1.contact_identity_id &&
                    a1.contact_identity_id == g2.contact_identity_id && a1.bridged && g2.bridged,
                "geometry 21 <-> anatomy 7 must preserve one physical contact identity");
    }
    {
        ContactIdentityContinuityV1 ids;
        const auto g21 = ids.update(identity_obs(ContactIdentitySourceV1::GeometryAnatomy, 21, 0, 0, 5, 300, 180));
        const auto a7 = ids.update(identity_obs(ContactIdentitySourceV1::AnatomyOnly, anatomy7, 0.5, 0, 5, 302, 181));
        const auto g34 = ids.update(identity_obs(ContactIdentitySourceV1::GeometryAnatomy, 34, 0.8, 0, 5, 304, 181));
        require(g21.contact_identity_id == a7.contact_identity_id && g34.contact_identity_id != a7.contact_identity_id,
                "geometry branch 21 -> anatomy 7 -> geometry branch 34 must remain a real identity switch");
    }
    {
        ContactIdentityContinuityV1 ids;
        const auto a7 = ids.update(identity_obs(ContactIdentitySourceV1::AnatomyOnly, anatomy7, 0, 0, 5, 300, 180));
        const auto a12 = ids.update(identity_obs(ContactIdentitySourceV1::AnatomyOnly, anatomy12, 0.5, 0, 5, 301, 181));
        require(a7.contact_identity_id != a12.contact_identity_id,
                "anatomy raw-id 7 -> 12 must remain a real same-source identity switch");
    }
    {
        ContactIdentityContinuityV1 ids;
        const auto g = ids.update(identity_obs(ContactIdentitySourceV1::GeometryAnatomy, 21, 0, 0, 5, 300, 180));
        const auto a = ids.update(identity_obs(ContactIdentitySourceV1::AnatomyOnly, anatomy7, 30, 0, 5, 340, 180));
        require(g.contact_identity_id != a.contact_identity_id && a.reason == "contact-identity-motion-reset",
                "large cross-mode metric/2D motion must not be aliased");
    }
    {
        ContactIdentityContinuityV1 ids;
        const auto g = ids.update(identity_obs(ContactIdentitySourceV1::GeometryAnatomy, 21, 0, 0, 5, 300, 180));
        ids.note_invalid(ContactIdentityInterruptionV1::Transient);
        const auto a = ids.update(identity_obs(ContactIdentitySourceV1::AnatomyOnly, anatomy7, 0.5, 0, 5, 302, 181));
        require(g.contact_identity_id != a.contact_identity_id && a.reason == "cross-mode-after-gap-reset",
                "cross-mode alias must not be created across an invalid semantic gap");
    }
    {
        ContactIdentityContinuityV1 ids;
        const auto g1 = ids.update(identity_obs(ContactIdentitySourceV1::GeometryAnatomy, 21, 0, 0, 5, 300, 180));
        ids.note_invalid(ContactIdentityInterruptionV1::Transient);
        const auto g2 = ids.update(identity_obs(ContactIdentitySourceV1::GeometryAnatomy, 21, 0.5, 0, 5, 301, 180));
        require(g1.contact_identity_id == g2.contact_identity_id,
                "same raw identity may resume after one sparse transient gap");
    }
    {
        ContactIdentityContinuityV1 ids;
        const auto g1 = ids.update(identity_obs(ContactIdentitySourceV1::GeometryAnatomy, 21, 0, 0, 5, 300, 180));
        ids.note_invalid(ContactIdentityInterruptionV1::Hard);
        const auto g2 = ids.update(identity_obs(ContactIdentitySourceV1::GeometryAnatomy, 21, 0, 0, 5, 300, 180));
        require(g1.contact_identity_id != g2.contact_identity_id,
                "NO_HAND/surface/tracking hard interruption must start a new contact identity epoch");
    }
    {
        // Physical regression class from the 2C.1A smoke: near-surface VALID
        // samples alternated between raw geometry id 21 and raw anatomy id 7 at
        // essentially the same H. The semantic detector must see one stable
        // contact identity and be able to accumulate the three validated samples.
        ContactIdentityContinuityV1 ids;
        TouchContactDetectorV1 detector;
        Counts counts;
        auto feed = [&](ContactIdentitySourceV1 source, std::uint64_t raw, double h, int px) {
            const auto decision = ids.update(identity_obs(source, raw, 0.0, 0.0, h, px, 200));
            require(decision.contact_identity_id != 0, "contact identity adapter must publish a semantic identity for valid input");
            accumulate(detector.update(sample(decision.contact_identity_id, 0.0, 0.0, h)), counts);
            return decision.contact_identity_id;
        };

        const auto cid0 = feed(ContactIdentitySourceV1::GeometryAnatomy, 21, 35.0, 390);
        feed(ContactIdentitySourceV1::GeometryAnatomy, 21, 25.0, 392);
        feed(ContactIdentitySourceV1::GeometryAnatomy, 21, 15.0, 394);
        const auto cid1 = feed(ContactIdentitySourceV1::AnatomyOnly, anatomy7, 10.0, 396);
        const auto cid2 = feed(ContactIdentitySourceV1::GeometryAnatomy, 21, 9.0, 397);
        const auto cid3 = feed(ContactIdentitySourceV1::AnatomyOnly, anatomy7, 8.0, 398);
        require(cid0 == cid1 && cid1 == cid2 && cid2 == cid3,
                "physical raw-id churn regression must remain one semantic contact identity");
        require(counts.down == 1, "cross-fusion raw-id churn must no longer prevent a real sparse-evidence DOWN");
    }

    std::cout << "Phase 2C.1B cross-fusion contact identity + sparse touch semantics self-test: PASS\n";
    return 0;
}
