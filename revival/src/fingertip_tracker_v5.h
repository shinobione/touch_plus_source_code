#pragma once

#include "fingertip_tracker_v4.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace touchplus::tracking {

// Phase 2B.5 physical-smoke correction.
//
// 2B.4 proved the learned background is useful, but the real fingertip often
// vanished before endpoint scoring because the half-resolution dense disparity
// has no valid sample on low-texture skin. V5 therefore decouples IDENTIFICATION
// from METRIC DEPTH:
//
//   learned grayscale background -> 2D changed silhouette -> distal fingertip
//                                             |
//                                             v
//                         robust full-res stereo only near that 2D fingertip
//
// Dense depth remains a physical support/core gate so lighting/shadows alone do
// not become a hand. It is no longer required at every silhouette cell.
//
// This is still the controlled desk boundary: one hand enters from the top and
// one index is extended generally downward into the work area.

constexpr int kV5BackgroundFrames = 30;
constexpr double kV5CoreAppearanceDelta = 10.0;
constexpr double kV5AppearanceOnlyDelta = 24.0;
constexpr size_t kV5MinSilhouetteCells = 140;
constexpr size_t kV5MaxSilhouetteCells = 18000;
constexpr size_t kV5MinDepthCoreCells = 24;
constexpr int kV5EntryMaxGy = 78;          // full-res y <= 156 px
constexpr int kV5MinEntryCells = 10;
constexpr int kV5MinVerticalSpan = 28;
constexpr double kV5MinDownwardFraction = 0.40;

struct SilhouetteTipV5 {
    bool valid = false;
    size_t silhouette_cells = 0;
    size_t depth_core_cells = 0;
    int gx = -1;
    int gy = -1;
    int geodesic_steps = 0;
    std::vector<uint8_t> selected_mask;
};

inline std::vector<uint8_t> bridge_single_cell_gaps_v5(
    const std::vector<uint8_t>& raw,
    int width,
    int height) {

    std::vector<uint8_t> connected = raw;
    if (raw.size() != static_cast<size_t>(width) * height) return connected;

    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            const size_t idx = static_cast<size_t>(y) * width + x;
            if (raw[idx]) continue;

            const bool horizontal =
                raw[static_cast<size_t>(y) * width + (x - 1)] &&
                raw[static_cast<size_t>(y) * width + (x + 1)];
            const bool vertical =
                raw[static_cast<size_t>(y - 1) * width + x] &&
                raw[static_cast<size_t>(y + 1) * width + x];
            const bool diag_a =
                raw[static_cast<size_t>(y - 1) * width + (x - 1)] &&
                raw[static_cast<size_t>(y + 1) * width + (x + 1)];
            const bool diag_b =
                raw[static_cast<size_t>(y - 1) * width + (x + 1)] &&
                raw[static_cast<size_t>(y + 1) * width + (x - 1)];

            int neighbors = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    if (raw[static_cast<size_t>(y + dy) * width + (x + dx)]) {
                        ++neighbors;
                    }
                }
            }

            if (horizontal || vertical || diag_a || diag_b || neighbors >= 5) {
                connected[idx] = 1;
            }
        }
    }
    return connected;
}

inline SilhouetteTipV5 analyze_appearance_silhouette_v5(
    const std::vector<uint8_t>& appearance_mask,
    const std::vector<uint8_t>& depth_core_mask,
    int width,
    int height) {

    SilhouetteTipV5 out;
    const size_t cell_count = static_cast<size_t>(width) * height;
    if (width <= 0 || height <= 0 ||
        appearance_mask.size() != cell_count ||
        depth_core_mask.size() != cell_count) {
        return out;
    }

    const std::vector<uint8_t> connected =
        bridge_single_cell_gaps_v5(appearance_mask, width, height);

    constexpr std::array<std::array<int, 2>, 8> neighbors{{
        {{-1,-1}}, {{0,-1}}, {{1,-1}}, {{-1,0}},
        {{1,0}}, {{-1,1}}, {{0,1}}, {{1,1}}
    }};

    struct Stats {
        int label = -1;
        size_t cells = 0;
        size_t core = 0;
        int entry = 0;
        int min_x = std::numeric_limits<int>::max();
        int max_x = -1;
        int min_y = std::numeric_limits<int>::max();
        int max_y = -1;
    };

    std::vector<int> labels(cell_count, -1);
    std::vector<int> queue;
    queue.reserve(cell_count);
    std::vector<Stats> components;
    int next_label = 0;

    for (int sy = 0; sy < height; ++sy) {
        for (int sx = 0; sx < width; ++sx) {
            const size_t seed = static_cast<size_t>(sy) * width + sx;
            if (!connected[seed] || labels[seed] >= 0) continue;

            Stats s;
            s.label = next_label;
            queue.clear();
            queue.push_back(static_cast<int>(seed));
            labels[seed] = next_label;
            size_t head = 0;

            while (head < queue.size()) {
                const int flat = queue[head++];
                const int y = flat / width;
                const int x = flat - y * width;
                const size_t idx = static_cast<size_t>(flat);
                ++s.cells;
                if (depth_core_mask[idx]) ++s.core;
                if (y <= kV5EntryMaxGy) ++s.entry;
                s.min_x = std::min(s.min_x, x);
                s.max_x = std::max(s.max_x, x);
                s.min_y = std::min(s.min_y, y);
                s.max_y = std::max(s.max_y, y);

                for (const auto& n : neighbors) {
                    const int nx = x + n[0];
                    const int ny = y + n[1];
                    if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
                    const size_t ni = static_cast<size_t>(ny) * width + nx;
                    if (connected[ni] && labels[ni] < 0) {
                        labels[ni] = next_label;
                        queue.push_back(static_cast<int>(ni));
                    }
                }
            }

            components.push_back(s);
            ++next_label;
        }
    }

    int best_label = -1;
    double best_component_score = -1.0;
    Stats best_stats;
    for (const auto& s : components) {
        if (s.cells < kV5MinSilhouetteCells || s.cells > kV5MaxSilhouetteCells) continue;
        if (s.core < kV5MinDepthCoreCells) continue;
        if (s.entry < kV5MinEntryCells) continue;
        if (s.max_y - s.min_y < kV5MinVerticalSpan) continue;

        const double score =
            static_cast<double>(s.cells) + 3.0 * static_cast<double>(s.core);
        if (score > best_component_score) {
            best_component_score = score;
            best_label = s.label;
            best_stats = s;
        }
    }
    if (best_label < 0) return out;

    out.selected_mask.assign(cell_count, 0);
    for (size_t idx = 0; idx < cell_count; ++idx) {
        if (labels[idx] == best_label && connected[idx]) {
            out.selected_mask[idx] = 1;
        }
    }
    out.silhouette_cells = best_stats.cells;
    out.depth_core_cells = best_stats.core;

    const int y_span = std::max(1, best_stats.max_y - best_stats.min_y);
    const int anchor_band = std::max(
        5, static_cast<int>(std::lround(static_cast<double>(y_span) * 0.12)));
    const int anchor_max_y = std::min(best_stats.max_y, best_stats.min_y + anchor_band);

    std::vector<int> distance(cell_count, -1);
    queue.clear();
    for (int y = best_stats.min_y; y <= anchor_max_y; ++y) {
        for (int x = best_stats.min_x; x <= best_stats.max_x; ++x) {
            const size_t idx = static_cast<size_t>(y) * width + x;
            if (!out.selected_mask[idx]) continue;
            distance[idx] = 0;
            queue.push_back(static_cast<int>(idx));
        }
    }
    if (queue.empty()) {
        out.selected_mask.clear();
        return out;
    }

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
            if (!out.selected_mask[ni] || distance[ni] >= 0) continue;
            distance[ni] = distance[static_cast<size_t>(flat)] + 1;
            queue.push_back(static_cast<int>(ni));
        }
    }

    int max_distance = 0;
    for (size_t idx = 0; idx < cell_count; ++idx) {
        if (out.selected_mask[idx] && distance[idx] > max_distance) {
            max_distance = distance[idx];
        }
    }
    if (max_distance < 10) {
        out.selected_mask.clear();
        return out;
    }

    const int min_candidate_y = best_stats.min_y +
        static_cast<int>(std::floor(static_cast<double>(y_span) * kV5MinDownwardFraction));

    double best_tip_score = -1.0;
    int best_x = -1;
    int best_y = -1;
    int best_distance = 0;

    for (int y = best_stats.min_y; y <= best_stats.max_y; ++y) {
        for (int x = best_stats.min_x; x <= best_stats.max_x; ++x) {
            const size_t idx = static_cast<size_t>(y) * width + x;
            if (!out.selected_mask[idx] || distance[idx] < 0) continue;
            if (y < min_candidate_y) continue;
            if (distance[idx] < static_cast<int>(std::floor(max_distance * 0.50))) continue;

            int neighbor_count = 0;
            for (const auto& n : neighbors) {
                const int nx = x + n[0];
                const int ny = y + n[1];
                if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
                if (out.selected_mask[static_cast<size_t>(ny) * width + nx]) {
                    ++neighbor_count;
                }
            }

            const double downward = std::clamp(
                static_cast<double>(y - best_stats.min_y) /
                    static_cast<double>(y_span), 0.0, 1.0);
            const double geodesic = std::clamp(
                static_cast<double>(distance[idx]) /
                    static_cast<double>(max_distance), 0.0, 1.0);
            const double boundary =
                1.0 - static_cast<double>(neighbor_count) / 8.0;

            const double score =
                0.55 * downward + 0.35 * geodesic + 0.10 * boundary;
            if (score > best_tip_score) {
                best_tip_score = score;
                best_x = x;
                best_y = y;
                best_distance = distance[idx];
            }
        }
    }

    if (best_x < 0 || best_y < 0) {
        out.selected_mask.clear();
        return out;
    }

    out.valid = true;
    out.gx = best_x;
    out.gy = best_y;
    out.geodesic_steps = best_distance;
    return out;
}

inline bool mask_near_v5(
    const std::vector<uint8_t>& mask,
    int width,
    int height,
    int gx,
    int gy,
    int radius = 1) {

    if (mask.size() != static_cast<size_t>(width) * height) return false;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const int x = gx + dx;
            const int y = gy + dy;
            if (x < 0 || x >= width || y < 0 || y >= height) continue;
            if (mask[static_cast<size_t>(y) * width + x]) return true;
        }
    }
    return false;
}

class FingertipTrackerV5 {
public:
    void request_background_capture() {
        const size_t depth_cells =
            static_cast<size_t>(touchplus::depth::kDepthWidth) *
            touchplus::depth::kDepthHeight;
        const size_t eye_pixels =
            static_cast<size_t>(touchplus::depth::kEyeWidth) *
            touchplus::depth::kEyeHeight;
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
        const size_t eye_pixels =
            static_cast<size_t>(touchplus::depth::kEyeWidth) *
            touchplus::depth::kEyeHeight;
        const size_t depth_cells =
            static_cast<size_t>(touchplus::depth::kDepthWidth) *
            touchplus::depth::kDepthHeight;
        if (!surface.valid || left_gray.size() < eye_pixels ||
            right_gray.size() < eye_pixels) {
            last_result_ = out;
            return out;
        }

        if (background_learning_) {
            accumulate_background(left_gray, workspace);
            last_result_ = out;
            selected_mask_.clear();
            return out;
        }
        if (!background_ready_ || background_left_.size() != eye_pixels) {
            last_result_ = out;
            selected_mask_.clear();
            return out;
        }

        const double roi_half_x = surface.spread_x_mm >= 80.0
            ? surface.spread_x_mm * 0.5 + 70.0 : 280.0;
        const double roi_half_y = surface.spread_y_mm >= 80.0
            ? surface.spread_y_mm * 0.5 + 70.0 : 260.0;

        std::vector<uint8_t> appearance_mask(depth_cells, 0);
        std::vector<uint8_t> depth_core_mask(depth_cells, 0);
        std::vector<int> core_disp_small(depth_cells, 0);

        constexpr int radius = 2;
        constexpr int area = (radius * 2 + 1) * (radius * 2 + 1);
        constexpr int max_average_cost = 42;
        constexpr double uniqueness = 1.10;
        constexpr int inf = std::numeric_limits<int>::max() / 4;

        size_t changed_cells = 0;
        for (int gy = 0; gy < touchplus::depth::kDepthHeight; ++gy) {
            for (int gx = 0; gx < touchplus::depth::kDepthWidth; ++gx) {
                const size_t idx =
                    static_cast<size_t>(gy) * touchplus::depth::kDepthWidth + gx;
                const int px = std::clamp(
                    gx * touchplus::depth::kDepthScale + 1,
                    0, touchplus::depth::kEyeWidth - 1);
                const int py = std::clamp(
                    gy * touchplus::depth::kDepthScale + 1,
                    0, touchplus::depth::kEyeHeight - 1);

                const double appearance_delta =
                    appearance_delta_v4(left_gray, background_left_, px, py);

                const int d_small = workspace.best_disp[idx];
                const bool dense_valid = d_small > 0 &&
                    workspace.best_cost[idx] <= max_average_cost * area &&
                    (workspace.second_cost[idx] == inf ||
                     static_cast<double>(workspace.second_cost[idx]) >=
                        static_cast<double>(workspace.best_cost[idx]) * uniqueness);

                bool core = false;
                if (dense_valid) {
                    const double disparity =
                        static_cast<double>(d_small * touchplus::depth::kDepthScale);
                    const double u =
                        gx * touchplus::depth::kDepthScale + 0.5;
                    const double v =
                        gy * touchplus::depth::kDepthScale + 0.5;
                    const auto camera =
                        touchplus::surface::camera_point_from_q(
                            calibration, u, v, disparity);
                    if (std::isfinite(camera.x) &&
                        std::isfinite(camera.y) &&
                        std::isfinite(camera.z)) {
                        const auto sp =
                            touchplus::surface::to_surface(surface, camera);
                        if (std::isfinite(sp.h_mm) &&
                            sp.h_mm >= kV2MinForegroundHmm &&
                            sp.h_mm <= kV2MaxForegroundHmm &&
                            std::abs(sp.x_mm) <= roi_half_x &&
                            std::abs(sp.y_mm) <= roi_half_y) {

                            const int bg_count =
                                static_cast<int>(background_disp_count_[idx]);
                            const double bg_disp = bg_count > 0
                                ? static_cast<double>(background_disp_sum_[idx]) /
                                    static_cast<double>(bg_count)
                                : std::numeric_limits<double>::quiet_NaN();
                            core = foreground_against_background_v4(
                                d_small, bg_disp, bg_count,
                                sp.h_mm, appearance_delta);
                        }
                    }
                }

                if (core) {
                    depth_core_mask[idx] = 1;
                    core_disp_small[idx] = d_small;
                }

                const bool appearance_changed =
                    appearance_delta >=
                        (core ? kV5CoreAppearanceDelta :
                                kV5AppearanceOnlyDelta);
                if (appearance_changed) {
                    appearance_mask[idx] = 1;
                    ++changed_cells;
                }
            }
        }

        const auto silhouette = analyze_appearance_silhouette_v5(
            appearance_mask,
            depth_core_mask,
            touchplus::depth::kDepthWidth,
            touchplus::depth::kDepthHeight);

        selected_mask_ = silhouette.selected_mask;
        out.foreground_samples = changed_cells;
        out.hand_samples = silhouette.silhouette_cells;
        if (!silhouette.valid) {
            ++missing_frames_;
            last_result_ = out;
            return out;
        }

        out.hand_valid = true;
        const int px = silhouette.gx * touchplus::depth::kDepthScale + 1;
        const int py = silhouette.gy * touchplus::depth::kDepthScale + 1;
        out.pixel_x = px;
        out.pixel_y = py;

        int nearest_d_small = 0;
        int nearest_dist2 = std::numeric_limits<int>::max();
        for (int gy = 0; gy < touchplus::depth::kDepthHeight; ++gy) {
            for (int gx = 0; gx < touchplus::depth::kDepthWidth; ++gx) {
                const size_t idx =
                    static_cast<size_t>(gy) * touchplus::depth::kDepthWidth + gx;
                if (!depth_core_mask[idx] ||
                    selected_mask_.empty() ||
                    !selected_mask_[idx]) {
                    continue;
                }
                const int dx = gx - silhouette.gx;
                const int dy = gy - silhouette.gy;
                const int d2 = dx * dx + dy * dy;
                if (d2 < nearest_dist2) {
                    nearest_dist2 = d2;
                    nearest_d_small = core_disp_small[idx];
                }
            }
        }

        if (nearest_d_small <= 0 || nearest_dist2 > 48 * 48) {
            out.confidence = "LOW";
            ++missing_frames_;
            last_result_ = out;
            return out;
        }

        const double coarse_disp =
            static_cast<double>(
                nearest_d_small * touchplus::depth::kDepthScale);
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
                if (sx < 12 ||
                    sx >= touchplus::depth::kEyeWidth - 5 ||
                    sy < 5 ||
                    sy >= touchplus::depth::kEyeHeight - 5) {
                    continue;
                }

                const int sgx = sx / touchplus::depth::kDepthScale;
                const int sgy = sy / touchplus::depth::kDepthScale;
                if (!mask_near_v5(
                        selected_mask_,
                        touchplus::depth::kDepthWidth,
                        touchplus::depth::kDepthHeight,
                        sgx, sgy, 1)) {
                    continue;
                }

                const auto match =
                    touchplus::depth::robust_point_detail::
                        mutually_consistent_match(
                            left_gray, right_gray,
                            sx, sy, min_d, max_d);
                if (!match.valid) continue;

                const auto camera =
                    touchplus::surface::camera_point_from_q(
                        calibration,
                        static_cast<double>(sx),
                        static_cast<double>(sy),
                        match.disparity);
                if (!std::isfinite(camera.x) ||
                    !std::isfinite(camera.y) ||
                    !std::isfinite(camera.z)) {
                    continue;
                }
                const auto sp =
                    touchplus::surface::to_surface(surface, camera);
                if (!std::isfinite(sp.h_mm) ||
                    sp.h_mm < 2.0 ||
                    sp.h_mm > kV2MaxForegroundHmm + 40.0 ||
                    std::abs(sp.x_mm) > roi_half_x ||
                    std::abs(sp.y_mm) > roi_half_y) {
                    continue;
                }
                refined.push_back(sp);
            }
        }

        if (!refined.empty()) {
            std::vector<double> hs;
            hs.reserve(refined.size());
            for (const auto& p : refined) hs.push_back(p.h_mm);
            const double median_h =
                touchplus::surface::median(std::move(hs));
            std::vector<touchplus::surface::SurfacePoint> consistent;
            consistent.reserve(refined.size());
            for (const auto& p : refined) {
                if (std::abs(p.h_mm - median_h) <= 24.0) {
                    consistent.push_back(p);
                }
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
        const bool strong_silhouette =
            out.hand_samples >= 280 &&
            out.hand_samples <= kV5MaxSilhouetteCells;
        out.confidence =
            (strong_silhouette && refined.size() >= 6)
                ? "HIGH" : "MEDIUM";

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
            smoothed_.x_mm =
                smoothed_.x_mm * (1.0 - alpha) +
                out.raw_tip.x_mm * alpha;
            smoothed_.y_mm =
                smoothed_.y_mm * (1.0 - alpha) +
                out.raw_tip.y_mm * alpha;
            smoothed_.h_mm =
                smoothed_.h_mm * (1.0 - alpha) +
                out.raw_tip.h_mm * alpha;
        } else {
            smoothed_ = out.raw_tip;
            have_smoothed_ = true;
        }

        missing_frames_ = 0;
        out.smoothed_tip = smoothed_;
        out.fingertip_valid =
            out.confidence == "HIGH" ||
            out.confidence == "MEDIUM";
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
    const std::vector<uint8_t>& selected_mask() const {
        return selected_mask_;
    }

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

        const size_t depth_cells =
            static_cast<size_t>(touchplus::depth::kDepthWidth) *
            touchplus::depth::kDepthHeight;
        if (background_disp_sum_.size() != depth_cells ||
            background_disp_count_.size() != depth_cells) {
            return;
        }

        for (size_t i = 0; i < depth_cells; ++i) {
            const int d = workspace.best_disp[i];
            if (d <= 0) continue;
            background_disp_sum_[i] += static_cast<uint32_t>(d);
            if (background_disp_count_[i] <
                std::numeric_limits<uint16_t>::max()) {
                ++background_disp_count_[i];
            }
        }

        ++background_frames_;
        if (background_frames_ < kV5BackgroundFrames) return;

        background_left_.resize(left_gray.size());
        for (size_t i = 0; i < left_gray.size(); ++i) {
            background_left_[i] = static_cast<uint8_t>(
                background_left_sum_[i] /
                static_cast<uint32_t>(background_frames_));
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
