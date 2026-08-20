#pragma once

// Preserve the proven Phase 1C/2A include order first: depth_probe_lock.h brings
// in the hardened point matcher, then Phase 2A replaces only the surface fitter.
#include "depth_probe_lock.h"
#include "surface_frame_robust.h"
#include "depth_surface_frame.h"
#include "fingertip_tracker_v8.h"

#ifdef point_depth
#undef point_depth
#endif

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>

namespace touchplus::depth {
namespace surface_runtime_detail {

inline bool undo_requested() {
    static thread_local bool previous_u_down = false;
    const bool down = (GetAsyncKeyState('U') & 0x8000) != 0;
    const bool rising = down && !previous_u_down;
    previous_u_down = down;
    return rising;
}

inline void handle_undo() {
    if (!undo_requested()) {
        return;
    }
    auto& s = touchplus::surface::live_detail::state();
    if (s.capturing) {
        std::cout << "[SURFACE] U ignored while a C capture is running.\n";
        return;
    }
    if (s.pending_points.empty()) {
        std::cout << "[SURFACE] No pending surface point to undo.\n";
        return;
    }
    s.pending_points.pop_back();
    std::cout << "[SURFACE] UNDID last pending point. Remaining="
              << s.pending_points.size() << ".\n";
}

} // namespace surface_runtime_detail

namespace tracking_runtime_detail {

struct RuntimeState {
    bool enabled = true;
    bool previous_t_down = false;
    bool previous_b_down = false;
    bool announced = false;
    std::uint64_t report_counter = 0;
    touchplus::tracking::FingertipTrackerV8 tracker;
    touchplus::tracking::TrackingResult result;
};

inline RuntimeState& state() {
    // All tracker work is intentionally owned by the capture/depth thread.
    static thread_local RuntimeState value;
    return value;
}

inline void announce_once(RuntimeState& s) {
    if (s.announced) return;
    s.announced = true;
    std::cout << "\n[TRACK] PHASE 2B.8 RUNTIME ACTIVE"
              << " | tracker=TEMPORAL-PALM-BRANCH-ID | T toggles ON/OFF\n";
    std::cout << "[TRACK] Clear the work area, then press B once to learn 30 clean background frames.\n";
    std::cout << "[TRACK] V8 validates the palm, describes finger-like branches, then persists branch identity across frames.\n";
    std::cout << "[TRACK] Cyan circle/plus = palm observation; white cross = current 2D candidate (may still be UNKNOWN).\n";
    std::cout << "[TRACK] identity_confidence and stereo_confidence are independent; only LOCKED identity reaches stereo.\n";
    std::cout << "[TRACK] Wrong finite/HIGH anatomy remains a hard blocker; unstable identity must become UNKNOWN.\n";
}

inline bool toggle_requested(RuntimeState& s) {
    const bool down = (GetAsyncKeyState('T') & 0x8000) != 0;
    const bool rising = down && !s.previous_t_down;
    s.previous_t_down = down;
    return rising;
}

inline bool background_requested(RuntimeState& s) {
    const bool down = (GetAsyncKeyState('B') & 0x8000) != 0;
    const bool rising = down && !s.previous_b_down;
    s.previous_b_down = down;
    return rising;
}

inline void maybe_toggle(RuntimeState& s) {
    announce_once(s);
    if (!toggle_requested(s)) return;
    s.enabled = !s.enabled;
    if (!s.enabled) {
        s.result = {};
    }
    std::cout << "[TRACK] Phase 2B geometry tracker "
              << (s.enabled ? "ENABLED" : "DISABLED")
              << " (T toggles).\n";
}

inline void maybe_background(RuntimeState& s) {
    announce_once(s);
    if (!background_requested(s)) return;
    s.tracker.request_background_capture();
    s.result = {};
    std::cout << "[TRACK] BACKGROUND LEARN STARTED | remove hand / raised temporary objects and hold scene still.\n";
}

inline void overlay_coarse_candidate(
    std::vector<uint8_t>& heatmap_bgra,
    const touchplus::tracking::TrackingResult& r) {

    if (!r.hand_valid || r.fingertip_valid || r.pixel_x < 0 || r.pixel_y < 0) return;
    if (heatmap_bgra.size() < static_cast<size_t>(kDepthWidth) * kDepthHeight * 4) return;

    const int gx = r.pixel_x / kDepthScale;
    const int gy = r.pixel_y / kDepthScale;
    auto set_white = [&](int x, int y) {
        if (x < 0 || x >= kDepthWidth || y < 0 || y >= kDepthHeight) return;
        const size_t i = (static_cast<size_t>(y) * kDepthWidth + x) * 4;
        heatmap_bgra[i + 0] = 255;
        heatmap_bgra[i + 1] = 255;
        heatmap_bgra[i + 2] = 255;
        heatmap_bgra[i + 3] = 255;
    };
    for (int d = -5; d <= 5; ++d) {
        set_white(gx + d, gy);
        set_white(gx, gy + d);
    }
}

inline void overlay_palm_core(
    std::vector<uint8_t>& heatmap_bgra,
    const touchplus::tracking::IdentityObservationV8& identity) {

    if (!identity.palm_valid ||
        identity.palm_gx < 0 ||
        identity.palm_gy < 0 ||
        identity.palm_radius <= 0.0) {
        return;
    }
    if (heatmap_bgra.size() < static_cast<size_t>(kDepthWidth) * kDepthHeight * 4) return;

    auto set_cyan = [&](int x, int y) {
        if (x < 0 || x >= kDepthWidth || y < 0 || y >= kDepthHeight) return;
        const size_t i = (static_cast<size_t>(y) * kDepthWidth + x) * 4;
        heatmap_bgra[i + 0] = 255;
        heatmap_bgra[i + 1] = 255;
        heatmap_bgra[i + 2] = 0;
        heatmap_bgra[i + 3] = 255;
    };

    const int cx = identity.palm_gx;
    const int cy = identity.palm_gy;
    const double radius = identity.palm_radius;
    for (int d = -4; d <= 4; ++d) {
        set_cyan(cx + d, cy);
        set_cyan(cx, cy + d);
    }
    constexpr double pi = 3.14159265358979323846;
    for (int degrees = 0; degrees < 360; degrees += 3) {
        const double angle = degrees * pi / 180.0;
        set_cyan(
            static_cast<int>(std::lround(cx + std::cos(angle) * radius)),
            static_cast<int>(std::lround(cy + std::sin(angle) * radius)));
    }
}

inline const char* rejection_reason(
    const touchplus::tracking::IdentityObservationV8& identity,
    const touchplus::tracking::IdentityDecisionV8& decision) {

    if (!identity.palm_valid) return "palm-invalid";
    if (decision.palm_rejected) return "palm-temporal-reject";
    if (decision.jump_rejected) return "tip-jump-reject";
    if (decision.ambiguous || identity.static_ambiguous) return "ambiguous-branch";
    if (decision.association_rejected) return "branch-association-reject";
    if (decision.state == touchplus::tracking::IdentityStateV8::Acquiring) return "identity-acquiring";
    if (!decision.has_candidate) return "no-finger-like-branch";
    return "identity-unknown";
}

inline void maybe_report(RuntimeState& s) {
    announce_once(s);
    ++s.report_counter;
    if (s.report_counter % 30 != 0) return;

    if (!s.enabled) {
        std::cout << "[TRACK] heartbeat | tracker=DISABLED | press T to enable\n";
        return;
    }
    if (!touchplus::surface::live_surface_model().valid) {
        std::cout << "[TRACK] heartbeat | tracker=ENABLED | waiting for valid surface/<serial>.json\n";
        return;
    }
    if (s.tracker.background_learning()) {
        std::cout << "[TRACK] heartbeat | background=LEARNING "
                  << s.tracker.background_frames() << "/"
                  << touchplus::tracking::kV5BackgroundFrames
                  << " | keep work area clear and still\n";
        return;
    }
    if (!s.tracker.background_ready()) {
        std::cout << "[TRACK] heartbeat | background=NOT_READY | clear work area and press B\n";
        return;
    }

    const auto& r = s.result;
    const auto& identity = s.tracker.last_identity();
    const auto& decision = s.tracker.last_decision();

    if (!r.hand_valid) {
        std::cout << "[TRACK] heartbeat | background=READY | no palm-supported hand"
                  << " | changed_cells=" << r.foreground_samples
                  << " | identity=UNKNOWN/LOW | stereo=NOT_RUN\n";
        return;
    }

    std::cout << std::fixed << std::setprecision(1)
              << "[TRACK] heartbeat | background=READY | silhouette=" << r.hand_samples
              << " cells | palm=" << identity.palm_gx << "," << identity.palm_gy
              << " r=" << identity.palm_radius
              << " fill=" << identity.palm_core_fill
              << " | branches=" << identity.candidates.size()
              << " | identity_state="
              << touchplus::tracking::identity_state_name_v8(decision.state)
              << " | identity_confidence=" << s.tracker.identity_confidence()
              << " | stereo_confidence=" << s.tracker.stereo_confidence();

    if (!r.fingertip_valid) {
        if (r.pixel_x >= 0 && r.pixel_y >= 0) {
            std::cout << " | tip_pixel=" << r.pixel_x << "," << r.pixel_y;
        }
        std::cout << " | fingertip=UNKNOWN"
                  << " | reason=" << rejection_reason(identity, decision)
                  << " | association=" << decision.association_score
                  << " | tip_residual=" << decision.tip_residual
                  << " | refinement=" << r.refinement_support
                  << "\n";
        return;
    }

    std::cout << " | fingertip surface XYZ=("
              << r.smoothed_tip.x_mm << ", "
              << r.smoothed_tip.y_mm << ", H="
              << r.smoothed_tip.h_mm << ") mm"
              << " | tip_pixel=" << r.pixel_x << "," << r.pixel_y
              << " | branch_id=" << decision.branch_id
              << " | support=" << r.refinement_support
              << " | final_confidence=" << r.confidence << "\n";
}

} // namespace tracking_runtime_detail

inline void compute_depth_heatmap_fingertip_wrapper(
    const Calibration& c,
    const std::vector<uint8_t>& left,
    const std::vector<uint8_t>& right,
    DepthWorkspace& workspace) {

    // First run the already-accepted Phase 1C dense diagnostic unchanged.
    touchplus::depth::compute_depth_heatmap(c, left, right, workspace);

    auto& runtime = tracking_runtime_detail::state();
    if (!runtime.enabled) return;

    const auto& surface = touchplus::surface::live_surface_model();
    if (!surface.valid) {
        runtime.result = {};
        return;
    }

    runtime.result = runtime.tracker.update(c, surface, left, right, workspace);
    touchplus::tracking::overlay_tracking(
        workspace.heatmap_bgra,
        runtime.tracker.selected_mask(),
        runtime.result);
    tracking_runtime_detail::overlay_palm_core(
        workspace.heatmap_bgra,
        runtime.tracker.last_identity());
    tracking_runtime_detail::overlay_coarse_candidate(
        workspace.heatmap_bgra,
        runtime.result);
}

inline PointDepth point_depth_surface_runtime_wrapper(
    const Calibration& c,
    const std::vector<uint8_t>& left,
    const std::vector<uint8_t>& right,
    int cursor_x,
    int cursor_y) {

    surface_runtime_detail::handle_undo();

    // Phase 2B controls are serviced on the every-frame point path so T/B do
    // not depend on whether the half-rate dense-depth display is currently on.
    auto& tracking = tracking_runtime_detail::state();
    tracking_runtime_detail::maybe_toggle(tracking);
    tracking_runtime_detail::maybe_background(tracking);

    PointDepth result = touchplus::surface::point_depth_surface_wrapper(
        c, left, right, cursor_x, cursor_y);
    tracking_runtime_detail::maybe_report(tracking);
    return result;
}

} // namespace touchplus::depth

// Keep depth_viewer.cpp on its proven persistent capture/render path. Redirect
// only the two extension seams needed by Phase 2A/2B.
#define point_depth point_depth_surface_runtime_wrapper
#define compute_depth_heatmap compute_depth_heatmap_fingertip_wrapper
