#pragma once

// Phase 2B.10B shadow-only hybrid layer.
//
// Keep the accepted Phase 2B.9C.2 tracker and all metric/stereo behavior intact.
// The Ractiv-inspired full-resolution distal refiner is physically validated as
// a useful 2D diagnostic. In 2B.10B its accepted point is ALSO evaluated through
// the same robust Touch+ stereo primitives in parallel, but the B result remains
// telemetry only and MUST NOT replace A, alter smoothing, surface XYZ/H, contact
// semantics, or enable any OS output.

#include "depth_surface_frame_runtime.h"
#include "fingertip_refiner_v10.h"
#include "fingertip_stereo_shadow_v10b.h"

#ifdef point_depth
#undef point_depth
#endif
#ifdef compute_depth_heatmap
#undef compute_depth_heatmap
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

namespace touchplus::depth {
namespace hybrid_refiner_runtime_detail_v10 {

struct RuntimeStateV10 {
    bool previous_b_down = false;
    bool background_learning = false;
    bool background_ready = false;
    int background_frames = 0;
    std::vector<std::uint32_t> background_sum;
    std::vector<std::uint8_t> background_left;
    touchplus::tracking::DistalRefineResultV10 last{};
    touchplus::tracking::ShadowStereoResultV10B shadow{};
    std::uint64_t attempts = 0;
    std::uint64_t accepts = 0;
    std::uint64_t shadow_attempts = 0;
    std::uint64_t shadow_valid = 0;
    std::uint64_t both_valid = 0;
    std::uint64_t a_only_valid = 0;
    std::uint64_t b_only_valid = 0;
    std::uint64_t report_counter = 0;
};

inline RuntimeStateV10& state() {
    static thread_local RuntimeStateV10 value;
    return value;
}

inline void start_background(RuntimeStateV10& s) {
    const std::size_t eye_pixels =
        static_cast<std::size_t>(kEyeWidth) * kEyeHeight;
    s.background_learning = true;
    s.background_ready = false;
    s.background_frames = 0;
    s.background_sum.assign(eye_pixels, 0u);
    s.background_left.clear();
    s.last = {};
    s.shadow = {};
    s.attempts = 0;
    s.accepts = 0;
    s.shadow_attempts = 0;
    s.shadow_valid = 0;
    s.both_valid = 0;
    s.a_only_valid = 0;
    s.b_only_valid = 0;
    std::cout
        << "[HYBRID] 2B.10B refiner background learning started | "
        << "shadow_ab=1 authoritative=A metric_output_unchanged=1 OS_INJECTION=DISABLED\n";
}

inline void maybe_start_background(RuntimeStateV10& s) {
    const bool down = (GetAsyncKeyState('B') & 0x8000) != 0;
    const bool rising = down && !s.previous_b_down;
    s.previous_b_down = down;
    if (rising) start_background(s);
}

inline void accumulate_background(
    RuntimeStateV10& s,
    const std::vector<std::uint8_t>& left_gray) {

    if (!s.background_learning) return;
    const std::size_t eye_pixels =
        static_cast<std::size_t>(kEyeWidth) * kEyeHeight;
    if (left_gray.size() < eye_pixels || s.background_sum.size() != eye_pixels) {
        s.background_learning = false;
        s.background_ready = false;
        s.background_sum.clear();
        std::cout << "[HYBRID] refiner background aborted: invalid LEFT frame size.\n";
        return;
    }

    for (std::size_t i = 0; i < eye_pixels; ++i) {
        s.background_sum[i] += left_gray[i];
    }
    ++s.background_frames;

    if (s.background_frames < touchplus::tracking::kV5BackgroundFrames) return;

    s.background_left.resize(eye_pixels);
    for (std::size_t i = 0; i < eye_pixels; ++i) {
        s.background_left[i] = static_cast<std::uint8_t>(
            s.background_sum[i] /
            static_cast<std::uint32_t>(s.background_frames));
    }
    s.background_sum.clear();
    s.background_learning = false;
    s.background_ready = true;
    std::cout
        << "[HYBRID] 2B.10B refiner background READY | frames="
        << s.background_frames
        << " | A remains authoritative; B stereo is shadow telemetry only.\n";
}

inline void overlay_refined_candidate(
    std::vector<std::uint8_t>& heatmap_bgra,
    const touchplus::tracking::DistalRefineResultV10& refined) {

    if (!refined.accepted || refined.refined_x < 0 || refined.refined_y < 0) return;
    if (heatmap_bgra.size() <
        static_cast<std::size_t>(kDepthWidth) * kDepthHeight * 4) return;

    const int gx = refined.refined_x / kDepthScale;
    const int gy = refined.refined_y / kDepthScale;
    auto set_green = [&](int x, int y) {
        if (x < 0 || x >= kDepthWidth || y < 0 || y >= kDepthHeight) return;
        const std::size_t i =
            (static_cast<std::size_t>(y) * kDepthWidth + x) * 4;
        // BGRA: green.
        heatmap_bgra[i] = 0;
        heatmap_bgra[i + 1] = 255;
        heatmap_bgra[i + 2] = 0;
        heatmap_bgra[i + 3] = 255;
    };

    for (int d = -7; d <= 7; ++d) {
        set_green(gx + d, gy + d);
        set_green(gx + d, gy - d);
    }
    for (int d = -3; d <= 3; ++d) {
        set_green(gx + d, gy);
        set_green(gx, gy + d);
    }
}

inline void run_refiner(
    RuntimeStateV10& hybrid,
    touchplus::depth::tracking_runtime_detail::RuntimeState& modern,
    const Calibration& calibration,
    const std::vector<std::uint8_t>& left_gray,
    const std::vector<std::uint8_t>& right_gray,
    const DepthWorkspace& workspace,
    std::vector<std::uint8_t>& heatmap_bgra) {

    hybrid.last = {};
    hybrid.shadow = {};
    if (hybrid.background_learning) {
        accumulate_background(hybrid, left_gray);
        return;
    }
    if (!hybrid.background_ready) return;
    if (!modern.enabled || !modern.tracker.background_ready()) return;

    const auto& fusion = modern.tracker.last_fusion();
    const auto& identity = modern.tracker.last_identity();
    const auto& anatomy = modern.tracker.last_anatomy_observation();
    const auto& mask = modern.tracker.selected_mask();

    // Hard ownership boundary: 2B.10B can refine only an already-published
    // modern fused identity. It can never invent/reacquire a fingertip itself.
    if (!fusion.publish || fusion.pixel_x < 0 || fusion.pixel_y < 0 ||
        !identity.palm_valid || mask.empty()) {
        return;
    }

    ++hybrid.attempts;
    const double palm_x =
        identity.palm_gx * kDepthScale + 1.0;
    const double palm_y =
        identity.palm_gy * kDepthScale + 1.0;

    hybrid.last = touchplus::tracking::refine_distal_tip_v10(
        left_gray,
        hybrid.background_left,
        kEyeWidth,
        kEyeHeight,
        mask,
        kDepthWidth,
        kDepthHeight,
        kDepthScale,
        fusion.pixel_x,
        fusion.pixel_y,
        palm_x,
        palm_y,
        anatomy.axis_dx,
        anatomy.axis_dy);

    if (!hybrid.last.accepted) return;

    ++hybrid.accepts;
    overlay_refined_candidate(heatmap_bgra, hybrid.last);

    // 2B.10B SHADOW ONLY: run B through the same robust stereo primitives used
    // by the accepted tracker. The returned point is never written into modern
    // tracker state, smoothing, runtime.result, cursor depth, contact, or output.
    ++hybrid.shadow_attempts;
    const auto& surface = touchplus::surface::live_surface_model();
    hybrid.shadow = touchplus::tracking::evaluate_shadow_stereo_v10b(
        calibration,
        surface,
        left_gray,
        right_gray,
        workspace,
        mask,
        hybrid.last.refined_x,
        hybrid.last.refined_y,
        modern.tracker.identity_confidence());

    const bool a_valid = modern.result.fingertip_valid;
    const bool b_valid = hybrid.shadow.valid;
    if (b_valid) ++hybrid.shadow_valid;
    if (a_valid && b_valid) ++hybrid.both_valid;
    else if (a_valid) ++hybrid.a_only_valid;
    else if (b_valid) ++hybrid.b_only_valid;
}

inline void maybe_report(RuntimeStateV10& hybrid) {
    ++hybrid.report_counter;
    if ((hybrid.report_counter % 30) != 0) return;

    if (hybrid.background_learning) {
        std::cout
            << "[HYBRID] heartbeat | mode=2B.10B_SHADOW_AB"
            << " | background=LEARNING "
            << hybrid.background_frames << "/"
            << touchplus::tracking::kV5BackgroundFrames
            << " | authoritative=A B_output=DISABLED\n";
        return;
    }
    if (!hybrid.background_ready) {
        std::cout
            << "[HYBRID] heartbeat | mode=2B.10B_SHADOW_AB"
            << " | background=NOT_READY | press B with clear still scene"
            << " | authoritative=A B_output=DISABLED\n";
        return;
    }

    auto& modern = touchplus::depth::tracking_runtime_detail::state();
    const auto& a = modern.result;
    const auto& r = hybrid.last;
    const auto& b = hybrid.shadow;

    std::cout
        << std::fixed << std::setprecision(1)
        << "[HYBRID] heartbeat | mode=2B.10B_SHADOW_AB"
        << " | refiner="
        << touchplus::tracking::distal_refine_status_name_v10(r.status)
        << " | coarse=" << r.coarse_x << "," << r.coarse_y
        << " refined=" << r.refined_x << "," << r.refined_y
        << " shift_px=" << r.shift_px
        << " forward_px=" << r.forward_px
        << " lateral_px=" << r.lateral_px
        << " | A=" << (a.fingertip_valid ? "VALID" : "INVALID")
        << "/" << modern.tracker.stereo_confidence()
        << " support=" << a.refinement_support;

    if (a.fingertip_valid) {
        std::cout
            << " rawXYZ=(" << a.raw_tip.x_mm << "," << a.raw_tip.y_mm
            << ",H=" << a.raw_tip.h_mm << ")";
    }

    std::cout
        << " | B=" << (b.valid ? "VALID" : (b.attempted ? "INVALID" : "NOT_RUN"))
        << "/" << b.stereo_confidence
        << " support=" << b.refinement_support
        << " reason=" << b.reason;

    if (b.valid) {
        std::cout
            << " rawXYZ=(" << b.raw_tip.x_mm << "," << b.raw_tip.y_mm
            << ",H=" << b.raw_tip.h_mm << ")";
    }

    if (a.fingertip_valid && b.valid) {
        const double dx = b.raw_tip.x_mm - a.raw_tip.x_mm;
        const double dy = b.raw_tip.y_mm - a.raw_tip.y_mm;
        const double dh = b.raw_tip.h_mm - a.raw_tip.h_mm;
        const double dxyz = std::sqrt(dx * dx + dy * dy + dh * dh);
        std::cout
            << " | BminusA=(dX=" << dx << ",dY=" << dy
            << ",dH=" << dh << ",dXYZ=" << dxyz << ")";
    }

    std::cout
        << " | counts refine=" << hybrid.accepts << "/" << hybrid.attempts
        << " shadow=" << hybrid.shadow_valid << "/" << hybrid.shadow_attempts
        << " both=" << hybrid.both_valid
        << " A_only=" << hybrid.a_only_valid
        << " B_only=" << hybrid.b_only_valid
        << " | authoritative=A metric_output=UNCHANGED OS_INJECTION=DISABLED\n";
}

} // namespace hybrid_refiner_runtime_detail_v10

inline void compute_depth_heatmap_hybrid_v10_wrapper(
    const Calibration& c,
    const std::vector<uint8_t>& left,
    const std::vector<uint8_t>& right,
    DepthWorkspace& workspace) {

    // Run the exact accepted 2B.9C.2 implementation first. A is authoritative.
    touchplus::depth::compute_depth_heatmap_fingertip_wrapper(
        c, left, right, workspace);

    auto& modern = tracking_runtime_detail::state();
    auto& hybrid = hybrid_refiner_runtime_detail_v10::state();
    hybrid_refiner_runtime_detail_v10::run_refiner(
        hybrid, modern, c, left, right, workspace, workspace.heatmap_bgra);
}

inline PointDepth point_depth_surface_runtime_hybrid_v10_wrapper(
    const Calibration& c,
    const std::vector<uint8_t>& left,
    const std::vector<uint8_t>& right,
    int cursor_x,
    int cursor_y) {

    auto& hybrid = hybrid_refiner_runtime_detail_v10::state();
    hybrid_refiner_runtime_detail_v10::maybe_start_background(hybrid);

    // Preserve all accepted Phase 2A/2B keyboard handling/reporting and return
    // exactly A's point-depth result. B is shadow telemetry only.
    PointDepth result = touchplus::depth::point_depth_surface_runtime_wrapper(
        c, left, right, cursor_x, cursor_y);

    hybrid_refiner_runtime_detail_v10::maybe_report(hybrid);
    return result;
}

} // namespace touchplus::depth

#define point_depth point_depth_surface_runtime_hybrid_v10_wrapper
#define compute_depth_heatmap compute_depth_heatmap_hybrid_v10_wrapper
