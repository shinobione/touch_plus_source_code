#pragma once

#include "fingertip_tracker_v3.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace touchplus::tracking {

// Phase 2B.4 physical-smoke correction.
//
// 2B.3 proved that a geodesic endpoint alone is not enough: the real desk scene
// still produced 600-1500-cell pseudo-hands with no hand present, and the coarse
// endpoint could jump across unrelated depth tails. V4 therefore learns a clean
// stereo/depth background first. A cell may become hand foreground only when it
// is measurably closer than that learned background (or, for cells where the
// background had no reliable disparity, when both appearance and H changed).
//
// This remains an intentionally controlled desk boundary: one hand enters from
// the top of the image and an extended index points downward into the work area.

constexpr int kV4BackgroundFrames = 30;
constexpr int kV4MinBackgroundSamples = 6;
constexpr double kV4MinBackgroundDeltaSmall = 2.0;
constexpr double kV4FallbackMinHmm = 32.0;
constexpr double kV4FallbackAppearanceDelta = 14.0;
constexpr int kV4EntryMaxGy = 72;          // full-res y <= 144 px
constexpr int kV4MinEntryCells = 8;
constexpr double kV4MinDownwardFraction = 0.36;

inline double appearance_delta_v4(
    const std::vector<uint8_t>& current,
    const std::vector<uint8_t>& background,
    int x,
    int y) {

    if (current.size() < static_cast<size_t>(touchplus::depth::kEyeWidth) * touchplus::depth::kEyeHeight ||
        background.size() != current.size()) {
        return 0.0;
    }

    constexpr std::array<std::array<int, 2>, 5> offsets{{
        {{0,0}}, {{-2,0}}, {{2,0}}, {{0,-2}}, {{0,2}}
    }};
    double sum = 0.0;
    int count = 0;
    for (const auto& off : offsets) {
        const int sx = std::clamp(x + off[0], 0, touchplus::depth::kEyeWidth - 1);
        const int sy = std::clamp(y + off[1], 0, touchplus::depth::kEyeHeight - 1);
        const size_t idx = static_cast<size_t>(sy) * touchplus::depth::kEyeWidth + sx;
        sum += std::abs(static_cast<int>(current[idx]) - static_cast<int>(background[idx]));
        ++count;
    }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

inline bool foreground_against_background_v4(
    int current_disp_small,
    double background_disp_small,
    int background_samples,
    double h_mm,
    double appearance_delta) {

    if (background_samples >= kV4MinBackgroundSamples && std::isfinite(background_disp_small)) {
        return static_cast<double>(current_disp_small) >=
            background_disp_small + kV4MinBackgroundDeltaSmall;
    }

    // Background disparity can legitimately be absent on a textureless table.
    // In that case require BOTH a stronger physical height and a visible image
    // change, rather than trusting one noisy dense-depth cell by itself.
    return h_mm >= kV4FallbackMinHmm &&
           appearance_delta >= kV4FallbackAppearanceDelta;
}

inline bool top_entry_component_v4(
    const std::vector<uint8_t>& selected_mask,
    int width,
    int height) {

    if (selected_mask.size() != static_cast<size_t>(width) * height) return false;
    int entry_cells = 0;
    const int limit = std::min(height - 1, kV4EntryMaxGy);
    for (int y = 0; y <= limit; ++y) {
        for (int x = 0; x < width; ++x) {
            if (selected_mask[static_cast<size_t>(y) * width + x]) {
                ++entry_cells;
                if (entry_cells >= kV4MinEntryCells) return true;
            }
        }
    }
    return false;
}

struct DistalTipV4 {
    bool valid = false;
    int gx = -1;
    int gy = -1;
    int geodesic_steps = 0;
    GridSample sample{};
};

inline DistalTipV4 distal_tip_from_top_v4(
    const std::vector<GridSample>& samples,
    const std::vector<uint8_t>& selected_mask,
    int width,
    int height) {

    DistalTipV4 out;
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

    // One-cell bridge ONLY inside the already selected hand component, to let
    // graph distance cross small stereo holes without joining unrelated scene.
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

    const int y_span = std::max(1, max_y - min_y);
    const int anchor_band = std::max(5, static_cast<int>(std::lround(y_span * 0.12)));
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
    const int min_candidate_y = min_y +
        static_cast<int>(std::floor(y_span * kV4MinDownwardFraction));

    double best_score = -1.0;
    int best_si = -1;
    int best_distance = 0;
    for (size_t idx = 0; idx < cell_count; ++idx) {
        const int si = sample_index[idx];
        if (si < 0 || distance[idx] < 0) continue;
        const auto& s = samples[static_cast<size_t>(si)];
        if (s.gy < min_candidate_y) continue;
        if (distance[idx] < static_cast<int>(std::floor(max_distance * 0.52))) continue;

        int neighbor_count = 0;
        for (const auto& n : neighbors) {
            const int nx = s.gx + n[0];
            const int ny = s.gy + n[1];
            if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
            if (selected_mask[static_cast<size_t>(ny) * width + nx]) ++neighbor_count;
        }

        const double downward_score = std::clamp(
            static_cast<double>(s.gy - min_y) / static_cast<double>(y_span), 0.0, 1.0);
        const double geodesic_score = std::clamp(
            static_cast<double>(distance[idx]) / static_cast<double>(max_distance), 0.0, 1.0);
        const double low_height_score = std::clamp(
            1.0 - (s.surface.h_mm - min_h) / h_span, 0.0, 1.0);
        const double boundary_score = 1.0 - static_cast<double>(neighbor_count) / 8.0;

        // For this explicit desk gesture, downward location is anatomical
        // evidence: the wrist enters at the top and the extended index points
        // into the lower work area. Geodesic distance remains strong evidence,
        // while H and local thinness only help tie-breaking.
        const double score = 0.46 * downward_score +
                             0.36 * geodesic_score +
                             0.10 * low_height_score +
                             0.08 * boundary_score;
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

class FingertipTrackerV4 {
public:
    void request_background_capture() {
        const size_t depth_cells = static_cast<size_t>(touchplus::depth::kDepthWidth) * touchplus::depth::kDepthHeight;
        const size_t eye_pixels = static_cast<size_t>(touchplus::depth::kEyeWidth) * touchplus::depth::kEyeHeight;
        background_learning_ = true;
        background_ready_ = false;
        background_frames_ = 0;
        background_disp_sum_.assign(depth_cells, 0u);
        background_disp_count_.assign(depth_cells, 0u);
        background_left_sum_.assign(eye_pixels, 0u);
        background_left_.clear();
        clear_tracking_only();
    }

    bool background_ready() const { return background_ready_; }
    bool background_learning() const { return background_learning_; }
    int background_frames() const { return background_frames_; }

    TrackingResult update(
        const touchplus::depth::Calibration& calibration,
        const touchplus::surface::SurfaceModel& surface,
        const std::vector<uint8_t>& left_gray,
        const std::vector<uint8_t>& right_gray,
        const touchplus::depth::DepthWorkspace& workspace) {

        TrackingResult out;
        const size_t eye_pixels = static_cast<size_t>(touchplus::depth::kEyeWidth) * touchplus::depth::kEyeHeight;
        if (!surface.valid || left_gray.size() < eye_pixels || right_gray.size() < eye_pixels) {
            last_result_ = out;
            return out;
        }

        if (background_learning_) {
            accumulate_background(left_gray, workspace);
            last_result_ = out;
            selected_mask_.clear();
            return out;
        }
        if (!background_ready_) {
            last_result_ = out;
            selected_mask_.clear();
            return out;
        }

        const double roi_half_x = surface.spread_x_mm >= 80.0
            ? surface.spread_x_mm * 0.5 + 70.0 : 280.0;
        const double roi_half_y = surface.spread_y_mm >= 80.0
            ? surface.spread_y_mm * 0.5 + 70.0 : 260.0;

        std::vector<GridSample> candidates;
        candidates.reserve(static_cast<size_t>(touchplus::depth::kDepthWidth) * touchplus::depth::kDepthHeight / 10);
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

                const int px = std::clamp(gx * touchplus::depth::kDepthScale + 1, 0, touchplus::depth::kEyeWidth - 1);
                const int py = std::clamp(gy * touchplus::depth::kDepthScale + 1, 0, touchplus::depth::kEyeHeight - 1);
                const double appearance_delta = appearance_delta_v4(left_gray, background_left_, px, py);
                const int bg_count = static_cast<int>(background_disp_count_[idx]);
                const double bg_disp = bg_count > 0
                    ? static_cast<double>(background_disp_sum_[idx]) / static_cast<double>(bg_count)
                    : std::numeric_limits<double>::quiet_NaN();
                if (!foreground_against_background_v4(
                        d_small, bg_disp, bg_count, sp.h_mm, appearance_delta)) {
                    continue;
                }

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
        if (!segmentation.valid ||
            !top_entry_component_v4(selected_mask_, touchplus::depth::kDepthWidth, touchplus::depth::kDepthHeight)) {
            ++missing_frames_;
            last_result_ = out;
            return out;
        }
        out.hand_valid = true;

        const auto tip = distal_tip_from_top_v4(
            samples, selected_mask_, touchplus::depth::kDepthWidth, touchplus::depth::kDepthHeight);
        if (!tip.valid) {
            out.confidence = "LOW";
            ++missing_frames_;
            last_result_ = out;
            return out;
        }

        const int px = tip.gx * touchplus::depth::kDepthScale + 1;
        const int py = tip.gy * touchplus::depth::kDepthScale + 1;
        out.pixel_x = px;
        out.pixel_y = py;
        out.raw_tip = tip.sample.surface;

        const double coarse_disp = tip.sample.disparity_px;
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
                    sqr(sp.x_mm - tip.sample.surface.x_mm) +
                    sqr(sp.y_mm - tip.sample.surface.y_mm));
                if (planar_delta <= 34.0) refined.push_back(sp);
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
            if (jump > 90.0 && missing_frames_ < 3) {
                out.confidence = "LOW";
                ++missing_frames_;
                last_result_ = out;
                return out;
            }
            constexpr double alpha = 0.30;
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
        clear_tracking_only();
        background_learning_ = false;
        background_ready_ = false;
        background_frames_ = 0;
        background_disp_sum_.clear();
        background_disp_count_.clear();
        background_left_sum_.clear();
        background_left_.clear();
    }

    const TrackingResult& last_result() const { return last_result_; }
    const std::vector<uint8_t>& selected_mask() const { return selected_mask_; }

private:
    void clear_tracking_only() {
        selected_mask_.clear();
        last_result_ = {};
        have_smoothed_ = false;
        missing_frames_ = 0;
        smoothed_ = {};
    }

    void accumulate_background(
        const std::vector<uint8_t>& left_gray,
        const touchplus::depth::DepthWorkspace& workspace) {

        if (background_left_sum_.size() != left_gray.size()) return;
        for (size_t i = 0; i < left_gray.size(); ++i) {
            background_left_sum_[i] += left_gray[i];
        }

        const size_t depth_cells = static_cast<size_t>(touchplus::depth::kDepthWidth) * touchplus::depth::kDepthHeight;
        if (background_disp_sum_.size() != depth_cells || background_disp_count_.size() != depth_cells) return;
        for (size_t i = 0; i < depth_cells; ++i) {
            const int d = workspace.best_disp[i];
            if (d <= 0) continue;
            background_disp_sum_[i] += static_cast<uint32_t>(d);
            if (background_disp_count_[i] < std::numeric_limits<uint16_t>::max()) {
                ++background_disp_count_[i];
            }
        }

        ++background_frames_;
        if (background_frames_ < kV4BackgroundFrames) return;

        background_left_.resize(left_gray.size());
        for (size_t i = 0; i < left_gray.size(); ++i) {
            background_left_[i] = static_cast<uint8_t>(
                background_left_sum_[i] / static_cast<uint32_t>(background_frames_));
        }
        background_learning_ = false;
        background_ready_ = true;
        clear_tracking_only();
    }

    TrackingResult last_result_{};
    std::vector<uint8_t> selected_mask_;
    bool have_smoothed_ = false;
    int missing_frames_ = 0;
    touchplus::surface::SurfacePoint smoothed_{};

    bool background_learning_ = false;
    bool background_ready_ = false;
    int background_frames_ = 0;
    std::vector<uint32_t> background_disp_sum_;
    std::vector<uint16_t> background_disp_count_;
    std::vector<uint32_t> background_left_sum_;
    std::vector<uint8_t> background_left_;
};

} // namespace touchplus::tracking
