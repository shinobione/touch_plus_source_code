#pragma once

#include <cmath>
#include <cstdint>

namespace touchplus::contact {

inline constexpr double kCandidateHmm = 6.0;
inline constexpr double kContactDownHmm = 4.0;
inline constexpr double kContactUpHmm = 8.0;
inline constexpr int kCandidateFrames = 3;
inline constexpr int kReleaseFrames = 2;
inline constexpr double kApproachDeltaMm = -0.5;
inline constexpr double kMaxFrameDhMm = 20.0;
inline constexpr double kMaxFrameDxyMm = 50.0;

enum class ContactStateV2C1 {
    NoFinger,
    Hover,
    Approaching,
    ContactCandidate,
    TouchHeld,
    Release
};

enum class ContactEventV2C1 {
    Hover,
    TouchDown,
    TouchHeld,
    TouchUp
};

enum class FingertipSourceV2C1 { A, B };

struct ContactInputV2C1 {
    bool identity_accepted = false;
    bool identity_current = false;
    bool identity_stale = false;
    bool sample_valid = false;
    std::uint64_t identity_id = 0;
    FingertipSourceV2C1 fingertip_source = FingertipSourceV2C1::A;
    double x_mm = 0.0;
    double y_mm = 0.0;
    double h_mm = 0.0;
};

struct ContactOutputV2C1 {
    ContactStateV2C1 state = ContactStateV2C1::NoFinger;
    ContactEventV2C1 event = ContactEventV2C1::Hover;
    const char* reason = "NO_CURRENT_FINGER";
    std::uint64_t identity_id = 0;
    FingertipSourceV2C1 fingertip_source = FingertipSourceV2C1::A;
    double x_mm = 0.0;
    double y_mm = 0.0;
    double h_mm = 0.0;
    double dh_mm = 0.0;
    double dxy_mm = 0.0;
    int candidate_count = 0;
    int release_count = 0;
    std::uint64_t down_total = 0;
    std::uint64_t up_total = 0;
    bool delta_valid = false;
    bool state_changed = false;
};

inline const char* contact_state_name_v2c1(ContactStateV2C1 state) {
    switch (state) {
        case ContactStateV2C1::NoFinger: return "NO_FINGER";
        case ContactStateV2C1::Hover: return "HOVER";
        case ContactStateV2C1::Approaching: return "APPROACHING";
        case ContactStateV2C1::ContactCandidate: return "CONTACT_CANDIDATE";
        case ContactStateV2C1::TouchHeld: return "TOUCH_HELD";
        case ContactStateV2C1::Release: return "RELEASE";
    }
    return "NO_FINGER";
}

inline const char* contact_event_name_v2c1(ContactEventV2C1 event) {
    switch (event) {
        case ContactEventV2C1::Hover: return "HOVER";
        case ContactEventV2C1::TouchDown: return "TOUCH_DOWN";
        case ContactEventV2C1::TouchHeld: return "TOUCH_HELD";
        case ContactEventV2C1::TouchUp: return "TOUCH_UP";
    }
    return "HOVER";
}

inline const char* fingertip_source_name_v2c1(FingertipSourceV2C1 source) {
    return source == FingertipSourceV2C1::B ? "B" : "A";
}

class ContactStateMachineV2C1 {
public:
    ContactOutputV2C1 update(const ContactInputV2C1& input) {
        const ContactStateV2C1 previous_state = state_;
        ContactOutputV2C1 output;
        output.identity_id = input.identity_id;
        output.fingertip_source = input.fingertip_source;
        output.x_mm = input.x_mm;
        output.y_mm = input.y_mm;
        output.h_mm = input.h_mm;

        const bool finite = std::isfinite(input.x_mm) &&
            std::isfinite(input.y_mm) && std::isfinite(input.h_mm);
        const bool identity_ok = input.identity_accepted &&
            input.identity_current && !input.identity_stale &&
            input.identity_id != 0;
        const bool touching = is_touching();

        if (!finite) {
            fail_closed(output, touching, "NON_FINITE_SAMPLE");
            return finish(output, previous_state);
        }
        if (!input.sample_valid) {
            fail_closed(output, touching, "INVALID_SAMPLE");
            return finish(output, previous_state);
        }
        if (!identity_ok) {
            fail_closed(output, touching,
                input.identity_stale ? "IDENTITY_STALE" : "IDENTITY_NOT_CURRENT");
            return finish(output, previous_state);
        }
        if (active_identity_id_ != 0 && input.identity_id != active_identity_id_) {
            fail_closed(output, touching, "IDENTITY_CHANGED");
            return finish(output, previous_state);
        }

        if (have_previous_) {
            output.delta_valid = true;
            output.dh_mm = input.h_mm - previous_h_mm_;
            output.dxy_mm = std::hypot(
                input.x_mm - previous_x_mm_, input.y_mm - previous_y_mm_);
            if (std::abs(output.dh_mm) > kMaxFrameDhMm ||
                output.dxy_mm > kMaxFrameDxyMm) {
                fail_closed(output, touching,
                    std::abs(output.dh_mm) > kMaxFrameDhMm ?
                        "EXCESSIVE_H_JUMP" : "EXCESSIVE_XY_JUMP");
                return finish(output, previous_state);
            }
        }

        active_identity_id_ = input.identity_id;
        const bool approaching_now =
            output.delta_valid && output.dh_mm <= kApproachDeltaMm;

        if (touching) {
            update_touching(input, output);
        } else {
            update_hover_candidate(input, approaching_now, output);
        }

        previous_x_mm_ = input.x_mm;
        previous_y_mm_ = input.y_mm;
        previous_h_mm_ = input.h_mm;
        have_previous_ = true;
        return finish(output, previous_state);
    }

    void reset() {
        state_ = ContactStateV2C1::NoFinger;
        active_identity_id_ = 0;
        have_previous_ = false;
        approach_seen_ = false;
        candidate_count_ = 0;
        release_count_ = 0;
    }

private:
    bool is_touching() const {
        return state_ == ContactStateV2C1::TouchHeld ||
            state_ == ContactStateV2C1::Release;
    }

    void update_hover_candidate(
        const ContactInputV2C1& input,
        bool approaching_now,
        ContactOutputV2C1& output) {

        output.event = ContactEventV2C1::Hover;
        if (input.h_mm <= kCandidateHmm) {
            if (candidate_count_ == 0) approach_seen_ = approaching_now;
            else approach_seen_ = approach_seen_ || approaching_now;
            ++candidate_count_;
            state_ = ContactStateV2C1::ContactCandidate;
            output.reason = approach_seen_ ?
                "CANDIDATE_WITH_APPROACH" : "CANDIDATE_NO_APPROACH";

            if (candidate_count_ >= kCandidateFrames &&
                input.h_mm <= kContactDownHmm && approach_seen_) {
                state_ = ContactStateV2C1::TouchHeld;
                output.event = ContactEventV2C1::TouchDown;
                output.reason = "PERSISTENT_APPROACH_DOWN";
                ++down_total_;
                candidate_count_ = 0;
                release_count_ = 0;
                approach_seen_ = false;
            }
            return;
        }

        candidate_count_ = 0;
        approach_seen_ = false;
        state_ = approaching_now ?
            ContactStateV2C1::Approaching : ContactStateV2C1::Hover;
        output.reason = approaching_now ? "APPROACH_ABOVE_CANDIDATE" : "HOVER_CLEAR";
    }

    void update_touching(
        const ContactInputV2C1& input,
        ContactOutputV2C1& output) {

        output.event = ContactEventV2C1::TouchHeld;
        if (input.h_mm >= kContactUpHmm) {
            ++release_count_;
            state_ = ContactStateV2C1::Release;
            output.reason = "RELEASE_CANDIDATE";
            if (release_count_ >= kReleaseFrames) {
                state_ = ContactStateV2C1::Hover;
                output.event = ContactEventV2C1::TouchUp;
                output.reason = "RELEASE_HYSTERESIS_UP";
                ++up_total_;
                release_count_ = 0;
                active_identity_id_ = 0;
                have_previous_ = false;
            }
            return;
        }

        release_count_ = 0;
        state_ = ContactStateV2C1::TouchHeld;
        output.reason = "CONTACT_HELD";
    }

    void fail_closed(
        ContactOutputV2C1& output,
        bool touching,
        const char* reason) {

        if (touching) {
            output.event = ContactEventV2C1::TouchUp;
            ++up_total_;
        } else {
            output.event = ContactEventV2C1::Hover;
        }
        output.reason = reason;
        state_ = ContactStateV2C1::NoFinger;
        active_identity_id_ = 0;
        have_previous_ = false;
        approach_seen_ = false;
        candidate_count_ = 0;
        release_count_ = 0;
    }

    ContactOutputV2C1 finish(
        ContactOutputV2C1 output,
        ContactStateV2C1 previous_state) const {

        output.state = state_;
        output.candidate_count = candidate_count_;
        output.release_count = release_count_;
        output.down_total = down_total_;
        output.up_total = up_total_;
        output.state_changed = state_ != previous_state;
        return output;
    }

    ContactStateV2C1 state_ = ContactStateV2C1::NoFinger;
    std::uint64_t active_identity_id_ = 0;
    double previous_x_mm_ = 0.0;
    double previous_y_mm_ = 0.0;
    double previous_h_mm_ = 0.0;
    bool have_previous_ = false;
    bool approach_seen_ = false;
    int candidate_count_ = 0;
    int release_count_ = 0;
    std::uint64_t down_total_ = 0;
    std::uint64_t up_total_ = 0;
};

} // namespace touchplus::contact
