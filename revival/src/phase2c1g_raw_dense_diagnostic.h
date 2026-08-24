#pragma once

// Phase 2C.1G diagnostic-only wrapper.
//
// Phase 2C.1F closed the simple low-texture-threshold path: authoritative
// TextureLow probes did not survive a complete shadow forward/reverse/LR check
// during real physical contact. 2C.1G therefore leaves the full-resolution
// matcher untouched and inspects a different signal already present in the
// accepted stack: half-resolution dense stereo BEFORE the V6 support H floor
// (kV6MinSupportHmm = 8 mm) is applied.
//
// This wrapper always runs the exact accepted 2B.10D/2C runtime first, then
// observes dense cells around the already-published fused fingertip. It reuses
// the same dense cost/uniqueness validity rule as FingertipTrackerV9, applies Q
// and the accepted surface transform, keeps the accepted surface ROI, but does
// NOT apply the V6 H>=8 mm support floor. It is telemetry-only and cannot feed
// identity, stereo refinement, smoothing, contact semantics, promotion or OS
// output.

#include "depth_surface_frame_runtime_v10.h"

#ifdef compute_depth_heatmap
#undef compute_depth_heatmap
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <vector>

namespace touchplus::depth {
namespace raw_dense_diagnostic_detail_v2c1g {

constexpr int kLocalRadiusCellsV2C1G = 12;
constexpr int kLocalRadiusPxV2C1G = kLocalRadiusCellsV2C1G * kDepthScale;

struct RawDenseDiagnosticV2C1G {
    bool attempted = false;
    std::uint32_t frame = 0;
    int target_x = -1;
    int target_y = -1;
    int raw_dense_count = 0;
    double nearest_raw_dense_px = std::numeric_limits<double>::quiet_NaN();
    double nearest_raw_dense_h_mm = std::numeric_limits<double>::quiet_NaN();
    double nearest_raw_dense_disparity_px = std::numeric_limits<double>::quiet_NaN();
    double local_h_min_mm = std::numeric_limits<double>::quiet_NaN();
    double local_h_p25_mm = std::numeric_limits<double>::quiet_NaN();
    double local_h_median_mm = std::numeric_limits<double>::quiet_NaN();
};

inline RawDenseDiagnosticV2C1G evaluate_raw_dense_v2c1g(
    const Calibration& calibration,
    const DepthWorkspace& workspace) {

    RawDenseDiagnosticV2C1G out;
    auto& modern = touchplus::depth::tracking_runtime_detail::state();
    out.frame = modern.tracker.frame_id();

    if (!modern.enabled || !modern.tracker.background_ready()) return out;

    const auto& fusion = modern.tracker.last_fusion();
    const auto& mask = modern.tracker.selected_mask();
    const auto& surface = touchplus::surface::live_surface_model();
    if (!fusion.publish || fusion.pixel_x < 0 || fusion.pixel_y < 0 ||
        mask.size() != static_cast<std::size_t>(kDepthWidth) * kDepthHeight ||
        !surface.valid) {
        return out;
    }

    out.attempted = true;
    out.target_x = fusion.pixel_x;
    out.target_y = fusion.pixel_y;

    // Match FingertipTrackerV9's accepted half-resolution dense validity rule.
    constexpr int patch_radius = 2;
    constexpr int patch_area = (patch_radius * 2 + 1) * (patch_radius * 2 + 1);
    constexpr int max_average_cost = 44;
    constexpr double uniqueness = 1.08;
    constexpr int inf = std::numeric_limits<int>::max() / 4;

    constexpr int local_radius2 = kLocalRadiusCellsV2C1G * kLocalRadiusCellsV2C1G;

    const int target_gx = fusion.pixel_x / kDepthScale;
    const int target_gy = fusion.pixel_y / kDepthScale;
    const int min_gx = std::max(0, target_gx - kLocalRadiusCellsV2C1G);
    const int max_gx = std::min(kDepthWidth - 1, target_gx + kLocalRadiusCellsV2C1G);
    const int min_gy = std::max(0, target_gy - kLocalRadiusCellsV2C1G);
    const int max_gy = std::min(kDepthHeight - 1, target_gy + kLocalRadiusCellsV2C1G);

    const double roi_half_x = surface.spread_x_mm >= 80.0
        ? surface.spread_x_mm * 0.5 + 70.0 : 280.0;
    const double roi_half_y = surface.spread_y_mm >= 80.0
        ? surface.spread_y_mm * 0.5 + 70.0 : 260.0;

    int nearest_dist2 = std::numeric_limits<int>::max();
    std::vector<double> hs;
    hs.reserve((kLocalRadiusCellsV2C1G * 2 + 1) *
               (kLocalRadiusCellsV2C1G * 2 + 1));

    for (int gy = min_gy; gy <= max_gy; ++gy) {
        for (int gx = min_gx; gx <= max_gx; ++gx) {
            const int dx = gx - target_gx;
            const int dy = gy - target_gy;
            const int dist2 = dx * dx + dy * dy;
            if (dist2 > local_radius2) continue;

            const std::size_t idx = static_cast<std::size_t>(gy) * kDepthWidth + gx;
            if (!mask[idx]) continue;

            const int d_small = workspace.best_disp[idx];
            const bool dense_valid =
                d_small > 0 &&
                workspace.best_cost[idx] <= max_average_cost * patch_area &&
                (workspace.second_cost[idx] == inf ||
                 static_cast<double>(workspace.second_cost[idx]) >=
                    static_cast<double>(workspace.best_cost[idx]) * uniqueness);
            if (!dense_valid) continue;

            const double disparity = static_cast<double>(d_small * kDepthScale);
            const double u = gx * kDepthScale + 0.5;
            const double v = gy * kDepthScale + 0.5;
            const auto camera = touchplus::surface::camera_point_from_q(
                calibration, u, v, disparity);
            if (!std::isfinite(camera.x) || !std::isfinite(camera.y) ||
                !std::isfinite(camera.z)) {
                continue;
            }

            const auto sp = touchplus::surface::to_surface(surface, camera);
            if (!std::isfinite(sp.h_mm) || !std::isfinite(sp.x_mm) ||
                !std::isfinite(sp.y_mm)) {
                continue;
            }
            if (std::abs(sp.x_mm) > roi_half_x || std::abs(sp.y_mm) > roi_half_y) {
                continue;
            }

            ++out.raw_dense_count;
            hs.push_back(sp.h_mm);

            if (dist2 < nearest_dist2) {
                nearest_dist2 = dist2;
                out.nearest_raw_dense_px =
                    std::sqrt(static_cast<double>(dist2)) * kDepthScale;
                out.nearest_raw_dense_h_mm = sp.h_mm;
                out.nearest_raw_dense_disparity_px = disparity;
            }
        }
    }

    if (!hs.empty()) {
        std::sort(hs.begin(), hs.end());
        out.local_h_min_mm = hs.front();
        const std::size_t p25_index = static_cast<std::size_t>(
            std::floor(0.25 * static_cast<double>(hs.size() - 1)));
        out.local_h_p25_mm = hs[p25_index];
        const std::size_t mid = hs.size() / 2;
        out.local_h_median_mm = hs[mid];
        if ((hs.size() & 1U) == 0U) {
            out.local_h_median_mm = 0.5 * (hs[mid - 1] + hs[mid]);
        }
    }

    return out;
}

inline void report_raw_dense_v2c1g(const RawDenseDiagnosticV2C1G& d) {
    if (!d.attempted || (d.frame % 15U) != 0U) return;

    const auto print_value = [](double value) {
        if (std::isfinite(value)) std::cout << value;
        else std::cout << "nan";
    };

    std::cout << std::fixed << std::setprecision(1)
              << "[RAW_DENSE] frame=" << d.frame
              << " target=" << d.target_x << ',' << d.target_y
              << " count=" << d.raw_dense_count
              << " nearest_px=";
    print_value(d.nearest_raw_dense_px);
    std::cout << " nearest_H=";
    print_value(d.nearest_raw_dense_h_mm);
    std::cout << " nearest_disparity=";
    print_value(d.nearest_raw_dense_disparity_px);
    std::cout << " H_min=";
    print_value(d.local_h_min_mm);
    std::cout << " H_p25=";
    print_value(d.local_h_p25_mm);
    std::cout << " H_median=";
    print_value(d.local_h_median_mm);
    std::cout << " radius_px=" << kLocalRadiusPxV2C1G
              << " pre_support_H_floor=BYPASSED"
              << " authoritative=UNCHANGED"
              << " OS_INJECTION=DISABLED\n";
}

} // namespace raw_dense_diagnostic_detail_v2c1g

inline void compute_depth_heatmap_phase2c1g_wrapper(
    const Calibration& calibration,
    const std::vector<std::uint8_t>& left,
    const std::vector<std::uint8_t>& right,
    DepthWorkspace& workspace) {

    // Always run the exact accepted 2B.10D / Phase 2C runtime first.
    touchplus::depth::compute_depth_heatmap_hybrid_v10_wrapper(
        calibration, left, right, workspace);

    const auto diagnostic =
        raw_dense_diagnostic_detail_v2c1g::evaluate_raw_dense_v2c1g(
            calibration, workspace);
    raw_dense_diagnostic_detail_v2c1g::report_raw_dense_v2c1g(diagnostic);
}

} // namespace touchplus::depth

#define compute_depth_heatmap compute_depth_heatmap_phase2c1g_wrapper
