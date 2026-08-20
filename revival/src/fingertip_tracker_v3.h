#pragma once

#include "fingertip_tracker_v2.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace touchplus::tracking {

// Phase 2B.3 physical-smoke correction.
//
// The first V2 hand smoke proved that the hardened foreground segmentation was
// useful, but its radial-from-centroid extremity score repeatedly selected the
// wrist / back-of-hand side instead of the extended index. For the canonical
// Touch+ desk setup the forearm enters the camera image from the top. Treat that
// as a deliberate physical prior: anchor the top entry band, walk through the
// selected hand component, and choose the distal geodesic endpoint opposite the
// wrist. This is a controlled single-hand / extended-index slice, not a claim
// of orientation-independent hand-pose understanding.

struct GeodesicTipV3 {
    bool valid = false;
    int gx = -1;
    int gy = -1;
    int geodesic_steps = 0;
    GridSample sample{};
};

inline GeodesicTipV3 geodesic_tip_from_top_wrist_v3(
    const std::vector<GridSample>& samples,
    const std::vector<uint8_t>& selected_mask,
    int width,
    int height) {

    GeodesicTipV3 out;
    if (width <= 0 || height <= 0 || samples.empty() ||
        selected_mask.size() != static_cast<size_t>(width) * height) {
        return out;
    }

    const size_t cell_count = static_cast<size_t>(width) * height;
    std::vector<int> sample_index(cell_count, -1);
    int min_y = height;
    int max_y = -1;
    double min_h = std::numeric_limits<double>::infinity();
    double max_h = -std::numeric_limits<double>::infinity();

    for (size_t i = 0; i < samples.size(); ++i) {
        const auto& s = samples[i];
        if (s.gx < 0 || s.gx >= width || s.gy < 0 || s.gy >= height) continue;
        const size_t idx = static_cast<size_t>(s.gy) * width + s.gx;
        if (!selected_mask[idx]) continue;
        sample_index[idx] = static_cast<int>(i);
        min_y = std::min(min_y, s.gy);
        max_y = std::max(max_y, s.gy);
        min_h = std::min(min_h, s.surface.h_mm);
        max_h = std::max(max_h, s.surface.h_mm);
    }

    if (max_y < min_y || !std::isfinite(min_h) || !std::isfinite(max_h)) return out;

    // Grow ONLY the already selected component by one cell for graph walking.
    // This reconnects sparse dense-depth holes without allowing other scene
    // components to join the hand after segmentation.
    std::vector<uint8_t> walkable = selected_mask;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t idx = static_cast<size_t>(y) * width + x;
            if (!selected_mask[idx]) continue;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                        walkable[static_cast<size_t>(ny) * width + nx] = 1;
                    }
                }
            }
        }
    }

    // Canonical desk prior: the forearm/wrist enters from the top of the image.
    // Seed a small top band rather than one noisy topmost pixel.
    const int y_span = std::max(1, max_y - min_y);
    const int anchor_band = std::max(5, static_cast<int>(std::lround(y_span * 0.14)));
    const int anchor_max_y = std::min(max_y, min_y + anchor_band);

    std::vector<int> distance(cell_count, -1);
    std::vector<int> queue;
    queue.reserve(cell_count);
    for (int y = min_y; y <= anchor_max_y; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t idx = static_cast<size_t>(y) * width + x;
            if (!selected_mask[idx]) continue;
            distance[idx] = 0;
            queue.push_back(static_cast<int>(idx));
        }
    }
    if (queue.empty()) return out;

    constexpr std::array<std::array<int, 2>, 8> neighbors{{
        {{-1,-1}}, {{0,-1}}, {{1,-1}}, {{-1,0}},
        {{1,0}}, {{-1,1}}, {{0,1}}, {{1,1}}
    }};

    size_t head = 0;
    while (head < queue.size()) {
        const int flat = queue[head++];
        const int y = flat / width;
        const int x = flat - y * width;
        for (const auto& n : neighbors) {
            const int nx = x + n[0];
            const int ny = y + n[1];
            if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
            const size_t ni = static_cast<size_t>(ny) * width + nx;
            if (!walkable[ni] || distance[ni] >= 0) continue;
            distance[ni] = distance[static_cast<size_t>(flat)] + 1;
            queue.push_back(static_cast<int>(ni));
        }
    }

    int max_distance = 0;
    for (size_t idx = 0; idx < cell_count; ++idx) {
        if (sample_index[idx] >= 0 && distance[idx] > max_distance) {
            max_distance = distance[idx];
        }
    }
    if (max_distance < 8) return out;

    const double h_span = std::max(5.0, max_h - min_h);
    double best_score = -1.0;
    int best_si = -1;
    int best_distance = 0;

    for (size_t idx = 0; idx < cell_count; ++idx) {
        const int si = sample_index[idx];
        if (si < 0 || distance[idx] < 0) continue;
        if (distance[idx] < static_cast<int>(std::floor(max_distance * 0.62))) continue;

        const auto& s = samples[static_cast<size_t>(si)];
        int neighbor_count = 0;
        for (const auto& n : neighbors) {
            const int nx = s.gx + n[0];
            const int ny = s.gy + n[1];
            if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
            if (selected_mask[static_cast<size_t>(ny) * width + nx]) ++neighbor_count;
        }

        const double geodesic_score = std::clamp(
            static_cast<double>(distance[idx]) / static_cast<double>(max_distance), 0.0, 1.0);
        const double low_height_score = std::clamp(
            1.0 - (s.surface.h_mm - min_h) / h_span, 0.0, 1.0);
        const double boundary_score = 1.0 - static_cast<double>(neighbor_count) / 8.0;

        // Geodesic distance must dominate. Height and local thinness only break
        // ties among distal endpoints; they cannot pull the candidate back to
        // the wrist as the old centroid-radius scorer did.
        const double score = 0.78 * geodesic_score +
                             0.12 * low_height_score +
                             0.10 * boundary_score;
        if (score > best_score) {
            best_score = score;
            best_si = si;
            best_distance = distance[idx];
        }
    }

    if (best_si < 0) return out;
    out.valid = true;
    out.sample = samples[static_cast<size_t>(best_si)];
    out.gx = out.sample.gx;
    out.gy = out.sample.gy;
    out.geodesic_steps = best_distance;
    return out;
}

class FingertipTrackerV3 {
public:
    TrackingResult update(
        const touchplus::depth::Calibration& calibration,
        const touchplus::surface::SurfaceModel& surface,
        const std::vector<uint8_t>& left_gray,
        const std::vector<uint8_t>& right_gray,
        const touchplus::depth::DepthWorkspace& workspace) {

        TrackingResult out;
        if (!surface.valid ||
            left_gray.size() < static_cast<size_t>(touchplus::depth::kEyeWidth) * touchplus::depth::kEyeHeight ||
            right_gray.size() < static_cast<size_t>(touchplus::depth::kEyeWidth) * touchplus::depth::kEyeHeight) {
            last_result_ = out;
            return out;
        }

        const double roi_half_x = surface.spread_x_mm >= 80.0
            ? surface.spread_x_mm * 0.5 + 70.0 : 280.0;
        const double roi_half_y = surface.spread_y_mm >= 80.0
            ? surface.spread_y_mm * 0.5 + 70.0 : 260.0;

        std::vector<GridSample> candidates;
        candidates.reserve(static_cast<size_t>(touchplus::depth::kDepthWidth) * touchplus::depth::kDepthHeight / 8);
        constexpr int radius = 2;
        constexpr int area = (radius * 2 + 1) * (radius * 2 + 1);
        constexpr int max_average_cost = 42;
        constexpr double uniqueness = 1.10;
        constexpr int inf = std::numeric_limits<int>::max() / 4;

        for (int gy = 0; gy < touchplus::depth::kDepthHeight; ++gy) {
            for (int gx = 0; gx < touchplus::depth::kDepthWidth; ++gx) {
                const size_t idx = static_cast<size_t>(gy) * touchplus::depth::kDepthWidth + gx;
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
                if (!std::isfinite(sp.h_mm) || sp.h_mm < kV2MinForegroundHmm || sp.h_mm > kV2MaxForegroundHmm) continue;
                if (std::abs(sp.x_mm) > roi_half_x || std::abs(sp.y_mm) > roi_half_y) continue;
                candidates.push_back({gx, gy, sp, disparity});
            }
        }

        std::vector<GridSample> samples = locally_consistent_samples_v2(
            candidates, touchplus::depth::kDepthWidth, touchplus::depth::kDepthHeight);

        ComponentAnalysis segmentation = analyze_surface_samples_v2(
            samples, touchplus::depth::kDepthWidth, touchplus::depth::kDepthHeight);
        selected_mask_ = std::move(segmentation.selected_mask);
        out.foreground_samples = segmentation.foreground_samples;
        out.hand_samples = segmentation.component_samples;
        if (!segmentation.valid) {
            ++missing_frames_;
            last_result_ = out;
            return out;
        }
        out.hand_valid = true;

        const auto geodesic = geodesic_tip_from_top_wrist_v3(
            samples, selected_mask_, touchplus::depth::kDepthWidth, touchplus::depth::kDepthHeight);
        if (!geodesic.valid) {
            out.confidence = "LOW";
            ++missing_frames_;
            last_result_ = out;
            return out;
        }

        const int px = geodesic.gx * touchplus::depth::kDepthScale + 1;
        const int py = geodesic.gy * touchplus::depth::kDepthScale + 1;
        out.pixel_x = px;
        out.pixel_y = py;
        out.raw_tip = geodesic.sample.surface;

        const double coarse_disp = geodesic.sample.disparity_px;
        std::vector<touchplus::surface::SurfacePoint> refined;
        constexpr std::array<int, 5> offsets{{-8, -4, 0, 4, 8}};
        const int min_d = std::max(
            touchplus::depth::robust_point_detail::kMinDisparity,
            static_cast<int>(std::floor(coarse_disp - 14.0)));
        const int max_d = std::min(
            touchplus::depth::robust_point_detail::kMaxDisparity,
            static_cast<int>(std::ceil(coarse_disp + 14.0)));

        for (const int oy : offsets) {
            for (const int ox : offsets) {
                const int sx = px + ox;
                const int sy = py + oy;
                if (sx < 12 || sx >= touchplus::depth::kEyeWidth - 5 ||
                    sy < 5 || sy >= touchplus::depth::kEyeHeight - 5) continue;
                const auto match = touchplus::depth::robust_point_detail::mutually_consistent_match(
                    left_gray, right_gray, sx, sy, min_d, max_d);
                if (!match.valid) continue;
                const auto camera = touchplus::surface::camera_point_from_q(
                    calibration, static_cast<double>(sx), static_cast<double>(sy), match.disparity);
                if (!std::isfinite(camera.x) || !std::isfinite(camera.y) || !std::isfinite(camera.z)) continue;
                const auto sp = touchplus::surface::to_surface(surface, camera);
                if (!std::isfinite(sp.h_mm) || sp.h_mm < 2.0 || sp.h_mm > kV2MaxForegroundHmm + 40.0) continue;
                if (std::abs(sp.x_mm) > roi_half_x || std::abs(sp.y_mm) > roi_half_y) continue;
                const double planar_delta = std::sqrt(
                    sqr(sp.x_mm - geodesic.sample.surface.x_mm) +
                    sqr(sp.y_mm - geodesic.sample.surface.y_mm));
                if (planar_delta <= 38.0) refined.push_back(sp);
            }
        }

        out.refinement_support = static_cast<int>(refined.size());
        if (refined.size() < 3) {
            out.confidence = "LOW";
            ++missing_frames_;
            last_result_ = out;
            return out;
        }

        out.raw_tip = median_surface_point(refined);
        const bool strong_component = out.hand_samples >= 120 && out.hand_samples <= kV2MaxHandSamples;
        out.confidence = (strong_component && refined.size() >= 6) ? "HIGH" : "MEDIUM";

        if (have_smoothed_) {
            const double jump = std::sqrt(
                sqr(out.raw_tip.x_mm - smoothed_.x_mm) +
                sqr(out.raw_tip.y_mm - smoothed_.y_mm) +
                sqr(out.raw_tip.h_mm - smoothed_.h_mm));
            if (jump > kMaxTemporalJumpMm && missing_frames_ < 3) {
                out.confidence = "LOW";
                ++missing_frames_;
                last_result_ = out;
                return out;
            }
            constexpr double alpha = 0.35;
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
        selected_mask_.clear();
        last_result_ = {};
        have_smoothed_ = false;
        missing_frames_ = 0;
        smoothed_ = {};
    }

    const TrackingResult& last_result() const { return last_result_; }
    const std::vector<uint8_t>& selected_mask() const { return selected_mask_; }

private:
    TrackingResult last_result_{};
    std::vector<uint8_t> selected_mask_;
    bool have_smoothed_ = false;
    int missing_frames_ = 0;
    touchplus::surface::SurfacePoint smoothed_{};
};

} // namespace touchplus::tracking
