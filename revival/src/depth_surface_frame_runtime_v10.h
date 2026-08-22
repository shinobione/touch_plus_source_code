#pragma once

// Phase 2B.10A diagnostic-only hybrid layer.
//
// Keep the accepted Phase 2B.9C.2 tracker and all metric/stereo behavior intact,
// then evaluate a Ractiv-inspired full-resolution distal refiner beside it.
// The green refined point is telemetry/overlay only in 2B.10A: it MUST NOT
// replace the modern fused pixel, feed stereo/Q, change surface XYZ/H, contact
// semantics, or enable any OS output.

#include "depth_surface_frame_runtime.h"
#include "fingertip_refiner_v10.h"

#ifdef point_depth
#undef point_depth
#endif
#ifdef compute_depth_heatmap
#undef compute_depth_heatmap
#endif

#include <algorithm>
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
    std::uint64_t attempts = 0;
    std::uint64_t accepts = 0;
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
    s.attempts = 0;
    s.accepts = 0;
    std::cout
        << "[HYBRID] 2B.10A refiner background learning started | "
        << "diagnostic_only=1 metric_pixel_unchanged=1 stereo_unchanged=1\n";
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
        << "[HYBRID] 2B.10A refiner background READY | frames="
        << s.background_frames
        << " | modern tracker/stereo output still unchanged.\n";
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
    const std::vector<std::uint8_t>& left_gray,
    std::vector<std::uint8_t>& heatmap_bgra) {

    hybrid.last = {};
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

    // Hard ownership boundary: 2B.10A can refine only an already-published
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

    if (hybrid.last.accepted) {
        ++hybrid.accepts;
        overlay_refined_candidate(heatmap_bgra, hybrid.last);
    }
}

inline void maybe_report(RuntimeStateV10& hybrid) {
    ++hybrid.report_counter;
    if ((hybrid.report_counter % 30) != 0) return;

    if (hybrid.background_learning) {
        std::cout
            << "[HYBRID] heartbeat | mode=2B.10A_DIAGNOSTIC_ONLY"
            << " | background=LEARNING "
            << hybrid.background_frames << "/"
            << touchplus::tracking::kV5BackgroundFrames
            << " | metric_pixel=UNCHANGED stereo=UNCHANGED\n";
        return;
    }
    if (!hybrid.background_ready) {
        std::cout
            << "[HYBRID] heartbeat | mode=2B.10A_DIAGNOSTIC_ONLY"
            << " | background=NOT_READY | press B with clear still scene"
            << " | metric_pixel=UNCHANGED stereo=UNCHANGED\n";
        return;
    }

    const auto& r = hybrid.last;
    std::cout
        << std::fixed << std::setprecision(1)
        << "[HYBRID] heartbeat | mode=2B.10A_DIAGNOSTIC_ONLY"
        << " | refiner="
        << touchplus::tracking::distal_refine_status_name_v10(r.status)
        << " | coarse=" << r.coarse_x << "," << r.coarse_y
        << " | refined=" << r.refined_x << "," << r.refined_y
        << " | shift_px=" << r.shift_px
        << " | forward_px=" << r.forward_px
        << " | lateral_px=" << r.lateral_px
        << " | component_px=" << r.component_pixels
        << " | attempts=" << hybrid.attempts
        << " accepts=" << hybrid.accepts
        << " | metric_pixel=UNCHANGED stereo=UNCHANGED OS_INJECTION=DISABLED\n";
}

} // namespace hybrid_refiner_runtime_detail_v10

inline void compute_depth_heatmap_hybrid_v10_wrapper(
    const Calibration& c,
    const std::vector<uint8_t>& left,
    const std::vector<uint8_t>& right,
    DepthWorkspace& workspace) {

    // Run the exact accepted 2B.9C.2 implementation first.
    touchplus::depth::compute_depth_heatmap_fingertip_wrapper(
        c, left, right, workspace);

    auto& modern = tracking_runtime_detail::state();
    auto& hybrid = hybrid_refiner_runtime_detail_v10::state();
    hybrid_refiner_runtime_detail_v10::run_refiner(
        hybrid, modern, left, workspace.heatmap_bgra);
}

inline PointDepth point_depth_surface_runtime_hybrid_v10_wrapper(
    const Calibration& c,
    const std::vector<uint8_t>& left,
    const std::vector<uint8_t>& right,
    int cursor_x,
    int cursor_y) {

    auto& hybrid = hybrid_refiner_runtime_detail_v10::state();
    hybrid_refiner_runtime_detail_v10::maybe_start_background(hybrid);

    // Preserve all accepted Phase 2A/2B keyboard handling/reporting.
    PointDepth result = touchplus::depth::point_depth_surface_runtime_wrapper(
        c, left, right, cursor_x, cursor_y);

    hybrid_refiner_runtime_detail_v10::maybe_report(hybrid);
    return result;
}

} // namespace touchplus::depth

#define point_depth point_depth_surface_runtime_hybrid_v10_wrapper
#define compute_depth_heatmap compute_depth_heatmap_hybrid_v10_wrapper
