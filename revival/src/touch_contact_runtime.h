#pragma once

// Phase 2C.1 is a semantic layer on top of the accepted Phase 2B fingertip
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
    touchplus::contact::ContactStateV1 last_logged_state = touchplus::contact::ContactStateV1::NoFinger;
    std::uint64_t report_counter = 0;
};

inline RuntimeContactState& state() {
    static thread_local RuntimeContactState value;
    return value;
}

inline void announce_once(RuntimeContactState& s) {
    if (s.announced) return;
    s.announced = true;
    std::cout << "\n[CONTACT] PHASE 2C.1 SEMANTIC CONTACT ACTIVE | OS_INJECTION=DISABLED\n";
    std::cout << "[CONTACT] Input ownership: current accepted Phase 2B fingertip only. model Z remains disabled.\n";
    std::cout << "[CONTACT] Candidate thresholds: DOWN<=12 mm, RELEASE>=22 mm, 3 near samples, 2 release samples.\n";
    std::cout << "[CONTACT] Thresholds are candidate values pending physical tuning; uncertainty fails safe.\n";
}

inline void log_transition(RuntimeContactState& s) {
    announce_once(s);
    const bool edge = s.result.event == touchplus::contact::ContactEventV1::Down ||
        s.result.event == touchplus::contact::ContactEventV1::Up;
    const bool state_changed = !s.have_logged_state || s.result.state != s.last_logged_state;
    if (!edge && !state_changed) return;

    std::cout << std::fixed << std::setprecision(1)
        << "[CONTACT] contact_state=" << touchplus::contact::contact_state_name_v1(s.result.state)
        << " | event=" << touchplus::contact::contact_event_name_v1(s.result.event)
        << " | identity_id=" << s.result.identity_id
        << " | H=" << s.result.h_mm << " mm"
        << " | h_velocity=" << s.result.h_velocity_mm_s << " mm/s"
        << " | near_count=" << s.result.near_count
        << " | release_count=" << s.result.release_count
        << " | xy_delta=" << s.result.xy_delta_mm << " mm"
        << " | reason=" << s.result.reason << "\n";

    s.last_logged_state = s.result.state;
    s.have_logged_state = true;
}

inline void update_from_tracking() {
    auto& s = state();
    auto& tracking = tracking_runtime_detail::state();
    announce_once(s);

    touchplus::contact::ContactSampleV1 sample;
    const auto& fusion = tracking.tracker.last_fusion();
    if (tracking.enabled &&
        touchplus::surface::live_surface_model().valid &&
        tracking.result.fingertip_valid &&
        fusion.identity_id != 0) {
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
        << " | identity_id=" << s.result.identity_id
        << " | H=" << s.result.h_mm << " mm"
        << " | h_velocity=" << s.result.h_velocity_mm_s << " mm/s"
        << " | near_count=" << s.result.near_count
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
