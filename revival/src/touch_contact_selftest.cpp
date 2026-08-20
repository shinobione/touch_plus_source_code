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

    std::cout << "Phase 2C.1A sparse validated touch/contact semantics self-test: PASS\n";
    return 0;
}
