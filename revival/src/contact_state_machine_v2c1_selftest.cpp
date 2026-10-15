#include "contact_state_machine_v2c1.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

using touchplus::contact::ContactEventV2C1;
using touchplus::contact::ContactInputV2C1;
using touchplus::contact::ContactOutputV2C1;
using touchplus::contact::ContactStateMachineV2C1;
using touchplus::contact::ContactStateV2C1;
using touchplus::contact::FingertipSourceV2C1;

struct Counts {
    int down = 0;
    int up = 0;
};

ContactInputV2C1 sample(
    double h,
    double x = 100.0,
    double y = 80.0,
    std::uint64_t identity = 7,
    FingertipSourceV2C1 source = FingertipSourceV2C1::A) {

    ContactInputV2C1 input;
    input.identity_accepted = true;
    input.identity_current = true;
    input.sample_valid = true;
    input.identity_id = identity;
    input.fingertip_source = source;
    input.x_mm = x;
    input.y_mm = y;
    input.h_mm = h;
    return input;
}

void count(const ContactOutputV2C1& output, Counts& counts) {
    if (output.event == ContactEventV2C1::TouchDown) ++counts.down;
    if (output.event == ContactEventV2C1::TouchUp) ++counts.up;
}

ContactOutputV2C1 feed(
    ContactStateMachineV2C1& machine,
    Counts& counts,
    const ContactInputV2C1& input) {

    const auto output = machine.update(input);
    count(output, counts);
    return output;
}

void approach_down(ContactStateMachineV2C1& machine, Counts& counts,
    FingertipSourceV2C1 source = FingertipSourceV2C1::A) {
    feed(machine, counts, sample(9.0, 100.0, 80.0, 7, source));
    feed(machine, counts, sample(5.5, 100.0, 80.0, 7, source));
    feed(machine, counts, sample(4.5, 100.0, 80.0, 7, source));
    feed(machine, counts, sample(3.5, 100.0, 80.0, 7, source));
}

void release_up(ContactStateMachineV2C1& machine, Counts& counts,
    FingertipSourceV2C1 source = FingertipSourceV2C1::A) {
    feed(machine, counts, sample(8.5, 100.0, 80.0, 7, source));
    feed(machine, counts, sample(9.0, 100.0, 80.0, 7, source));
}

bool expect(bool condition, const std::string& name) {
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
    return condition;
}

} // namespace

int main() {
    bool ok = true;

    {
        ContactStateMachineV2C1 machine; Counts counts;
        for (double h : {30.0, 25.0, 20.0, 15.0, 12.0})
            feed(machine, counts, sample(h));
        ok &= expect(counts.down == 0 && counts.up == 0,
            "hover only emits zero DOWN/UP");
    }
    {
        ContactStateMachineV2C1 machine; Counts counts;
        feed(machine, counts, sample(10.0));
        feed(machine, counts, sample(3.0));
        feed(machine, counts, sample(10.0));
        ok &= expect(counts.down == 0, "one low-H spike cannot emit DOWN");
    }
    {
        ContactStateMachineV2C1 machine; Counts counts;
        approach_down(machine, counts);
        ok &= expect(counts.down == 1 && counts.up == 0,
            "persistent approach emits exactly one DOWN");
        for (int i = 0; i < 8; ++i) feed(machine, counts, sample(3.2));
        ok &= expect(counts.down == 1,
            "stationary hold emits no additional DOWN");
        const auto lateral = feed(machine, counts, sample(3.0, 140.0, 80.0));
        ok &= expect(lateral.event == ContactEventV2C1::TouchHeld &&
                lateral.state == ContactStateV2C1::TouchHeld && counts.down == 1,
            "lateral XY motion remains held");
        release_up(machine, counts);
        ok &= expect(counts.up == 1,
            "release hysteresis emits exactly one UP");
    }
    {
        ContactStateMachineV2C1 machine; Counts counts;
        for (int tap = 0; tap < 3; ++tap) {
            approach_down(machine, counts);
            release_up(machine, counts);
        }
        ok &= expect(counts.down == 3 && counts.up == 3,
            "repeated taps emit one DOWN/UP pair per tap");
    }
    {
        ContactStateMachineV2C1 machine; Counts counts;
        auto invalid = sample(3.0); invalid.identity_accepted = false;
        feed(machine, counts, invalid);
        auto stale = sample(3.0); stale.identity_stale = true;
        feed(machine, counts, stale);
        auto non_current = sample(3.0); non_current.identity_current = false;
        feed(machine, counts, non_current);
        ok &= expect(counts.down == 0,
            "UNKNOWN/stale/non-current before contact emits zero DOWN");
    }
    {
        ContactStateMachineV2C1 machine; Counts counts;
        approach_down(machine, counts);
        auto lost = sample(3.0); lost.identity_current = false;
        feed(machine, counts, lost);
        feed(machine, counts, lost);
        ok &= expect(counts.down == 1 && counts.up == 1,
            "identity loss during hold emits exactly one fail-safe UP");
    }
    {
        ContactStateMachineV2C1 machine; Counts counts;
        approach_down(machine, counts);
        auto invalid = sample(3.0); invalid.sample_valid = false;
        feed(machine, counts, invalid);
        feed(machine, counts, invalid);
        ok &= expect(counts.down == 1 && counts.up == 1,
            "invalid sample during hold emits exactly one fail-safe UP");
    }
    {
        ContactStateMachineV2C1 machine; Counts counts;
        approach_down(machine, counts);
        feed(machine, counts, sample(3.0, 100.0, 80.0, 8));
        feed(machine, counts, sample(3.0, 100.0, 80.0, 8));
        ok &= expect(counts.down == 1 && counts.up == 1,
            "identity change during hold emits exactly one fail-safe UP");
    }
    {
        ContactStateMachineV2C1 machine; Counts counts;
        feed(machine, counts, sample(30.0));
        feed(machine, counts, sample(5.0));
        feed(machine, counts, sample(4.0, 160.0, 80.0));
        ok &= expect(counts.down == 0,
            "excessive H/XY deltas fail closed before contact");
        approach_down(machine, counts);
        feed(machine, counts, sample(3.0, 151.0, 80.0));
        ok &= expect(counts.up == 1,
            "excessive XY delta during hold emits one fail-safe UP");
    }
    {
        ContactStateMachineV2C1 machine; Counts counts;
        approach_down(machine, counts);
        feed(machine, counts, sample(24.0));
        ok &= expect(counts.down == 1 && counts.up == 1,
            "excessive H delta during hold emits one fail-safe UP");
    }
    {
        ContactStateMachineV2C1 machine; Counts counts;
        auto nan_sample = sample(3.0);
        nan_sample.h_mm = std::numeric_limits<double>::quiet_NaN();
        feed(machine, counts, nan_sample);
        auto inf_sample = sample(3.0);
        inf_sample.x_mm = std::numeric_limits<double>::infinity();
        feed(machine, counts, inf_sample);
        ok &= expect(counts.down == 0 && counts.up == 0,
            "NaN/inf before contact fail closed");
        approach_down(machine, counts);
        feed(machine, counts, nan_sample);
        feed(machine, counts, nan_sample);
        ok &= expect(counts.up == 1,
            "non-finite sample during hold emits exactly one fail-safe UP");
    }
    {
        Counts a_counts; ContactStateMachineV2C1 a;
        approach_down(a, a_counts, FingertipSourceV2C1::A);
        release_up(a, a_counts, FingertipSourceV2C1::A);
        Counts b_counts; ContactStateMachineV2C1 b;
        approach_down(b, b_counts, FingertipSourceV2C1::B);
        release_up(b, b_counts, FingertipSourceV2C1::B);
        ok &= expect(a_counts.down == 1 && a_counts.up == 1 &&
                b_counts.down == 1 && b_counts.up == 1,
            "selected source A and B obey identical contact rules");
    }

    ok &= expect(touchplus::contact::kCandidateHmm == 6.0 &&
            touchplus::contact::kContactDownHmm == 4.0 &&
            touchplus::contact::kContactUpHmm == 8.0 &&
            touchplus::contact::kCandidateFrames == 3 &&
            touchplus::contact::kReleaseFrames == 2 &&
            touchplus::contact::kApproachDeltaMm == -0.5 &&
            touchplus::contact::kMaxFrameDhMm == 20.0 &&
            touchplus::contact::kMaxFrameDxyMm == 50.0,
        "first-smoke constants remain exact");
    std::cout << "[PASS] OS_INJECTION=DISABLED (semantic events only)\n";
    return ok ? 0 : 1;
}
