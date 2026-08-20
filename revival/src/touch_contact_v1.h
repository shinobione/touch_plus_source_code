#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace touchplus::contact {

enum class ContactStateV1 { NoFinger, Hover, Approaching, ContactCandidate, TouchDown, TouchHeld, TouchUp };
enum class ContactEventV1 { None, Down, Held, Up };
enum class ContactInputStatusV1 {
    Valid, TrackingDisabled, SurfaceInvalid, NoHand, IdentityUnknown, AnatomyRejected, StereoLow, NoFreshMetric,
};

inline const char* contact_state_name_v1(ContactStateV1 state) {
    switch (state) {
        case ContactStateV1::NoFinger: return "NO_FINGER";
        case ContactStateV1::Hover: return "HOVER";
        case ContactStateV1::Approaching: return "APPROACHING";
        case ContactStateV1::ContactCandidate: return "CONTACT_CANDIDATE";
        case ContactStateV1::TouchDown: return "TOUCH_DOWN";
        case ContactStateV1::TouchHeld: return "TOUCH_HELD";
        case ContactStateV1::TouchUp: return "TOUCH_UP";
    }
    return "UNKNOWN";
}

inline const char* contact_event_name_v1(ContactEventV1 event) {
    switch (event) {
        case ContactEventV1::None: return "NONE";
        case ContactEventV1::Down: return "DOWN";
        case ContactEventV1::Held: return "HELD";
        case ContactEventV1::Up: return "UP";
    }
    return "NONE";
}

inline const char* contact_input_status_name_v1(ContactInputStatusV1 status) {
    switch (status) {
        case ContactInputStatusV1::Valid: return "VALID";
        case ContactInputStatusV1::TrackingDisabled: return "TRACKING_DISABLED";
        case ContactInputStatusV1::SurfaceInvalid: return "SURFACE_INVALID";
        case ContactInputStatusV1::NoHand: return "NO_HAND";
        case ContactInputStatusV1::IdentityUnknown: return "IDENTITY_UNKNOWN";
        case ContactInputStatusV1::AnatomyRejected: return "ANATOMY_REJECT";
        case ContactInputStatusV1::StereoLow: return "STEREO_LOW";
        case ContactInputStatusV1::NoFreshMetric: return "NO_FRESH_METRIC";
    }
    return "NO_FRESH_METRIC";
}

inline bool transient_contact_gap_v1(ContactInputStatusV1 status) {
    return status == ContactInputStatusV1::IdentityUnknown ||
        status == ContactInputStatusV1::AnatomyRejected ||
        status == ContactInputStatusV1::StereoLow ||
        status == ContactInputStatusV1::NoFreshMetric;
}

struct ContactConfigV1 {
    double nominal_hz = 15.0;
    double approach_band_mm = 45.0;
    double down_h_mm = 12.0;
    double candidate_h_slack_mm = 3.0;
    double release_h_mm = 22.0;
    double min_approach_step_mm = 0.15;
    double max_candidate_xy_step_mm = 16.0;
    double max_precontact_xy_jump_mm = 45.0;
    double max_held_xy_jump_mm = 90.0;
    double max_h_jump_mm = 28.0;
    int approach_memory_frames = 12;
    int near_frames_required = 3;
    int release_frames_required = 2;
    int evidence_window_frames = 5;
    int max_transient_gap_frames = 1;
};

struct ContactSampleV1 {
    bool valid = false;
    ContactInputStatusV1 input_status = ContactInputStatusV1::NoFreshMetric;
    std::uint64_t identity_id = 0;
    double x_mm = 0.0;
    double y_mm = 0.0;
    double h_mm = 0.0;
};

struct ContactResultV1 {
    ContactStateV1 state = ContactStateV1::NoFinger;
    ContactEventV1 event = ContactEventV1::None;
    ContactInputStatusV1 input_status = ContactInputStatusV1::NoFreshMetric;
    std::uint64_t identity_id = 0;
    double h_mm = 0.0;
    double h_velocity_mm_s = 0.0;
    double xy_delta_mm = 0.0;
    int near_count = 0;
    int release_count = 0;
    int transient_gap_count = 0;
    int evidence_age_frames = 0;
    std::string reason = "no-finger";
};

class TouchContactDetectorV1 {
public:
    explicit TouchContactDetectorV1(ContactConfigV1 config = {}) : config_(config) {}
    void reset() { hard_reset(); }

    ContactResultV1 update(const ContactSampleV1& sample) {
        if (!sample.valid || sample.identity_id == 0 || !std::isfinite(sample.x_mm) ||
            !std::isfinite(sample.y_mm) || !std::isfinite(sample.h_mm)) {
            const auto status = sample.input_status == ContactInputStatusV1::Valid
                ? ContactInputStatusV1::NoFreshMetric : sample.input_status;
            return handle_invalid(status);
        }

        if (!have_previous_) {
            adopt_identity(sample);
            return make_result(ContactStateV1::Hover, ContactEventV1::None, sample, 0.0, 0.0,
                "identity-acquired", ContactInputStatusV1::Valid);
        }

        if (sample.identity_id != identity_id_) {
            if (is_contact_active()) {
                const auto out = make_result(ContactStateV1::TouchUp, ContactEventV1::Up, previous_, 0.0, 0.0,
                    "identity-switch", ContactInputStatusV1::Valid);
                hard_reset();
                return out;
            }
            adopt_identity(sample);
            return make_result(ContactStateV1::Hover, ContactEventV1::None, sample, 0.0, 0.0,
                "identity-switch-reset", ContactInputStatusV1::Valid);
        }

        transient_gap_count_ = 0;
        if (!is_contact_active() && near_count_ > 0) {
            ++evidence_age_frames_;
            if (evidence_age_frames_ > config_.evidence_window_frames) {
                clear_candidate_evidence();
                state_ = ContactStateV1::Hover;
            }
        }

        const double dx = sample.x_mm - previous_.x_mm;
        const double dy = sample.y_mm - previous_.y_mm;
        const double xy_delta = std::sqrt(dx * dx + dy * dy);
        const double dh = sample.h_mm - previous_.h_mm;
        const double h_velocity = dh * std::max(1.0, config_.nominal_hz);
        const double xy_limit = is_contact_active() ? config_.max_held_xy_jump_mm : config_.max_precontact_xy_jump_mm;

        if (std::abs(dh) > config_.max_h_jump_mm || xy_delta > xy_limit) {
            if (is_contact_active()) {
                const auto out = make_result(ContactStateV1::TouchUp, ContactEventV1::Up, sample, h_velocity, xy_delta,
                    std::abs(dh) > config_.max_h_jump_mm ? "held-h-jump" : "held-xy-jump", ContactInputStatusV1::Valid);
                hard_reset();
                return out;
            }
            adopt_identity(sample);
            state_ = ContactStateV1::Hover;
            return make_result(state_, ContactEventV1::None, sample, h_velocity, xy_delta,
                std::abs(dh) > config_.max_h_jump_mm ? "precontact-h-jump-reset" : "precontact-xy-jump-reset",
                ContactInputStatusV1::Valid);
        }

        if (dh <= -config_.min_approach_step_mm) approach_memory_ = config_.approach_memory_frames;
        else if (approach_memory_ > 0) --approach_memory_;

        ContactResultV1 result;
        switch (state_) {
            case ContactStateV1::NoFinger:
                state_ = ContactStateV1::Hover;
                result = make_result(state_, ContactEventV1::None, sample, h_velocity, xy_delta, "finger-valid", ContactInputStatusV1::Valid);
                break;

            case ContactStateV1::Hover:
                clear_candidate_evidence();
                release_count_ = 0;
                if (sample.h_mm <= config_.approach_band_mm && approach_memory_ > 0) {
                    state_ = ContactStateV1::Approaching;
                    result = make_result(state_, ContactEventV1::None, sample, h_velocity, xy_delta, "approach-evidence", ContactInputStatusV1::Valid);
                } else {
                    result = make_result(state_, ContactEventV1::None, sample, h_velocity, xy_delta, "hover", ContactInputStatusV1::Valid);
                }
                break;

            case ContactStateV1::Approaching:
                if (sample.h_mm > config_.approach_band_mm || approach_memory_ == 0) {
                    clear_candidate_evidence();
                    state_ = ContactStateV1::Hover;
                    result = make_result(state_, ContactEventV1::None, sample, h_velocity, xy_delta, "approach-lost", ContactInputStatusV1::Valid);
                    break;
                }
                if (sample.h_mm <= config_.down_h_mm) {
                    if (xy_delta > config_.max_candidate_xy_step_mm) {
                        clear_candidate_evidence();
                        state_ = ContactStateV1::Hover;
                        result = make_result(state_, ContactEventV1::None, sample, h_velocity, xy_delta, "candidate-xy-reset", ContactInputStatusV1::Valid);
                        break;
                    }
                    if (near_count_ == 0) evidence_age_frames_ = 1;
                    ++near_count_;
                    if (near_count_ >= std::max(1, config_.near_frames_required - 1)) {
                        state_ = ContactStateV1::ContactCandidate;
                        result = make_result(state_, ContactEventV1::None, sample, h_velocity, xy_delta, "near-surface-persistent", ContactInputStatusV1::Valid);
                    } else {
                        result = make_result(state_, ContactEventV1::None, sample, h_velocity, xy_delta, "near-surface-counting", ContactInputStatusV1::Valid);
                    }
                } else {
                    clear_candidate_evidence();
                    result = make_result(state_, ContactEventV1::None, sample, h_velocity, xy_delta, "approaching", ContactInputStatusV1::Valid);
                }
                break;

            case ContactStateV1::ContactCandidate:
                if (sample.h_mm > config_.down_h_mm + config_.candidate_h_slack_mm || approach_memory_ == 0) {
                    clear_candidate_evidence();
                    state_ = sample.h_mm <= config_.approach_band_mm ? ContactStateV1::Approaching : ContactStateV1::Hover;
                    result = make_result(state_, ContactEventV1::None, sample, h_velocity, xy_delta, "candidate-aborted", ContactInputStatusV1::Valid);
                    break;
                }
                if (xy_delta > config_.max_candidate_xy_step_mm) {
                    clear_candidate_evidence();
                    state_ = ContactStateV1::Hover;
                    result = make_result(state_, ContactEventV1::None, sample, h_velocity, xy_delta, "candidate-xy-reset", ContactInputStatusV1::Valid);
                    break;
                }
                ++near_count_;
                if (near_count_ >= config_.near_frames_required && evidence_age_frames_ <= config_.evidence_window_frames) {
                    state_ = ContactStateV1::TouchDown;
                    held_ = true;
                    release_count_ = 0;
                    result = make_result(state_, ContactEventV1::Down, sample, h_velocity, xy_delta,
                        "touch-confirmed-sparse-evidence", ContactInputStatusV1::Valid);
                } else {
                    result = make_result(state_, ContactEventV1::None, sample, h_velocity, xy_delta, "contact-confirming", ContactInputStatusV1::Valid);
                }
                break;

            case ContactStateV1::TouchDown:
                state_ = ContactStateV1::TouchHeld;
                [[fallthrough]];

            case ContactStateV1::TouchHeld:
                if (sample.h_mm >= config_.release_h_mm) {
                    ++release_count_;
                    if (release_count_ >= config_.release_frames_required) {
                        result = make_result(ContactStateV1::TouchUp, ContactEventV1::Up, sample, h_velocity, xy_delta,
                            "release-hysteresis", ContactInputStatusV1::Valid);
                        hard_reset();
                        return result;
                    }
                    result = make_result(ContactStateV1::TouchHeld, ContactEventV1::Held, sample, h_velocity, xy_delta,
                        "release-counting", ContactInputStatusV1::Valid);
                } else {
                    release_count_ = 0;
                    result = make_result(ContactStateV1::TouchHeld, ContactEventV1::Held, sample, h_velocity, xy_delta,
                        "held", ContactInputStatusV1::Valid);
                }
                break;

            case ContactStateV1::TouchUp:
                hard_reset();
                adopt_identity(sample);
                result = make_result(ContactStateV1::Hover, ContactEventV1::None, sample, h_velocity, xy_delta,
                    "post-up-hover", ContactInputStatusV1::Valid);
                break;
        }

        previous_ = sample;
        have_previous_ = true;
        return result;
    }

private:
    bool is_contact_active() const { return held_ || state_ == ContactStateV1::TouchDown || state_ == ContactStateV1::TouchHeld; }

    void clear_candidate_evidence() {
        near_count_ = 0;
        evidence_age_frames_ = 0;
        transient_gap_count_ = 0;
    }

    void hard_reset() {
        state_ = ContactStateV1::NoFinger;
        held_ = false;
        have_previous_ = false;
        identity_id_ = 0;
        previous_ = {};
        approach_memory_ = 0;
        near_count_ = 0;
        release_count_ = 0;
        transient_gap_count_ = 0;
        evidence_age_frames_ = 0;
    }

    void adopt_identity(const ContactSampleV1& sample) {
        identity_id_ = sample.identity_id;
        previous_ = sample;
        have_previous_ = true;
        state_ = ContactStateV1::Hover;
        held_ = false;
        approach_memory_ = 0;
        near_count_ = 0;
        release_count_ = 0;
        transient_gap_count_ = 0;
        evidence_age_frames_ = 0;
    }

    ContactResultV1 handle_invalid(ContactInputStatusV1 status) {
        if (is_contact_active()) {
            ContactSampleV1 last = previous_;
            const auto out = make_result(ContactStateV1::TouchUp, ContactEventV1::Up, last, 0.0, 0.0,
                contact_input_status_name_v1(status), status);
            hard_reset();
            return out;
        }

        const bool have_contact_evidence = have_previous_ &&
            (approach_memory_ > 0 || near_count_ > 0 || state_ == ContactStateV1::Approaching || state_ == ContactStateV1::ContactCandidate);
        if (!transient_contact_gap_v1(status) || !have_contact_evidence) {
            hard_reset();
            ContactResultV1 out;
            out.state = ContactStateV1::NoFinger;
            out.event = ContactEventV1::None;
            out.input_status = status;
            out.reason = contact_input_status_name_v1(status);
            return out;
        }

        ++transient_gap_count_;
        if (approach_memory_ > 0) --approach_memory_;
        if (near_count_ > 0) ++evidence_age_frames_;
        if (transient_gap_count_ > config_.max_transient_gap_frames || evidence_age_frames_ > config_.evidence_window_frames || approach_memory_ == 0) {
            hard_reset();
            ContactResultV1 out;
            out.state = ContactStateV1::NoFinger;
            out.event = ContactEventV1::None;
            out.input_status = status;
            out.reason = "transient-gap-expired";
            return out;
        }

        auto out = make_result(state_, ContactEventV1::None, previous_, 0.0, 0.0, "transient-gap-preserved", status);
        out.transient_gap_count = transient_gap_count_;
        out.evidence_age_frames = evidence_age_frames_;
        return out;
    }

    ContactResultV1 make_result(ContactStateV1 state, ContactEventV1 event, const ContactSampleV1& sample,
                                double h_velocity, double xy_delta, const char* reason, ContactInputStatusV1 input_status) const {
        ContactResultV1 out;
        out.state = state;
        out.event = event;
        out.input_status = input_status;
        out.identity_id = sample.identity_id;
        out.h_mm = sample.h_mm;
        out.h_velocity_mm_s = h_velocity;
        out.xy_delta_mm = xy_delta;
        out.near_count = near_count_;
        out.release_count = release_count_;
        out.transient_gap_count = transient_gap_count_;
        out.evidence_age_frames = evidence_age_frames_;
        out.reason = reason;
        return out;
    }

    ContactConfigV1 config_;
    ContactStateV1 state_ = ContactStateV1::NoFinger;
    bool held_ = false;
    bool have_previous_ = false;
    std::uint64_t identity_id_ = 0;
    ContactSampleV1 previous_{};
    int approach_memory_ = 0;
    int near_count_ = 0;
    int release_count_ = 0;
    int transient_gap_count_ = 0;
    int evidence_age_frames_ = 0;
};

} // namespace touchplus::contact
