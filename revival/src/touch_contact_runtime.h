#pragma once

// Phase 2C.1A is a semantic layer on top of the accepted Phase 2B fingertip
// stream. It force-includes the complete 2B.9C.2 runtime first, then wraps its
// final valid fingertip output without changing camera/surface calibration,
// landmark ownership, stereo matching, or persistent capture.
#include "depth_surface_frame_runtime.h"
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

namespace touchplus::depth {
namespace contact_runtime_detail {

struct RuntimeContactState {
    touchplus::contact::TouchContactDetectorV1 detector;
    touchplus::contact::ContactResultV1 result;
    bool announced = false;
    bool have_logged_state = false;
    bool have_logged_input = false;
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
    std::cout << "\n[CONTACT] PHASE 2C.1A SPARSE VALIDATED CONTACT ACTIVE | OS_INJECTION=DISABLED\n";
    std::cout << "[CONTACT] Input ownership: accepted Phase 2B fingertip only. model Z remains disabled.\n";
    std::cout << "[CONTACT] Candidate thresholds: DOWN<=12 mm, RELEASE>=22 mm, 3 VALID near samples.\n";
    std::cout << "[CONTACT] Sparse gate: isolated transient UNKNOWN may preserve evidence; UNKNOWN never adds evidence or creates DOWN.\n";
    std::cout << "[CONTACT] Safety: max 1 consecutive transient gap, 5 semantic-frame evidence window; NO_HAND/identity switch/jumps hard-reset.\n";
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
    if (!edge && !state_changed && !input_changed) return;

    std::cout << std::fixed << std::setprecision(1)
        << "[CONTACT] contact_state=" << touchplus::contact::contact_state_name_v1(s.result.state)
        << " | event=" << touchplus::contact::contact_event_name_v1(s.result.event)
        << " | input=" << touchplus::contact::contact_input_status_name_v1(s.result.input_status)
        << " | identity_id=" << s.result.identity_id
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
    if (sample.input_status == touchplus::contact::ContactInputStatusV1::Valid) {
        sample.valid = true;
        sample.identity_id = fusion.identity_id;
        sample.x_mm = tracking.result.smoothed_tip.x_mm;
        sample.y_mm = tracking.result.smoothed_tip.y_mm;
        sample.h_mm = tracking.result.smoothed_tip.h_mm;
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
        << " | identity_id=" << s.result.identity_id
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
