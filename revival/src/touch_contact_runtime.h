#pragma once

// Phase 2C.1B is a semantic layer on top of the accepted Phase 2B fingertip
// stream. It force-includes the complete 2B.9C.2 runtime first, then wraps its
// final valid fingertip output without changing camera/surface calibration,
// landmark ownership, stereo matching, persistent capture, or Phase 2B raw ids.
#include "depth_surface_frame_runtime.h"
#include "touch_contact_identity_v1.h"
#include "touch_contact_v1.h"

#ifdef point_depth
#undef point_depth
#endif
#ifdef compute_depth_heatmap
#undef compute_depth_heatmap
#endif

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>

namespace touchplus::depth {
namespace contact_runtime_detail {

inline touchplus::contact::ContactIdentitySourceV1 contact_identity_source_from_fusion(
    touchplus::tracking::FusionModeV9 mode) {
    switch (mode) {
        case touchplus::tracking::FusionModeV9::GeometryAnatomyAgree:
            return touchplus::contact::ContactIdentitySourceV1::GeometryAnatomy;
        case touchplus::tracking::FusionModeV9::AnatomyOnly:
            return touchplus::contact::ContactIdentitySourceV1::AnatomyOnly;
        default:
            return touchplus::contact::ContactIdentitySourceV1::Unknown;
    }
}

inline bool hard_contact_identity_interruption(touchplus::contact::ContactInputStatusV1 status) {
    return status == touchplus::contact::ContactInputStatusV1::TrackingDisabled ||
        status == touchplus::contact::ContactInputStatusV1::SurfaceInvalid ||
        status == touchplus::contact::ContactInputStatusV1::NoHand;
}

struct RuntimeContactState {
    touchplus::contact::TouchContactDetectorV1 detector;
    touchplus::contact::ContactIdentityContinuityV1 identity_continuity;
    touchplus::contact::ContactIdentityDecisionV1 identity_decision;
    touchplus::contact::ContactResultV1 result;
    std::uint64_t raw_identity_id = 0;
    touchplus::contact::ContactIdentitySourceV1 raw_identity_source = touchplus::contact::ContactIdentitySourceV1::Unknown;
    std::string identity_alias_reason = "identity-unavailable";
    bool announced = false;
    bool have_logged_state = false;
    bool have_logged_input = false;
    std::uint64_t last_logged_raw_identity_id = 0;
    std::uint64_t last_logged_contact_identity_id = 0;
    touchplus::contact::ContactIdentitySourceV1 last_logged_identity_source = touchplus::contact::ContactIdentitySourceV1::Unknown;
    std::string last_logged_alias_reason;
    touchplus::contact::ContactStateV1 last_logged_state = touchplus::contact::ContactStateV1::NoFinger;
    touchplus::contact::ContactInputStatusV1 last_logged_input = touchplus::contact::ContactInputStatusV1::NoFreshMetric;
    std::uint64_t report_counter = 0;
};

inline RuntimeContactState& state() {
    static thread_local RuntimeContactState value;
    return value;
}

inline void announce_once(RuntimeContactState& s) {
    if (s.announced) return;
    s.announced = true;
    std::cout << "\n[CONTACT] PHASE 2C.1B CROSS-FUSION CONTACT IDENTITY ACTIVE | OS_INJECTION=DISABLED\n";
    std::cout << "[CONTACT] Input ownership: accepted Phase 2B fingertip only. model Z remains disabled.\n";
    std::cout << "[CONTACT] Candidate thresholds: DOWN<=12 mm, RELEASE>=22 mm, 3 VALID near samples.\n";
    std::cout << "[CONTACT] Sparse gate: isolated transient UNKNOWN may preserve evidence; UNKNOWN never adds evidence or creates DOWN.\n";
    std::cout << "[CONTACT] Identity adapter: conservative GEOMETRY+ANATOMY <-> ANATOMY_ONLY aliasing only; same-source raw-id switches remain hard identity changes.\n";
    std::cout << "[CONTACT] Safety: cross-mode alias requires continuous metric + 2D tip motion and cannot be created across an invalid semantic gap.\n";
}

inline touchplus::contact::ContactInputStatusV1 classify_input_status() {
    auto& tracking = tracking_runtime_detail::state();
    if (!tracking.enabled) return touchplus::contact::ContactInputStatusV1::TrackingDisabled;
    if (!touchplus::surface::live_surface_model().valid) return touchplus::contact::ContactInputStatusV1::SurfaceInvalid;

    const auto& fusion = tracking.tracker.last_fusion();
    if (tracking.result.fingertip_valid && fusion.identity_id != 0)
        return touchplus::contact::ContactInputStatusV1::Valid;

    if (!tracking.result.hand_valid)
        return touchplus::contact::ContactInputStatusV1::NoHand;

    if (fusion.publish && fusion.identity_id != 0) {
        const auto& stereo = tracking.tracker.stereo_confidence();
        if (stereo == "LOW" || stereo == "NOT_RUN")
            return touchplus::contact::ContactInputStatusV1::StereoLow;
        return touchplus::contact::ContactInputStatusV1::NoFreshMetric;
    }

    const auto& anatomy = tracking.tracker.last_anatomy_observation();
    if (anatomy.status == touchplus::tracking::AnatomyStatusV9::Rejected ||
        anatomy.status == touchplus::tracking::AnatomyStatusV9::Stale ||
        anatomy.status == touchplus::tracking::AnatomyStatusV9::Error)
        return touchplus::contact::ContactInputStatusV1::AnatomyRejected;

    return touchplus::contact::ContactInputStatusV1::IdentityUnknown;
}

inline void log_transition(RuntimeContactState& s) {
    announce_once(s);
    const bool edge = s.result.event == touchplus::contact::ContactEventV1::Down ||
        s.result.event == touchplus::contact::ContactEventV1::Up;
    const bool state_changed = !s.have_logged_state || s.result.state != s.last_logged_state;
    const bool input_changed = !s.have_logged_input || s.result.input_status != s.last_logged_input;
    const bool identity_changed = s.raw_identity_id != s.last_logged_raw_identity_id ||
        s.result.identity_id != s.last_logged_contact_identity_id ||
        s.raw_identity_source != s.last_logged_identity_source ||
        s.identity_alias_reason != s.last_logged_alias_reason;
    if (!edge && !state_changed && !input_changed && !identity_changed) return;

    std::cout << std::fixed << std::setprecision(1)
        << "[CONTACT] contact_state=" << touchplus::contact::contact_state_name_v1(s.result.state)
        << " | event=" << touchplus::contact::contact_event_name_v1(s.result.event)
        << " | input=" << touchplus::contact::contact_input_status_name_v1(s.result.input_status)
        << " | raw_identity_id=" << s.raw_identity_id
        << " | contact_identity_id=" << s.result.identity_id
        << " | identity_source=" << touchplus::contact::contact_identity_source_name_v1(s.raw_identity_source)
        << " | identity_alias=" << s.identity_alias_reason
        << " | H=" << s.result.h_mm << " mm"
        << " | h_velocity=" << s.result.h_velocity_mm_s << " mm/s"
        << " | near_count=" << s.result.near_count
        << " | gap_count=" << s.result.transient_gap_count
        << " | evidence_age=" << s.result.evidence_age_frames
        << " | release_count=" << s.result.release_count
        << " | xy_delta=" << s.result.xy_delta_mm << " mm"
        << " | reason=" << s.result.reason << "\n";

    s.last_logged_state = s.result.state;
    s.last_logged_input = s.result.input_status;
    s.last_logged_raw_identity_id = s.raw_identity_id;
    s.last_logged_contact_identity_id = s.result.identity_id;
    s.last_logged_identity_source = s.raw_identity_source;
    s.last_logged_alias_reason = s.identity_alias_reason;
    s.have_logged_state = true;
    s.have_logged_input = true;
}

inline void update_from_tracking() {
    auto& s = state();
    auto& tracking = tracking_runtime_detail::state();
    announce_once(s);

    touchplus::contact::ContactSampleV1 sample;
    sample.input_status = classify_input_status();
    const auto& fusion = tracking.tracker.last_fusion();
    s.raw_identity_id = fusion.identity_id;
    s.raw_identity_source = contact_identity_source_from_fusion(fusion.mode);

    if (sample.input_status == touchplus::contact::ContactInputStatusV1::Valid) {
        touchplus::contact::ContactIdentityObservationV1 identity_obs;
        identity_obs.valid = true;
        identity_obs.raw_identity_id = fusion.identity_id;
        identity_obs.source = s.raw_identity_source;
        identity_obs.x_mm = tracking.result.smoothed_tip.x_mm;
        identity_obs.y_mm = tracking.result.smoothed_tip.y_mm;
        identity_obs.h_mm = tracking.result.smoothed_tip.h_mm;
        identity_obs.tip_x = fusion.pixel_x;
        identity_obs.tip_y = fusion.pixel_y;
        s.identity_decision = s.identity_continuity.update(identity_obs);
        s.identity_alias_reason = s.identity_decision.reason;

        if (s.identity_decision.contact_identity_id != 0) {
            sample.valid = true;
            sample.identity_id = s.identity_decision.contact_identity_id;
            sample.x_mm = tracking.result.smoothed_tip.x_mm;
            sample.y_mm = tracking.result.smoothed_tip.y_mm;
            sample.h_mm = tracking.result.smoothed_tip.h_mm;
        } else {
            sample.input_status = touchplus::contact::ContactInputStatusV1::IdentityUnknown;
            s.identity_alias_reason = "identity-adapter-rejected-valid-sample";
            s.identity_continuity.note_invalid(touchplus::contact::ContactIdentityInterruptionV1::Transient);
        }
    } else {
        s.identity_continuity.note_invalid(
            hard_contact_identity_interruption(sample.input_status)
                ? touchplus::contact::ContactIdentityInterruptionV1::Hard
                : touchplus::contact::ContactIdentityInterruptionV1::Transient);
        s.identity_alias_reason = hard_contact_identity_interruption(sample.input_status)
            ? "identity-hard-invalid-reset"
            : "identity-transient-gap-no-cross-mode-bridge";
    }

    s.result = s.detector.update(sample);
    log_transition(s);
}

inline void maybe_report(RuntimeContactState& s) {
    announce_once(s);
    ++s.report_counter;
    if (s.report_counter % 30 != 0) return;

    std::cout << std::fixed << std::setprecision(1)
        << "[CONTACT] heartbeat | contact_state=" << touchplus::contact::contact_state_name_v1(s.result.state)
        << " | event=" << touchplus::contact::contact_event_name_v1(s.result.event)
        << " | input=" << touchplus::contact::contact_input_status_name_v1(s.result.input_status)
        << " | raw_identity_id=" << s.raw_identity_id
        << " | contact_identity_id=" << s.result.identity_id
        << " | identity_source=" << touchplus::contact::contact_identity_source_name_v1(s.raw_identity_source)
        << " | identity_alias=" << s.identity_alias_reason
        << " | H=" << s.result.h_mm << " mm"
        << " | h_velocity=" << s.result.h_velocity_mm_s << " mm/s"
        << " | near_count=" << s.result.near_count
        << " | gap_count=" << s.result.transient_gap_count
        << " | evidence_age=" << s.result.evidence_age_frames
        << " | release_count=" << s.result.release_count
        << " | xy_delta=" << s.result.xy_delta_mm << " mm"
        << " | reason=" << s.result.reason << "\n";
}

} // namespace contact_runtime_detail

inline void compute_depth_heatmap_touch_contact_wrapper(
    const Calibration& c,
    const std::vector<uint8_t>& left,
    const std::vector<uint8_t>& right,
    DepthWorkspace& workspace) {
    compute_depth_heatmap_fingertip_wrapper(c, left, right, workspace);
    contact_runtime_detail::update_from_tracking();
}

inline PointDepth point_depth_touch_contact_wrapper(
    const Calibration& c,
    const std::vector<uint8_t>& left,
    const std::vector<uint8_t>& right,
    int cursor_x,
    int cursor_y) {
    PointDepth result = point_depth_surface_runtime_wrapper(c, left, right, cursor_x, cursor_y);
    contact_runtime_detail::maybe_report(contact_runtime_detail::state());
    return result;
}

} // namespace touchplus::depth

#define point_depth point_depth_touch_contact_wrapper
#define compute_depth_heatmap compute_depth_heatmap_touch_contact_wrapper
