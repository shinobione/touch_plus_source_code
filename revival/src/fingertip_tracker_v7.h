#pragma once

#include "fingertip_tracker_v6.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace touchplus::tracking {

// Phase 2B.7 — SCOPA-inspired palm-core fingertip identity.
//
// Real Touch+ smoke proved that even a support-bounded skeleton can emit a
// geometrically consistent but anatomically wrong HIGH-confidence point. The
// failure is architectural: "longest branch from the top-entry wrist" is not
// equivalent to "index finger" once palm topology, curled fingers and mask
// irregularity enter the scene.
//
// The recovered Ractiv SCOPA code solved this class of problem by estimating a
// palm center/radius from an interior distance transform, then reasoning about
// fingers outside that palm. V7 keeps that useful anatomical idea without
// importing the old OpenCV/pose/DTW stack:
//   bounded hand silhouette
//     -> chamfer distance-to-boundary palm core
//     -> remove palm disk from a thinned skeleton
//     -> connected external branches
//     -> reject the top-entry forearm branch
//     -> require one dominant finger branch (otherwise ambiguous/unknown)
//     -> extend that branch to the visible silhouette boundary
//     -> only then run the proven robust stereo XYZ refinement.
//
// Controlled Phase 2B boundary remains: one top-entry hand, one clearly
// dominant extended index. Multi-finger / weak anatomy must degrade to unknown.

constexpr double kV7PalmCutRadiusScale = 1.05;
constexpr double kV7MinPalmRadiusCells = 6.0;
constexpr double kV7MinFingerExtensionRadiusScale = 0.62;
constexpr double kV7AmbiguousLengthRatio = 0.88;
constexpr int kV7MinSkeletonBranchCells = 5;
constexpr int kV7PalmAttachBandCells = 5;

struct PalmBranchTipV7 {
    bool valid = false;
    bool ambiguous = false;
    int gx = -1;
    int gy = -1;
    int palm_gx = -1;
    int palm_gy = -1;
    double palm_radius = 0.0;
    int branch_count = 0;
    int rejected_forearm_branches = 0;
    double best_extension = 0.0;
    size_t skeleton_cells = 0;
};

inline std::vector<int> chamfer_inside_distance_v7(
    const std::vector<uint8_t>& mask,
    int width,
    int height) {

    const size_t cells = static_cast<size_t>(width) * height;
    constexpr int inf = 1 << 26;
    std::vector<int> dist(cells, 0);
    if (width <= 0 || height <= 0 || mask.size() != cells) return dist;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t idx = static_cast<size_t>(y) * width + x;
            if (!mask[idx]) {
                dist[idx] = 0;
                continue;
            }
            dist[idx] = (x == 0 || y == 0 || x == width - 1 || y == height - 1)
                ? 3 : inf;
        }
    }

    auto relax = [&](int x, int y, int nx, int ny, int cost) {
        if (nx < 0 || nx >= width || ny < 0 || ny >= height) return;
        const size_t idx = static_cast<size_t>(y) * width + x;
        const size_t ni = static_cast<size_t>(ny) * width + nx;
        dist[idx] = std::min(dist[idx], dist[ni] + cost);
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t idx = static_cast<size_t>(y) * width + x;
            if (!mask[idx]) continue;
            relax(x, y, x - 1, y, 3);
            relax(x, y, x, y - 1, 3);
            relax(x, y, x - 1, y - 1, 4);
            relax(x, y, x + 1, y - 1, 4);
        }
    }
    for (int y = height - 1; y >= 0; --y) {
        for (int x = width - 1; x >= 0; --x) {
            const size_t idx = static_cast<size_t>(y) * width + x;
            if (!mask[idx]) continue;
            relax(x, y, x + 1, y, 3);
            relax(x, y, x, y + 1, 3);
            relax(x, y, x + 1, y + 1, 4);
            relax(x, y, x - 1, y + 1, 4);
        }
    }
    return dist;
}

inline PalmBranchTipV7 palm_core_fingertip_v7(
    const std::vector<uint8_t>& hand_mask,
    int width,
    int height) {

    PalmBranchTipV7 out;
    const size_t cells = static_cast<size_t>(width) * height;
    if (width <= 0 || height <= 0 || hand_mask.size() != cells) return out;

    int min_x = width;
    int max_x = -1;
    int min_y = height;
    int max_y = -1;
    size_t hand_cells = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!hand_mask[static_cast<size_t>(y) * width + x]) continue;
            ++hand_cells;
            min_x = std::min(min_x, x);
            max_x = std::max(max_x, x);
            min_y = std::min(min_y, y);
            max_y = std::max(max_y, y);
        }
    }
    if (hand_cells < kV6MinHandCells || max_y <= min_y) return out;

    const int y_span = std::max(1, max_y - min_y);
    const int palm_search_min_y = std::min(
        max_y, min_y + std::max(8, static_cast<int>(std::lround(y_span * 0.18))));
    const int palm_search_max_y = std::max(
        palm_search_min_y, max_y - std::max(3, static_cast<int>(std::lround(y_span * 0.08))));

    const std::vector<int> boundary_dist =
        chamfer_inside_distance_v7(hand_mask, width, height);

    int palm_flat = -1;
    int palm_dist = -1;
    double palm_score = -1.0;
    for (int y = palm_search_min_y; y <= palm_search_max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const size_t idx = static_cast<size_t>(y) * width + x;
            if (!hand_mask[idx] || boundary_dist[idx] <= 0) continue;
            const double lower_bias = 0.06 *
                static_cast<double>(y - palm_search_min_y) /
                std::max(1, palm_search_max_y - palm_search_min_y);
            const double score = static_cast<double>(boundary_dist[idx]) + lower_bias;
            if (score > palm_score) {
                palm_score = score;
                palm_dist = boundary_dist[idx];
                palm_flat = static_cast<int>(idx);
            }
        }
    }
    if (palm_flat < 0) return out;

    out.palm_gx = palm_flat % width;
    out.palm_gy = palm_flat / width;
    out.palm_radius = static_cast<double>(palm_dist) / 3.0;
    if (out.palm_radius < kV7MinPalmRadiusCells) return out;

    const std::vector<uint8_t> skeleton =
        zhang_suen_skeleton_v6(hand_mask, width, height);
    out.skeleton_cells = mask_count_v6(skeleton);
    if (out.skeleton_cells < 10) return out;

    const double cut_radius = std::max(
        kV7MinPalmRadiusCells,
        out.palm_radius * kV7PalmCutRadiusScale);
    const int entry_limit = min_y + std::max(
        6, static_cast<int>(std::lround(y_span * 0.13)));

    std::vector<uint8_t> external(cells, 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t idx = static_cast<size_t>(y) * width + x;
            if (!skeleton[idx]) continue;
            const double radial = std::hypot(
                static_cast<double>(x - out.palm_gx),
                static_cast<double>(y - out.palm_gy));
            if (radial > cut_radius) external[idx] = 1;
        }
    }

    constexpr std::array<std::array<int, 2>, 8> neighbors{{
        {{-1,-1}}, {{0,-1}}, {{1,-1}}, {{-1,0}},
        {{1,0}}, {{-1,1}}, {{0,1}}, {{1,1}}
    }};

    struct Branch {
        int label = -1;
        int cells = 0;
        bool touches_top = false;
        bool attaches_palm = false;
        int tip_x = -1;
        int tip_y = -1;
        double far_radius = 0.0;
        double extension = 0.0;
    };

    std::vector<int> labels(cells, -1);
    std::vector<int> queue;
    queue.reserve(cells);
    std::vector<Branch> branches;
    int next_label = 0;

    for (int sy = 0; sy < height; ++sy) {
        for (int sx = 0; sx < width; ++sx) {
            const size_t seed = static_cast<size_t>(sy) * width + sx;
            if (!external[seed] || labels[seed] >= 0) continue;

            Branch b;
            b.label = next_label;
            queue.clear();
            queue.push_back(static_cast<int>(seed));
            labels[seed] = next_label;
            size_t head = 0;

            while (head < queue.size()) {
                const int flat = queue[head++];
                const int y = flat / width;
                const int x = flat - y * width;
                ++b.cells;
                if (y <= entry_limit) b.touches_top = true;
                const double radial = std::hypot(
                    static_cast<double>(x - out.palm_gx),
                    static_cast<double>(y - out.palm_gy));
                if (radial <= cut_radius + kV7PalmAttachBandCells) b.attaches_palm = true;
                if (radial > b.far_radius) {
                    b.far_radius = radial;
                    b.tip_x = x;
                    b.tip_y = y;
                }

                for (const auto& n : neighbors) {
                    const int nx = x + n[0];
                    const int ny = y + n[1];
                    if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
                    const size_t ni = static_cast<size_t>(ny) * width + nx;
                    if (external[ni] && labels[ni] < 0) {
                        labels[ni] = next_label;
                        queue.push_back(static_cast<int>(ni));
                    }
                }
            }
            b.extension = std::max(0.0, b.far_radius - cut_radius);
            branches.push_back(b);
            ++next_label;
        }
    }

    std::vector<Branch> fingers;
    const double min_extension = std::max(
        8.0, out.palm_radius * kV7MinFingerExtensionRadiusScale);
    for (const auto& b : branches) {
        if (b.cells < kV7MinSkeletonBranchCells || !b.attaches_palm) continue;
        if (b.touches_top) {
            ++out.rejected_forearm_branches;
            continue;
        }
        if (b.extension < min_extension || b.tip_x < 0 || b.tip_y < 0) continue;
        fingers.push_back(b);
    }
    out.branch_count = static_cast<int>(fingers.size());
    if (fingers.empty()) return out;

    std::sort(fingers.begin(), fingers.end(), [](const Branch& a, const Branch& b) {
        if (a.extension != b.extension) return a.extension > b.extension;
        return a.cells > b.cells;
    });

    const Branch& best = fingers.front();
    out.best_extension = best.extension;
    if (fingers.size() >= 2) {
        const Branch& second = fingers[1];
        const double separation = std::hypot(
            static_cast<double>(best.tip_x - second.tip_x),
            static_cast<double>(best.tip_y - second.tip_y));
        if (second.extension >= best.extension * kV7AmbiguousLengthRatio &&
            separation >= std::max(10.0, out.palm_radius * 0.65)) {
            out.ambiguous = true;
            return out;
        }
    }

    // The skeleton endpoint is anatomical direction, not necessarily the last
    // visible skin pixel. Extend in a narrow cone from the palm through the
    // winning branch and take the furthest silhouette cell on that ray family.
    double dx = static_cast<double>(best.tip_x - out.palm_gx);
    double dy = static_cast<double>(best.tip_y - out.palm_gy);
    const double dir_len = std::hypot(dx, dy);
    if (dir_len < 1e-6) return out;
    dx /= dir_len;
    dy /= dir_len;

    int visible_x = best.tip_x;
    int visible_y = best.tip_y;
    double best_projection = dir_len;
    const double max_perp = std::max(3.0, out.palm_radius * 0.42);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!hand_mask[static_cast<size_t>(y) * width + x]) continue;
            const double vx = static_cast<double>(x - out.palm_gx);
            const double vy = static_cast<double>(y - out.palm_gy);
            const double projection = vx * dx + vy * dy;
            if (projection < best_projection - 2.0) continue;
            const double perpendicular = std::abs(vx * dy - vy * dx);
            if (perpendicular > max_perp) continue;
            if (projection > best_projection) {
                best_projection = projection;
                visible_x = x;
                visible_y = y;
            }
        }
    }

    out.valid = true;
    out.gx = visible_x;
    out.gy = visible_y;
    return out;
}

class FingertipTrackerV7 {
public:
    void request_background_capture() {
        base_.request_background_capture();
        clear_tracking_only();
    }

    bool background_ready() const { return base_.background_ready(); }
    bool background_learning() const { return base_.background_learning(); }
    int background_frames() const { return base_.background_frames(); }

    TrackingResult update(
        const touchplus::depth::Calibration& calibration,
        const touchplus::surface::SurfaceModel& surface,
        const std::vector<uint8_t>& left_gray,
        const std::vector<uint8_t>& right_gray,
        const touchplus::depth::DepthWorkspace& workspace) {

        // V5 owns the learned-background appearance silhouette. Its endpoint
        // result is ignored; V7 only consumes the selected appearance mask.
        const TrackingResult base_result =
            base_.update(calibration, surface, left_gray, right_gray, workspace);

        TrackingResult out;
        selected_mask_ = base_.selected_mask();
        out.foreground_samples = base_result.foreground_samples;
        last_identity_ = {};
        if (!base_.background_ready() || selected_mask_.empty() || !base_result.hand_valid) {
            if (!base_result.hand_valid) selected_mask_.clear();
            last_result_ = out;
            return out;
        }

        const size_t depth_cells =
            static_cast<size_t>(touchplus::depth::kDepthWidth) *
            touchplus::depth::kDepthHeight;
        std::vector<uint8_t> support(depth_cells, 0);
        std::vector<int> support_disp_small(depth_cells, 0);

        const double roi_half_x = surface.spread_x_mm >= 80.0
            ? surface.spread_x_mm * 0.5 + 70.0 : 280.0;
        const double roi_half_y = surface.spread_y_mm >= 80.0
            ? surface.spread_y_mm * 0.5 + 70.0 : 260.0;

        constexpr int radius = 2;
        constexpr int area = (radius * 2 + 1) * (radius * 2 + 1);
        constexpr int max_average_cost = 44;
        constexpr double uniqueness = 1.08;
        constexpr int inf = std::numeric_limits<int>::max() / 4;

        for (int gy = 0; gy < touchplus::depth::kDepthHeight; ++gy) {
            for (int gx = 0; gx < touchplus::depth::kDepthWidth; ++gx) {
                const size_t idx = static_cast<size_t>(gy) * touchplus::depth::kDepthWidth + gx;
                if (!selected_mask_[idx]) continue;
                const int d_small = workspace.best_disp[idx];
                const bool dense_valid = d_small > 0 &&
                    workspace.best_cost[idx] <= max_average_cost * area &&
                    (workspace.second_cost[idx] == inf ||
                     static_cast<double>(workspace.second_cost[idx]) >=
                        static_cast<double>(workspace.best_cost[idx]) * uniqueness);
                if (!dense_valid) continue;

                const double disparity = static_cast<double>(d_small * touchplus::depth::kDepthScale);
                const double u = gx * touchplus::depth::kDepthScale + 0.5;
                const double v = gy * touchplus::depth::kDepthScale + 0.5;
                const auto camera = touchplus::surface::camera_point_from_q(calibration, u, v, disparity);
                if (!std::isfinite(camera.x) || !std::isfinite(camera.y) || !std::isfinite(camera.z)) continue;
                const auto sp = touchplus::surface::to_surface(surface, camera);
                if (!std::isfinite(sp.h_mm) || sp.h_mm < kV6MinSupportHmm || sp.h_mm > kV6MaxSupportHmm) continue;
                if (std::abs(sp.x_mm) > roi_half_x || std::abs(sp.y_mm) > roi_half_y) continue;
                support[idx] = 1;
                support_disp_small[idx] = d_small;
            }
        }

        const auto bounded = constrain_to_physical_support_v6(
            selected_mask_, support,
            touchplus::depth::kDepthWidth,
            touchplus::depth::kDepthHeight);
        if (!bounded.valid) {
            selected_mask_.clear();
            ++missing_frames_;
            last_result_ = out;
            return out;
        }
        selected_mask_ = bounded.mask;
        out.hand_samples = bounded.cells;
        out.hand_valid = true;

        last_identity_ = palm_core_fingertip_v7(
            selected_mask_,
            touchplus::depth::kDepthWidth,
            touchplus::depth::kDepthHeight);
        if (!last_identity_.valid || last_identity_.ambiguous) {
            out.confidence = "LOW";
            ++missing_frames_;
            last_result_ = out;
            return out;
        }

        const int px = last_identity_.gx * touchplus::depth::kDepthScale + 1;
        const int py = last_identity_.gy * touchplus::depth::kDepthScale + 1;
        out.pixel_x = px;
        out.pixel_y = py;

        int nearest_d_small = 0;
        int nearest_dist2 = std::numeric_limits<int>::max();
        for (int gy = 0; gy < touchplus::depth::kDepthHeight; ++gy) {
            for (int gx = 0; gx < touchplus::depth::kDepthWidth; ++gx) {
                const size_t idx = static_cast<size_t>(gy) * touchplus::depth::kDepthWidth + gx;
                if (!support[idx] || !selected_mask_[idx]) continue;
                const int sx = gx - last_identity_.gx;
                const int sy = gy - last_identity_.gy;
                const int d2 = sx * sx + sy * sy;
                if (d2 < nearest_dist2) {
                    nearest_dist2 = d2;
                    nearest_d_small = support_disp_small[idx];
                }
            }
        }

        if (nearest_d_small <= 0 || nearest_dist2 > 44 * 44) {
            out.confidence = "LOW";
            ++missing_frames_;
            last_result_ = out;
            return out;
        }

        const double coarse_disp = static_cast<double>(nearest_d_small * touchplus::depth::kDepthScale);
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
                if (!mask_near_v5(selected_mask_, touchplus::depth::kDepthWidth,
                                  touchplus::depth::kDepthHeight, sgx, sgy, 1)) continue;

                const auto match = touchplus::depth::robust_point_detail::mutually_consistent_match(
                    left_gray, right_gray, sx, sy, min_d, max_d);
                if (!match.valid) continue;

                const auto camera = touchplus::surface::camera_point_from_q(
                    calibration, static_cast<double>(sx), static_cast<double>(sy), match.disparity);
                if (!std::isfinite(camera.x) || !std::isfinite(camera.y) || !std::isfinite(camera.z)) continue;
                const auto sp = touchplus::surface::to_surface(surface, camera);
                if (!std::isfinite(sp.h_mm) || sp.h_mm < 2.0 ||
                    sp.h_mm > kV6MaxSupportHmm + 20.0 ||
                    std::abs(sp.x_mm) > roi_half_x || std::abs(sp.y_mm) > roi_half_y) continue;
                refined.push_back(sp);
            }
        }

        if (!refined.empty()) {
            std::vector<double> hs;
            hs.reserve(refined.size());
            for (const auto& p : refined) hs.push_back(p.h_mm);
            const double median_h = touchplus::surface::median(std::move(hs));
            std::vector<touchplus::surface::SurfacePoint> consistent;
            consistent.reserve(refined.size());
            for (const auto& p : refined) {
                if (std::abs(p.h_mm - median_h) <= 22.0) consistent.push_back(p);
            }
            refined = std::move(consistent);
        }

        out.refinement_support = static_cast<int>(refined.size());
        if (refined.size() < 3) {
            out.confidence = "LOW";
            ++missing_frames_;
            last_result_ = out;
            return out;
        }

        out.raw_tip = median_surface_point(refined);
        out.confidence = refined.size() >= 6 ? "HIGH" : "MEDIUM";

        if (have_smoothed_) {
            const double jump = std::sqrt(
                sqr(out.raw_tip.x_mm - smoothed_.x_mm) +
                sqr(out.raw_tip.y_mm - smoothed_.y_mm) +
                sqr(out.raw_tip.h_mm - smoothed_.h_mm));
            if (jump > 85.0 && missing_frames_ < 3) {
                out.confidence = "LOW";
                ++missing_frames_;
                last_result_ = out;
                return out;
            }
            constexpr double alpha = 0.32;
            smoothed_.x_mm = smoothed_.x_mm * (1.0 - alpha) + out.raw_tip.x_mm * alpha;
            smoothed_.y_mm = smoothed_.y_mm * (1.0 - alpha) + out.raw_tip.y_mm * alpha;
            smoothed_.h_mm = smoothed_.h_mm * (1.0 - alpha) + out.raw_tip.h_mm * alpha;
        } else {
            smoothed_ = out.raw_tip;
            have_smoothed_ = true;
        }

        missing_frames_ = 0;
        out.smoothed_tip = smoothed_;
        out.fingertip_valid = out.confidence == "HIGH" || out.confidence == "MEDIUM";
        last_result_ = out;
        return out;
    }

    void clear() {
        base_.clear();
        clear_tracking_only();
    }

    const TrackingResult& last_result() const { return last_result_; }
    const std::vector<uint8_t>& selected_mask() const { return selected_mask_; }
    const PalmBranchTipV7& last_identity() const { return last_identity_; }

private:
    void clear_tracking_only() {
        selected_mask_.clear();
        last_result_ = {};
        last_identity_ = {};
        have_smoothed_ = false;
        missing_frames_ = 0;
        smoothed_ = {};
    }

    FingertipTrackerV5 base_;
    TrackingResult last_result_{};
    PalmBranchTipV7 last_identity_{};
    std::vector<uint8_t> selected_mask_;
    bool have_smoothed_ = false;
    int missing_frames_ = 0;
    touchplus::surface::SurfacePoint smoothed_{};
};

} // namespace touchplus::tracking
