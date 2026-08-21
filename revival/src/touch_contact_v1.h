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
enum class ContactOcclusionStateV1 { Disarmed, Armed, Confirming, Held };

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

inline const char* contact_occlusion_state_name_v1(ContactOcclusionStateV1 state) {
    switch (state) {
        case ContactOcclusionStateV1::Armed: return "ARMED";
        case ContactOcclusionStateV1::Confirming: return "CONFIRMING";
        case ContactOcclusionStateV1::Held: return "HELD";
        default: return "DISARMED";
    }
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

    // Phase 2C.1C contact-occlusion bridge. Metric evidence owns arming;
    // non-metric 2D continuity may only confirm the expected last-centimetre
    // disappearance after that metric trajectory has already crossed toward H=0.
    double occlusion_arm_h_mm = 10.0;
    double occlusion_min_terminal_drop_mm = 5.0;
    double occlusion_predicted_h_max_mm = 2.0;
    double occlusion_max_metric_xy_step_mm = 16.0;
    double occlusion_max_tip_delta_px = 22.0;
    int occlusion_max_metric_spacing_frames = 4;
    int occlusion_max_valid_age_frames = 3;
    int occlusion_confirm_frames = 2;
};

struct ContactSampleV1 {
    bool valid = false;
    ContactInputStatusV1 input_status = ContactInputStatusV1::NoFreshMetric;
    std::uint64_t identity_id = 0;
    double x_mm = 0.0;
    double y_mm = 0.0;
    double h_mm = 0.0;

    // Full-res Touch+ fingertip pixel for a VALID metric sample, or a current
    // non-metric 2D proxy when contact_occlusion_proxy is true.
    int tip_x = -1;
    int tip_y = -1;
    bool contact_occlusion_proxy = false;
    bool occlusion_identity_compatible = false;
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

    ContactOcclusionStateV1 contact_bridge = ContactOcclusionStateV1::Disarmed;
    int precontact_valid_count = 0;
    double last_valid_h_mm = 0.0;
    double terminal_drop_mm = 0.0;
    double predicted_next_h_mm = 0.0;
    int occlusion_age_frames = 0;
    int occlusion_confirm_count = 0;
    double occlusion_tip_delta_px = 0.0;
    bool contact_from_occlusion_bridge = false;

    std::string reason = "no-finger";
};

class TouchContactDetectorV1 {
public:
    explicit TouchContactDetectorV1(ContactConfigV1 config = {}) : config_(config) {}
    void reset() {
        semantic_frame_ = 0;
        reset_semantic_state(true);
    }

    ContactResultV1 update(const ContactSampleV1& sample) {
        ++semantic_frame_;
        if (!sample.valid || sample.identity_id == 0 || !std::isfinite(sample.x_mm) ||
            !std::isfinite(sample.y_mm) || !std::isfinite(sample.h_mm)) {
            const auto status = sample.input_status == ContactInputStatusV1::Valid
                ? ContactInputStatusV1::NoFreshMetric : sample.input_status;
            return handle_invalid(sample, status);
        }

        if (!have_previous_) {
            adopt_identity(sample);
            record_valid_metric(sample);
            refresh_occlusion_arm();
            return make_result(ContactStateV1::Hover, ContactEventV1::None, sample, 0.0, 0.0,
                "identity-acquired", ContactInputStatusV1::Valid);
        }

        if (sample.identity_id != identity_id_) {
            if (is_contact_active()) {
                const auto out = make_result(ContactStateV1::TouchUp, ContactEventV1::Up, previous_, 0.0, 0.0,
                    "identity-switch", ContactInputStatusV1::Valid);
                reset_semantic_state(true);
                return out;
            }
            clear_bridge_history();
            adopt_identity(sample);
            record_valid_metric(sample);
            refresh_occlusion_arm();
            return make_result(ContactStateV1::Hover, ContactEventV1::None, sample, 0.0, 0.0,
                "identity-switch-reset", ContactInputStatusV1::Valid);
        }

        transient_gap_count_ = 0;
        occlusion_confirm_count_ = 0;
        occlusion_age_frames_ = 0;
        occlusion_tip_delta_px_ = 0.0;
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
                reset_semantic_state(true);
                return out;
            }
            clear_bridge_history();
            adopt_identity(sample);
            record_valid_metric(sample);
            refresh_occlusion_arm();
            state_ = ContactStateV1::Hover;
            return make_result(state_, ContactEventV1::None, sample, h_velocity, xy_delta,
                std::abs(dh) > config_.max_h_jump_mm ? "precontact-h-jump-reset" : "precontact-xy-jump-reset",
                ContactInputStatusV1::Valid);
        }

        if (dh <= -config_.min_approach_step_mm) approach_memory_ = config_.approach_memory_frames;
        else if (approach_memory_ > 0) --approach_memory_;

        record_valid_metric(sample);
        refresh_occlusion_arm();

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
                    occlusion_hold_allowed_ = true;
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
                        reset_semantic_state(true);
                        return result;
                    }
                    result = make_result(ContactStateV1::TouchHeld, ContactEventV1::Held, sample, h_velocity, xy_delta,
                        "release-counting", ContactInputStatusV1::Valid);
                } else {
                    release_count_ = 0;
                    result = make_result(ContactStateV1::TouchHeld, ContactEventV1::Held, sample, h_velocity, xy_delta,
                        occlusion_held_ ? "held-metric-reacquired-after-occlusion" : "held", ContactInputStatusV1::Valid);
                }
                break;

            case ContactStateV1::TouchUp:
                reset_semantic_state(true);
                adopt_identity(sample);
                record_valid_metric(sample);
                refresh_occlusion_arm();
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

    void clear_bridge_history() {
        bridge_history_identity_id_ = 0;
        have_bridge_prev_metric_ = false;
        have_bridge_last_metric_ = false;
        bridge_prev_metric_ = {};
        bridge_last_metric_ = {};
        bridge_prev_tick_ = 0;
        bridge_last_tick_ = 0;
        occlusion_armed_ = false;
        occlusion_confirm_count_ = 0;
        occlusion_hold_allowed_ = false;
        occlusion_held_ = false;
        terminal_drop_mm_ = 0.0;
        predicted_next_h_mm_ = 0.0;
        occlusion_age_frames_ = 0;
        occlusion_tip_delta_px_ = 0.0;
    }

    void reset_semantic_state(bool clear_bridge) {
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
        occlusion_confirm_count_ = 0;
        occlusion_age_frames_ = 0;
        occlusion_tip_delta_px_ = 0.0;
        if (clear_bridge) clear_bridge_history();
        else {
            occlusion_armed_ = false;
            occlusion_hold_allowed_ = false;
            occlusion_held_ = false;
        }
    }

    void adopt_identity(const ContactSampleV1& sample) {
        if (bridge_history_identity_id_ != 0 && bridge_history_identity_id_ != sample.identity_id)
            clear_bridge_history();
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
        occlusion_confirm_count_ = 0;
        occlusion_hold_allowed_ = false;
        occlusion_held_ = false;
    }

    void record_valid_metric(const ContactSampleV1& sample) {
        if (bridge_history_identity_id_ != sample.identity_id) {
            clear_bridge_history();
            bridge_history_identity_id_ = sample.identity_id;
        }
        if (have_bridge_last_metric_) {
            bridge_prev_metric_ = bridge_last_metric_;
            bridge_prev_tick_ = bridge_last_tick_;
            have_bridge_prev_metric_ = true;
        }
        bridge_last_metric_ = sample;
        bridge_last_tick_ = semantic_frame_;
        have_bridge_last_metric_ = true;
    }

    void refresh_occlusion_arm() {
        occlusion_armed_ = false;
        terminal_drop_mm_ = 0.0;
        predicted_next_h_mm_ = have_bridge_last_metric_ ? bridge_last_metric_.h_mm : 0.0;
        if (!have_bridge_prev_metric_ || !have_bridge_last_metric_) return;
        if (bridge_prev_metric_.identity_id == 0 || bridge_prev_metric_.identity_id != bridge_last_metric_.identity_id) return;
        if (bridge_prev_metric_.tip_x < 0 || bridge_prev_metric_.tip_y < 0 ||
            bridge_last_metric_.tip_x < 0 || bridge_last_metric_.tip_y < 0) return;
        if (bridge_last_tick_ <= bridge_prev_tick_ ||
            bridge_last_tick_ - bridge_prev_tick_ > static_cast<std::uint64_t>(config_.occlusion_max_metric_spacing_frames)) return;
        if (bridge_last_metric_.h_mm > config_.occlusion_arm_h_mm) return;

        terminal_drop_mm_ = bridge_prev_metric_.h_mm - bridge_last_metric_.h_mm;
        if (terminal_drop_mm_ < config_.occlusion_min_terminal_drop_mm) return;
        predicted_next_h_mm_ = bridge_last_metric_.h_mm - terminal_drop_mm_;
        if (predicted_next_h_mm_ > config_.occlusion_predicted_h_max_mm) return;

        const double dx = bridge_last_metric_.x_mm - bridge_prev_metric_.x_mm;
        const double dy = bridge_last_metric_.y_mm - bridge_prev_metric_.y_mm;
        if (std::hypot(dx, dy) > config_.occlusion_max_metric_xy_step_mm) return;

        occlusion_armed_ = true;
    }

    bool occlusion_proxy_compatible(const ContactSampleV1& sample) {
        occlusion_tip_delta_px_ = 0.0;
        if (!sample.contact_occlusion_proxy || !sample.occlusion_identity_compatible ||
            sample.tip_x < 0 || sample.tip_y < 0 || !have_bridge_last_metric_ ||
            bridge_last_metric_.tip_x < 0 || bridge_last_metric_.tip_y < 0) return false;
        occlusion_tip_delta_px_ = std::hypot(
            static_cast<double>(sample.tip_x - bridge_last_metric_.tip_x),
            static_cast<double>(sample.tip_y - bridge_last_metric_.tip_y));
        return occlusion_tip_delta_px_ <= config_.occlusion_max_tip_delta_px;
    }

    bool bridge_can_confirm(const ContactSampleV1& sample, ContactInputStatusV1 status) {
        if (!transient_contact_gap_v1(status) || !occlusion_armed_ || !have_bridge_last_metric_) return false;
        if (semantic_frame_ < bridge_last_tick_) return false;
        occlusion_age_frames_ = static_cast<int>(semantic_frame_ - bridge_last_tick_);
        if (occlusion_age_frames_ > config_.occlusion_max_valid_age_frames) return false;
        return occlusion_proxy_compatible(sample);
    }

    ContactResultV1 handle_invalid(const ContactSampleV1& sample, ContactInputStatusV1 status) {
        const bool transient = transient_contact_gap_v1(status);

        if (is_contact_active()) {
            if (occlusion_hold_allowed_ && transient && occlusion_proxy_compatible(sample)) {
                occlusion_held_ = true;
                occlusion_age_frames_ = have_bridge_last_metric_ && semantic_frame_ >= bridge_last_tick_
                    ? static_cast<int>(semantic_frame_ - bridge_last_tick_) : 0;
                state_ = ContactStateV1::TouchHeld;
                auto out = make_result(ContactStateV1::TouchHeld, ContactEventV1::Held, previous_, 0.0, 0.0,
                    "contact-occlusion-held", status);
                out.contact_bridge = ContactOcclusionStateV1::Held;
                return out;
            }

            ContactSampleV1 last = previous_;
            const auto out = make_result(ContactStateV1::TouchUp, ContactEventV1::Up, last, 0.0, 0.0,
                contact_input_status_name_v1(status), status);
            reset_semantic_state(true);
            return out;
        }

        if (bridge_can_confirm(sample, status)) {
            ++occlusion_confirm_count_;
            state_ = ContactStateV1::ContactCandidate;
            if (occlusion_confirm_count_ >= config_.occlusion_confirm_frames) {
                state_ = ContactStateV1::TouchDown;
                held_ = true;
                occlusion_hold_allowed_ = true;
                occlusion_held_ = true;
                release_count_ = 0;
                auto out = make_result(ContactStateV1::TouchDown, ContactEventV1::Down, previous_, 0.0, 0.0,
                    "touch-confirmed-contact-occlusion-bridge", status);
                out.contact_bridge = ContactOcclusionStateV1::Held;
                out.contact_from_occlusion_bridge = true;
                return out;
            }
            auto out = make_result(ContactStateV1::ContactCandidate, ContactEventV1::None, previous_, 0.0, 0.0,
                "contact-occlusion-confirming", status);
            out.contact_bridge = ContactOcclusionStateV1::Confirming;
            return out;
        }

        const bool have_contact_evidence = have_previous_ &&
            (approach_memory_ > 0 || near_count_ > 0 || state_ == ContactStateV1::Approaching || state_ == ContactStateV1::ContactCandidate);
        if (!transient || !have_contact_evidence) {
            reset_semantic_state(!transient);
            ContactResultV1 out;
            out.state = ContactStateV1::NoFinger;
            out.event = ContactEventV1::None;
            out.input_status = status;
            out.contact_bridge = occlusion_armed_ ? ContactOcclusionStateV1::Armed : ContactOcclusionStateV1::Disarmed;
            out.reason = contact_input_status_name_v1(status);
            return out;
        }

        ++transient_gap_count_;
        if (approach_memory_ > 0) --approach_memory_;
        if (near_count_ > 0) ++evidence_age_frames_;
        if (transient_gap_count_ > config_.max_transient_gap_frames || evidence_age_frames_ > config_.evidence_window_frames || approach_memory_ == 0) {
            // Preserve only the bounded recent metric history for 2C.1C. This lets
            // a fresh same-identity near-surface metric sample complete the
            // 21.5 -> 6.3 mm physical regression trajectory after sparse gaps,
            // while the age/spacing gates prevent stale resurrection.
            reset_semantic_state(false);
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
        out.identity_id = sample.identity_id != 0 ? sample.identity_id : identity_id_;
        out.h_mm = sample.h_mm;
        out.h_velocity_mm_s = h_velocity;
        out.xy_delta_mm = xy_delta;
        out.near_count = near_count_;
        out.release_count = release_count_;
        out.transient_gap_count = transient_gap_count_;
        out.evidence_age_frames = evidence_age_frames_;
        out.contact_bridge = occlusion_held_ ? ContactOcclusionStateV1::Held
            : occlusion_confirm_count_ > 0 ? ContactOcclusionStateV1::Confirming
            : occlusion_armed_ ? ContactOcclusionStateV1::Armed
            : ContactOcclusionStateV1::Disarmed;
        out.precontact_valid_count = have_bridge_prev_metric_ ? 2 : have_bridge_last_metric_ ? 1 : 0;
        out.last_valid_h_mm = have_bridge_last_metric_ ? bridge_last_metric_.h_mm : 0.0;
        out.terminal_drop_mm = terminal_drop_mm_;
        out.predicted_next_h_mm = predicted_next_h_mm_;
        out.occlusion_age_frames = occlusion_age_frames_;
        out.occlusion_confirm_count = occlusion_confirm_count_;
        out.occlusion_tip_delta_px = occlusion_tip_delta_px_;
        out.contact_from_occlusion_bridge = occlusion_held_;
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

    std::uint64_t semantic_frame_ = 0;
    std::uint64_t bridge_history_identity_id_ = 0;
    bool have_bridge_prev_metric_ = false;
    bool have_bridge_last_metric_ = false;
    ContactSampleV1 bridge_prev_metric_{};
    ContactSampleV1 bridge_last_metric_{};
    std::uint64_t bridge_prev_tick_ = 0;
    std::uint64_t bridge_last_tick_ = 0;
    bool occlusion_armed_ = false;
    bool occlusion_hold_allowed_ = false;
    bool occlusion_held_ = false;
    int occlusion_confirm_count_ = 0;
    double terminal_drop_mm_ = 0.0;
    double predicted_next_h_mm_ = 0.0;
    int occlusion_age_frames_ = 0;
    double occlusion_tip_delta_px_ = 0.0;
};

} // namespace touchplus::contact
