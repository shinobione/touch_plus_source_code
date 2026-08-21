#pragma once

// Phase 2C.1C is a semantic layer on top of the accepted Phase 2B fingertip
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

struct RuntimeOcclusionProxyV1 {
    bool valid = false;
    int tip_x = -1;
    int tip_y = -1;
    std::string reason = "occlusion-proxy-unavailable";
};

struct RuntimeContactState {
    touchplus::contact::TouchContactDetectorV1 detector;
    touchplus::contact::ContactIdentityContinuityV1 identity_continuity;
    touchplus::contact::ContactIdentityDecisionV1 identity_decision;
    touchplus::contact::ContactResultV1 result;
    std::uint64_t raw_identity_id = 0;
    touchplus::contact::ContactIdentitySourceV1 raw_identity_source = touchplus::contact::ContactIdentitySourceV1::Unknown;
    std::string identity_alias_reason = "identity-unavailable";
    bool occlusion_proxy_valid = false;
    std::string occlusion_proxy_reason = "occlusion-proxy-unavailable";
    bool announced = false;
    bool have_logged_state = false;
    bool have_logged_input = false;
    std::uint64_t last_logged_raw_identity_id = 0;
    std::uint64_t last_logged_contact_identity_id = 0;
    touchplus::contact::ContactIdentitySourceV1 last_logged_identity_source = touchplus::contact::ContactIdentitySourceV1::Unknown;
    touchplus::contact::ContactOcclusionStateV1 last_logged_bridge = touchplus::contact::ContactOcclusionStateV1::Disarmed;
    bool last_logged_proxy_valid = false;
    std::string last_logged_alias_reason;
    std::string last_logged_proxy_reason;
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
    std::cout << "\n[CONTACT] PHASE 2C.1C CONTACT OCCLUSION BRIDGE ACTIVE | OS_INJECTION=DISABLED\n";
    std::cout << "[CONTACT] Input ownership: accepted Phase 2B fingertip only. model Z remains disabled.\n";
    std::cout << "[CONTACT] Metric path unchanged: DOWN<=12 mm, RELEASE>=22 mm, 3 VALID near samples.\n";
    std::cout << "[CONTACT] Occlusion bridge: two recent VALID descending metric samples may ARM only when the terminal trajectory predicts the surface.\n";
    std::cout << "[CONTACT] Two current 2D-coherent occlusion frames may then confirm contact; they NEVER add H/XY or increment near_count.\n";
    std::cout << "[CONTACT] Bridge hold requires a current compatible 2D proxy every invalid frame; hard loss/contradiction remains fail-safe UP.\n";
    std::cout << "[CONTACT] Phase 2B, K/D/R/T/P/Q, surface frame and stereo matcher remain unchanged.\n";
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

inline RuntimeOcclusionProxyV1 derive_contact_occlusion_proxy(
    RuntimeContactState& s,
    touchplus::contact::ContactInputStatusV1 input_status) {

    RuntimeOcclusionProxyV1 out;
    if (!touchplus::contact::transient_contact_gap_v1(input_status)) {
        out.reason = "occlusion-proxy-hard-input";
        return out;
    }
    if (s.identity_continuity.current_contact_identity_id() == 0) {
        out.reason = "occlusion-proxy-no-contact-identity";
        return out;
    }

    auto& tracking = tracking_runtime_detail::state();
    const auto& geometry_obs = tracking.tracker.last_identity();
    const auto& geometry_decision = tracking.tracker.last_decision();
    const auto& anatomy_obs = tracking.tracker.last_anatomy_observation();
    const auto& anatomy_decision = tracking.tracker.last_anatomy_decision();
    const auto& fusion = tracking.tracker.last_fusion();
    const auto& mask = tracking.tracker.selected_mask();

    if (!tracking.result.hand_valid || !geometry_obs.hand_valid || !geometry_obs.palm_valid ||
        geometry_decision.palm_rejected || mask.empty()) {
        out.reason = "occlusion-proxy-palm-or-hand-invalid";
        return out;
    }

    int tip_x = -1;
    int tip_y = -1;
    const bool locked_anatomy_proxy =
        anatomy_decision.publish && anatomy_decision.has_candidate &&
        anatomy_decision.state == touchplus::tracking::AnatomyTrackStateV9::Locked &&
        !anatomy_decision.explicit_reject && !anatomy_decision.stale && !anatomy_decision.jump_rejected &&
        anatomy_decision.age_frames <= 1 &&
        (anatomy_decision.sync_status == touchplus::tracking::AnatomySyncStatusV9::CurrentFrame ||
         anatomy_decision.sync_status == touchplus::tracking::AnatomySyncStatusV9::MotionCompensated);

    if (locked_anatomy_proxy) {
        tip_x = anatomy_decision.tip_x;
        tip_y = anatomy_decision.tip_y;
        out.reason = "occlusion-proxy-locked-anatomy";
    } else {
        // Contact can erase the free-space distal boundary itself. 2C.1C may use
        // that *current-frame* rejection only as a non-metric occlusion proxy,
        // never as a fingertip publication or depth source. Age>0 is forbidden so
        // the old 2B.9C.1 stale-tip failure class cannot re-enter through this path.
        const bool contact_boundary_collapse =
            anatomy_obs.status == touchplus::tracking::AnatomyStatusV9::Rejected &&
            anatomy_obs.sync_status == touchplus::tracking::AnatomySyncStatusV9::TipNotCurrentDistal &&
            anatomy_obs.age_frames == 0 && anatomy_obs.tip_x >= 0 && anatomy_obs.tip_y >= 0 &&
            anatomy_obs.hand_confidence >= 0.95 && anatomy_obs.axis_quality >= 0.85 &&
            anatomy_obs.continuity >= 0.80 &&
            anatomy_decision.state == touchplus::tracking::AnatomyTrackStateV9::Locked &&
            anatomy_decision.anatomy_id != 0;
        if (!contact_boundary_collapse) {
            out.reason = "occlusion-proxy-no-current-2d-tip";
            return out;
        }
        tip_x = anatomy_obs.tip_x;
        tip_y = anatomy_obs.tip_y;
        out.reason = "occlusion-proxy-current-tip-not-distal";
    }

    if (tip_x < 0 || tip_y < 0 ||
        !touchplus::tracking::mask_near_fullres_v9(mask, kDepthWidth, kDepthHeight,
                                                   tip_x, tip_y, kDepthScale, 2)) {
        out.reason = "occlusion-proxy-outside-current-silhouette";
        return out;
    }

    constexpr std::uint64_t anatomy_high_bit = 0x8000000000000000ULL;
    if (anatomy_decision.anatomy_id != 0 && s.identity_continuity.anatomy_raw_id() != 0) {
        const std::uint64_t current_anatomy_raw = anatomy_high_bit | anatomy_decision.anatomy_id;
        if (current_anatomy_raw != s.identity_continuity.anatomy_raw_id()) {
            out.reason = "occlusion-proxy-anatomy-id-contradiction";
            return out;
        }
    }

    if (fusion.identity_id != 0) {
        const auto source = contact_identity_source_from_fusion(fusion.mode);
        if (source == touchplus::contact::ContactIdentitySourceV1::GeometryAnatomy &&
            s.identity_continuity.geometry_raw_id() != 0 &&
            fusion.identity_id != s.identity_continuity.geometry_raw_id()) {
            out.reason = "occlusion-proxy-geometry-id-contradiction";
            return out;
        }
        if (source == touchplus::contact::ContactIdentitySourceV1::AnatomyOnly &&
            s.identity_continuity.anatomy_raw_id() != 0 &&
            fusion.identity_id != s.identity_continuity.anatomy_raw_id()) {
            out.reason = "occlusion-proxy-anatomy-raw-contradiction";
            return out;
        }
    }

    out.valid = true;
    out.tip_x = tip_x;
    out.tip_y = tip_y;
    return out;
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
    const bool bridge_changed = s.result.contact_bridge != s.last_logged_bridge ||
        s.occlusion_proxy_valid != s.last_logged_proxy_valid ||
        s.occlusion_proxy_reason != s.last_logged_proxy_reason;
    if (!edge && !state_changed && !input_changed && !identity_changed && !bridge_changed) return;

    std::cout << std::fixed << std::setprecision(1)
        << "[CONTACT] contact_state=" << touchplus::contact::contact_state_name_v1(s.result.state)
        << " | event=" << touchplus::contact::contact_event_name_v1(s.result.event)
        << " | input=" << touchplus::contact::contact_input_status_name_v1(s.result.input_status)
        << " | raw_identity_id=" << s.raw_identity_id
        << " | contact_identity_id=" << s.result.identity_id
        << " | identity_source=" << touchplus::contact::contact_identity_source_name_v1(s.raw_identity_source)
        << " | identity_alias=" << s.identity_alias_reason
        << " | contact_bridge=" << touchplus::contact::contact_occlusion_state_name_v1(s.result.contact_bridge)
        << " | occlusion_proxy=" << (s.occlusion_proxy_valid ? "VALID" : "NONE")
        << " | proxy_reason=" << s.occlusion_proxy_reason
        << " | H=" << s.result.h_mm << " mm"
        << " | h_velocity=" << s.result.h_velocity_mm_s << " mm/s"
        << " | near_count=" << s.result.near_count
        << " | precontact_valid=" << s.result.precontact_valid_count
        << " | last_valid_H=" << s.result.last_valid_h_mm << " mm"
        << " | terminal_drop=" << s.result.terminal_drop_mm << " mm"
        << " | predicted_H=" << s.result.predicted_next_h_mm << " mm"
        << " | occlusion_age=" << s.result.occlusion_age_frames
        << " | occlusion_confirm=" << s.result.occlusion_confirm_count
        << " | occlusion_tip_delta=" << s.result.occlusion_tip_delta_px << " px"
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
    s.last_logged_bridge = s.result.contact_bridge;
    s.last_logged_proxy_valid = s.occlusion_proxy_valid;
    s.last_logged_alias_reason = s.identity_alias_reason;
    s.last_logged_proxy_reason = s.occlusion_proxy_reason;
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
    s.occlusion_proxy_valid = false;
    s.occlusion_proxy_reason = "occlusion-proxy-unavailable";

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
            sample.tip_x = fusion.pixel_x;
            sample.tip_y = fusion.pixel_y;
        } else {
            sample.input_status = touchplus::contact::ContactInputStatusV1::IdentityUnknown;
            s.identity_alias_reason = "identity-adapter-rejected-valid-sample";
            s.identity_continuity.note_invalid(touchplus::contact::ContactIdentityInterruptionV1::Transient);
        }
    } else {
        const auto proxy = derive_contact_occlusion_proxy(s, sample.input_status);
        s.occlusion_proxy_valid = proxy.valid;
        s.occlusion_proxy_reason = proxy.reason;
        if (proxy.valid) {
            sample.contact_occlusion_proxy = true;
            sample.occlusion_identity_compatible = true;
            sample.tip_x = proxy.tip_x;
            sample.tip_y = proxy.tip_y;
        }

        s.identity_continuity.note_invalid(
            hard_contact_identity_interruption(sample.input_status)
                ? touchplus::contact::ContactIdentityInterruptionV1::Hard
                : touchplus::contact::ContactIdentityInterruptionV1::Transient);
        s.identity_alias_reason = hard_contact_identity_interruption(sample.input_status)
            ? "identity-hard-invalid-reset"
            : proxy.valid ? "identity-transient-contact-occlusion-proxy"
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
        << " | contact_bridge=" << touchplus::contact::contact_occlusion_state_name_v1(s.result.contact_bridge)
        << " | occlusion_proxy=" << (s.occlusion_proxy_valid ? "VALID" : "NONE")
        << " | proxy_reason=" << s.occlusion_proxy_reason
        << " | H=" << s.result.h_mm << " mm"
        << " | h_velocity=" << s.result.h_velocity_mm_s << " mm/s"
        << " | near_count=" << s.result.near_count
        << " | precontact_valid=" << s.result.precontact_valid_count
        << " | last_valid_H=" << s.result.last_valid_h_mm << " mm"
        << " | terminal_drop=" << s.result.terminal_drop_mm << " mm"
        << " | predicted_H=" << s.result.predicted_next_h_mm << " mm"
        << " | occlusion_age=" << s.result.occlusion_age_frames
        << " | occlusion_confirm=" << s.result.occlusion_confirm_count
        << " | occlusion_tip_delta=" << s.result.occlusion_tip_delta_px << " px"
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
