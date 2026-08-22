#pragma once

// Phase 2B.10B shadow-only stereo evaluation.
//
// A = the accepted Phase 2B.9C.2 modern fused fingertip and its existing metric
//     result. It remains the ONLY authoritative runtime output.
// B = the accepted 2B.10A Ractiv-style refined 2D fingertip, evaluated through
//     the same robust Touch+ stereo primitives for comparison only.
//
// This helper MUST NOT mutate tracker state, smoothing, calibration, Q, surface
// frame, contact semantics, cursor/touch output, or any Phase 2B accepted result.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace touchplus::tracking {

struct ShadowStereoResultV10B {
    bool attempted = false;
    bool valid = false;
    int pixel_x = -1;
    int pixel_y = -1;
    int refinement_support = 0;
    std::string stereo_confidence = "NOT_RUN";
    std::string reason = "not-run";
    touchplus::surface::SurfacePoint raw_tip{};
};

namespace shadow_stereo_detail_v10b {

inline double median_value(std::vector<double> values) {
    if (values.empty()) return std::numeric_limits<double>::quiet_NaN();
    const std::size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(mid), values.end());
    double out = values[mid];
    if ((values.size() & 1U) == 0U) {
        const auto lo = std::max_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(mid));
        out = (*lo + out) * 0.5;
    }
    return out;
}

inline touchplus::surface::SurfacePoint median_point(
    const std::vector<touchplus::surface::SurfacePoint>& points) {

    touchplus::surface::SurfacePoint out{};
    if (points.empty()) {
        out.x_mm = out.y_mm = out.h_mm = std::numeric_limits<double>::quiet_NaN();
        return out;
    }
    std::vector<double> xs, ys, hs;
    xs.reserve(points.size()); ys.reserve(points.size()); hs.reserve(points.size());
    for (const auto& p : points) {
        xs.push_back(p.x_mm);
        ys.push_back(p.y_mm);
        hs.push_back(p.h_mm);
    }
    out.x_mm = median_value(std::move(xs));
    out.y_mm = median_value(std::move(ys));
    out.h_mm = median_value(std::move(hs));
    return out;
}

} // namespace shadow_stereo_detail_v10b

inline ShadowStereoResultV10B evaluate_shadow_stereo_v10b(
    const touchplus::depth::Calibration& calibration,
    const touchplus::surface::SurfaceModel& surface,
    const std::vector<std::uint8_t>& left_gray,
    const std::vector<std::uint8_t>& right_gray,
    const touchplus::depth::DepthWorkspace& workspace,
    const std::vector<std::uint8_t>& selected_mask,
    int px,
    int py,
    const std::string& identity_confidence) {

    ShadowStereoResultV10B out;
    out.attempted = true;
    out.pixel_x = px;
    out.pixel_y = py;

    const std::size_t eye_pixels =
        static_cast<std::size_t>(touchplus::depth::kEyeWidth) * touchplus::depth::kEyeHeight;
    const std::size_t depth_cells =
        static_cast<std::size_t>(touchplus::depth::kDepthWidth) * touchplus::depth::kDepthHeight;
    if (!surface.valid || px < 0 || py < 0 ||
        left_gray.size() < eye_pixels || right_gray.size() < eye_pixels ||
        selected_mask.size() != depth_cells ||
        workspace.best_disp.size() != depth_cells ||
        workspace.best_cost.size() != depth_cells ||
        workspace.second_cost.size() != depth_cells) {
        out.stereo_confidence = "LOW";
        out.reason = "invalid-input";
        return out;
    }

    const double roi_half_x =
        surface.spread_x_mm >= 80.0 ? surface.spread_x_mm * 0.5 + 70.0 : 280.0;
    const double roi_half_y =
        surface.spread_y_mm >= 80.0 ? surface.spread_y_mm * 0.5 + 70.0 : 260.0;

    constexpr int radius = 2;
    constexpr int area = (radius * 2 + 1) * (radius * 2 + 1);
    constexpr int max_average_cost = 44;
    constexpr double uniqueness = 1.08;
    constexpr int inf = std::numeric_limits<int>::max() / 4;

    int nearest_d_small = 0;
    int nearest_dist2 = std::numeric_limits<int>::max();
    const int target_gx = px / touchplus::depth::kDepthScale;
    const int target_gy = py / touchplus::depth::kDepthScale;

    for (int gy = 0; gy < touchplus::depth::kDepthHeight; ++gy) {
        for (int gx = 0; gx < touchplus::depth::kDepthWidth; ++gx) {
            const std::size_t idx =
                static_cast<std::size_t>(gy) * touchplus::depth::kDepthWidth + gx;
            if (!selected_mask[idx]) continue;

            const int d_small = workspace.best_disp[idx];
            const bool dense_valid =
                d_small > 0 &&
                workspace.best_cost[idx] <= max_average_cost * area &&
                (workspace.second_cost[idx] == inf ||
                 static_cast<double>(workspace.second_cost[idx]) >=
                     static_cast<double>(workspace.best_cost[idx]) * uniqueness);
            if (!dense_valid) continue;

            const double disparity =
                static_cast<double>(d_small * touchplus::depth::kDepthScale);
            const double u = gx * touchplus::depth::kDepthScale + 0.5;
            const double v = gy * touchplus::depth::kDepthScale + 0.5;
            const auto camera = touchplus::surface::camera_point_from_q(
                calibration, u, v, disparity);
            if (!std::isfinite(camera.x) || !std::isfinite(camera.y) || !std::isfinite(camera.z)) continue;
            const auto sp = touchplus::surface::to_surface(surface, camera);
            if (!std::isfinite(sp.h_mm) ||
                sp.h_mm < kV6MinSupportHmm || sp.h_mm > kV6MaxSupportHmm ||
                std::abs(sp.x_mm) > roi_half_x || std::abs(sp.y_mm) > roi_half_y) continue;

            const int sx = gx - target_gx;
            const int sy = gy - target_gy;
            const int d2 = sx * sx + sy * sy;
            if (d2 < nearest_dist2) {
                nearest_dist2 = d2;
                nearest_d_small = d_small;
            }
        }
    }

    if (nearest_d_small <= 0 || nearest_dist2 > 44 * 44) {
        out.stereo_confidence = "LOW";
        out.reason = "no-nearby-dense-support";
        return out;
    }

    const double coarse_disp =
        static_cast<double>(nearest_d_small * touchplus::depth::kDepthScale);
    const int min_d = std::max(
        touchplus::depth::robust_point_detail::kMinDisparity,
        static_cast<int>(std::floor(coarse_disp - 18.0)));
    const int max_d = std::min(
        touchplus::depth::robust_point_detail::kMaxDisparity,
        static_cast<int>(std::ceil(coarse_disp + 18.0)));

    std::vector<touchplus::surface::SurfacePoint> refined;
    constexpr std::array<int, 7> offsets{{-12, -8, -4, 0, 4, 8, 12}};
    for (const int oy : offsets) {
        for (const int ox : offsets) {
            const int sx = px + ox;
            const int sy = py + oy;
            if (sx < 12 || sx >= touchplus::depth::kEyeWidth - 5 ||
                sy < 5 || sy >= touchplus::depth::kEyeHeight - 5) continue;

            const int sgx = sx / touchplus::depth::kDepthScale;
            const int sgy = sy / touchplus::depth::kDepthScale;
            if (!mask_near_v5(
                    selected_mask,
                    touchplus::depth::kDepthWidth,
                    touchplus::depth::kDepthHeight,
                    sgx,
                    sgy,
                    1)) continue;

            const auto match = touchplus::depth::robust_point_detail::mutually_consistent_match(
                left_gray, right_gray, sx, sy, min_d, max_d);
            if (!match.valid) continue;

            const auto camera = touchplus::surface::camera_point_from_q(
                calibration,
                static_cast<double>(sx),
                static_cast<double>(sy),
                match.disparity);
            if (!std::isfinite(camera.x) || !std::isfinite(camera.y) || !std::isfinite(camera.z)) continue;

            const auto sp = touchplus::surface::to_surface(surface, camera);
            if (!std::isfinite(sp.h_mm) ||
                sp.h_mm < 2.0 || sp.h_mm > kV6MaxSupportHmm + 20.0 ||
                std::abs(sp.x_mm) > roi_half_x || std::abs(sp.y_mm) > roi_half_y) continue;
            refined.push_back(sp);
        }
    }

    if (!refined.empty()) {
        std::vector<double> hs;
        hs.reserve(refined.size());
        for (const auto& p : refined) hs.push_back(p.h_mm);
        const double median_h = shadow_stereo_detail_v10b::median_value(std::move(hs));
        std::vector<touchplus::surface::SurfacePoint> consistent;
        consistent.reserve(refined.size());
        for (const auto& p : refined) {
            if (std::abs(p.h_mm - median_h) <= 22.0) consistent.push_back(p);
        }
        refined = std::move(consistent);
    }

    out.refinement_support = static_cast<int>(refined.size());
    out.stereo_confidence =
        refined.size() >= 6 ? "HIGH" : refined.size() >= 3 ? "MEDIUM" : "LOW";

    if (!final_identity_stereo_gate_v9(identity_confidence, out.stereo_confidence)) {
        out.reason = "identity-stereo-gate";
        return out;
    }

    out.raw_tip = shadow_stereo_detail_v10b::median_point(refined);
    if (!std::isfinite(out.raw_tip.x_mm) ||
        !std::isfinite(out.raw_tip.y_mm) ||
        !std::isfinite(out.raw_tip.h_mm)) {
        out.stereo_confidence = "LOW";
        out.reason = "non-finite-metric";
        return out;
    }

    out.valid = true;
    out.reason = "valid-shadow-only";
    return out;
}

} // namespace touchplus::tracking
