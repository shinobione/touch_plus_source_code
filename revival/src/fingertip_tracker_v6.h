#pragma once

#include "fingertip_tracker_v5.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace touchplus::tracking {

// Phase 2B.6 physical-smoke correction (SUPERSEDED FOR IDENTITY BY 2B.7).
//
// 2B.5 proved that appearance silhouette is the right source for anatomical
// identity, but the real benchmark exposed two remaining failure modes:
//   1) hand shadows / photometric tails can stay connected to the silhouette;
//   2) scoring every silhouette pixel can choose a proximal/interior point.
//
// V6 therefore keeps the V5 learned-background segmentation, then:
//   - rebuilds a robust above-plane dense-depth SUPPORT core inside that mask;
//   - keeps appearance-only silhouette only within a bounded geodesic distance
//     from that physical support (enough for a low-texture distal finger);
//   - skeletonizes the resulting hand silhouette;
//   - considers skeleton ENDPOINTS only, measured from the top-entry wrist;
//   - rejects near-tied distal branches as anatomically ambiguous;
//   - extends the winning skeleton endpoint back to the visible silhouette edge;
//   - only then runs the proven full-resolution stereo matcher around that tip.
//
// Physical smoke later proved the wrist-root endpoint identity itself can still
// emit anatomically wrong HIGH-confidence fingertip points. 2B.7 therefore keeps
// V6's support-bounding helpers but replaces the identity stage with a palm-core
// / external-finger-branch decomposition inspired by recovered Ractiv SCOPA.
//
// This remains the controlled Phase 2B desk boundary: one top-entry hand with
// one clearly dominant extended index. Splayed / multi-finger ambiguity must
// degrade to unknown rather than silently selecting an arbitrary finger.

constexpr int kV6MaxAppearanceExtensionCells = 40; // half-res cells = ~80 px
constexpr size_t kV6MinSupportedCells = 18;
constexpr size_t kV6MinHandCells = 120;
constexpr int kV6MinEntryCells = 8;
constexpr int kV6EntryMaxGy = 82;
constexpr int kV6MinVerticalSpan = 24;
constexpr double kV6MinSupportHmm = 8.0;
constexpr double kV6MaxSupportHmm = 270.0;
constexpr double kV6AmbiguousDistanceRatio = 0.92;
constexpr double kV6AmbiguousScoreRatio = 0.96;
constexpr int kV6AmbiguousSeparationCells = 14;

inline size_t mask_count_v6(const std::vector<uint8_t>& mask) {
    size_t count = 0;
    for (const auto v : mask) count += v ? 1u : 0u;
    return count;
}

struct SupportedMaskV6 {
    bool valid = false;
    size_t cells = 0;
    size_t support_cells = 0;
    std::vector<uint8_t> mask;
};

inline SupportedMaskV6 constrain_to_physical_support_v6(
    const std::vector<uint8_t>& appearance_component,
    const std::vector<uint8_t>& support,
    int width,
    int height,
    int max_extension = kV6MaxAppearanceExtensionCells) {

    SupportedMaskV6 out;
    const size_t cell_count = static_cast<size_t>(width) * height;
    if (width <= 0 || height <= 0 ||
        appearance_component.size() != cell_count ||
        support.size() != cell_count) {
        return out;
    }

    constexpr std::array<std::array<int, 2>, 8> neighbors{{
        {{-1,-1}}, {{0,-1}}, {{1,-1}}, {{-1,0}},
        {{1,0}}, {{-1,1}}, {{0,1}}, {{1,1}}
    }};

    // Multi-source geodesic distance from physically supported cells, but only
    // through the already selected V5 appearance silhouette.
    std::vector<int> distance(cell_count, -1);
    std::vector<int> queue;
    queue.reserve(cell_count);
    for (size_t idx = 0; idx < cell_count; ++idx) {
        if (!appearance_component[idx] || !support[idx]) continue;
        distance[idx] = 0;
        queue.push_back(static_cast<int>(idx));
    }
    if (queue.size() < kV6MinSupportedCells) return out;

    size_t head = 0;
    while (head < queue.size()) {
        const int flat = queue[head++];
        const int d = distance[static_cast<size_t>(flat)];
        if (d >= max_extension) continue;
        const int y = flat / width;
        const int x = flat - y * width;
        for (const auto& n : neighbors) {
            const int nx = x + n[0];
            const int ny = y + n[1];
            if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
            const size_t ni = static_cast<size_t>(ny) * width + nx;
            if (!appearance_component[ni] || distance[ni] >= 0) continue;
            distance[ni] = d + 1;
            queue.push_back(static_cast<int>(ni));
        }
    }

    std::vector<uint8_t> bounded(cell_count, 0);
    for (size_t idx = 0; idx < cell_count; ++idx) {
        if (distance[idx] >= 0 && distance[idx] <= max_extension) bounded[idx] = 1;
    }

    // Re-select a single supported top-entry component after trimming. This is
    // important because bounded propagation can split a previously shadow-joined
    // V5 component into the real hand plus unrelated fragments.
    struct Stats {
        int label = -1;
        size_t cells = 0;
        size_t support_cells = 0;
        int entry = 0;
        int min_y = std::numeric_limits<int>::max();
        int max_y = -1;
    };

    std::vector<int> labels(cell_count, -1);
    std::vector<Stats> stats;
    queue.clear();
    int next_label = 0;

    for (int sy = 0; sy < height; ++sy) {
        for (int sx = 0; sx < width; ++sx) {
            const size_t seed = static_cast<size_t>(sy) * width + sx;
            if (!bounded[seed] || labels[seed] >= 0) continue;

            Stats s;
            s.label = next_label;
            queue.clear();
            queue.push_back(static_cast<int>(seed));
            labels[seed] = next_label;
            size_t qhead = 0;

            while (qhead < queue.size()) {
                const int flat = queue[qhead++];
                const int y = flat / width;
                const int x = flat - y * width;
                const size_t idx = static_cast<size_t>(flat);
                ++s.cells;
                if (support[idx]) ++s.support_cells;
                if (y <= kV6EntryMaxGy) ++s.entry;
                s.min_y = std::min(s.min_y, y);
                s.max_y = std::max(s.max_y, y);

                for (const auto& n : neighbors) {
                    const int nx = x + n[0];
                    const int ny = y + n[1];
                    if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
                    const size_t ni = static_cast<size_t>(ny) * width + nx;
                    if (bounded[ni] && labels[ni] < 0) {
                        labels[ni] = next_label;
                        queue.push_back(static_cast<int>(ni));
                    }
                }
            }
            stats.push_back(s);
            ++next_label;
        }
    }

    int best_label = -1;
    double best_score = -1.0;
    Stats best;
    for (const auto& s : stats) {
        if (s.cells < kV6MinHandCells) continue;
        if (s.support_cells < kV6MinSupportedCells) continue;
        if (s.entry < kV6MinEntryCells) continue;
        if (s.max_y - s.min_y < kV6MinVerticalSpan) continue;
        const double score = static_cast<double>(s.cells) +
                             6.0 * static_cast<double>(s.support_cells);
        if (score > best_score) {
            best_score = score;
            best_label = s.label;
            best = s;
        }
    }
    if (best_label < 0) return out;

    out.mask.assign(cell_count, 0);
    for (size_t idx = 0; idx < cell_count; ++idx) {
        if (labels[idx] == best_label && bounded[idx]) out.mask[idx] = 1;
    }
    out.cells = best.cells;
    out.support_cells = best.support_cells;
    out.valid = true;
    return out;
}

inline int skeleton_neighbor_count_v6(
    const std::vector<uint8_t>& mask,
    int width,
    int height,
    int x,
    int y) {

    int count = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            const int nx = x + dx;
            const int ny = y + dy;
            if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
            if (mask[static_cast<size_t>(ny) * width + nx]) ++count;
        }
    }
    return count;
}

inline int skeleton_transitions_v6(
    const std::vector<uint8_t>& m,
    int width,
    int x,
    int y) {

    const auto at = [&](int xx, int yy) -> int {
        return m[static_cast<size_t>(yy) * width + xx] ? 1 : 0;
    };
    const int p2 = at(x, y - 1);
    const int p3 = at(x + 1, y - 1);
    const int p4 = at(x + 1, y);
    const int p5 = at(x + 1, y + 1);
    const int p6 = at(x, y + 1);
    const int p7 = at(x - 1, y + 1);
    const int p8 = at(x - 1, y);
    const int p9 = at(x - 1, y - 1);
    const std::array<int, 9> ring{{p2,p3,p4,p5,p6,p7,p8,p9,p2}};
    int transitions = 0;
    for (size_t i = 0; i + 1 < ring.size(); ++i) {
        if (ring[i] == 0 && ring[i + 1] == 1) ++transitions;
    }
    return transitions;
}

inline std::vector<uint8_t> zhang_suen_skeleton_v6(
    const std::vector<uint8_t>& input,
    int width,
    int height) {

    std::vector<uint8_t> m = input;
    if (m.size() != static_cast<size_t>(width) * height || width < 3 || height < 3) {
        return m;
    }

    std::vector<size_t> remove;
    remove.reserve(m.size() / 8);

    bool changed = true;
    int iterations = 0;
    while (changed && iterations++ < 96) {
        changed = false;
        for (int phase = 0; phase < 2; ++phase) {
            remove.clear();
            for (int y = 1; y < height - 1; ++y) {
                for (int x = 1; x < width - 1; ++x) {
                    const size_t idx = static_cast<size_t>(y) * width + x;
                    if (!m[idx]) continue;

                    const auto at = [&](int xx, int yy) -> int {
                        return m[static_cast<size_t>(yy) * width + xx] ? 1 : 0;
                    };
                    const int p2 = at(x, y - 1);
                    const int p4 = at(x + 1, y);
                    const int p6 = at(x, y + 1);
                    const int p8 = at(x - 1, y);
                    const int neighbors = skeleton_neighbor_count_v6(m, width, height, x, y);
                    if (neighbors < 2 || neighbors > 6) continue;
                    if (skeleton_transitions_v6(m, width, x, y) != 1) continue;

                    if (phase == 0) {
                        if (p2 * p4 * p6 != 0) continue;
                        if (p4 * p6 * p8 != 0) continue;
                    } else {
                        if (p2 * p4 * p8 != 0) continue;
                        if (p2 * p6 * p8 != 0) continue;
                    }
                    remove.push_back(idx);
                }
            }
            if (!remove.empty()) changed = true;
            for (const size_t idx : remove) m[idx] = 0;
        }
    }
    return m;
}

struct SkeletonTipV6 {
    bool valid = false;
    bool ambiguous = false;
    int gx = -1;
    int gy = -1;
    int geodesic_steps = 0;
    int endpoint_count = 0;
    size_t skeleton_cells = 0;
};

inline SkeletonTipV6 skeleton_distal_tip_v6(
    const std::vector<uint8_t>& hand_mask,
    int width,
    int height) {

    SkeletonTipV6 out;
    const size_t cell_count = static_cast<size_t>(width) * height;
    if (hand_mask.size() != cell_count) return out;

    const std::vector<uint8_t> skeleton = zhang_suen_skeleton_v6(hand_mask, width, height);
    out.skeleton_cells = mask_count_v6(skeleton);
    if (out.skeleton_cells < 10) return out;

    int min_y = height;
    int max_y = -1;
    int min_x = width;
    int max_x = -1;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!hand_mask[static_cast<size_t>(y) * width + x]) continue;
            min_y = std::min(min_y, y);
            max_y = std::max(max_y, y);
            min_x = std::min(min_x, x);
            max_x = std::max(max_x, x);
        }
    }
    if (max_y < min_y) return out;

    const int y_span = std::max(1, max_y - min_y);
    const int anchor_band = std::max(5, static_cast<int>(std::lround(y_span * 0.14)));
    const int anchor_max_y = std::min(max_y, min_y + anchor_band);

    constexpr std::array<std::array<int, 2>, 8> neighbors{{
        {{-1,-1}}, {{0,-1}}, {{1,-1}}, {{-1,0}},
        {{1,0}}, {{-1,1}}, {{0,1}}, {{1,1}}
    }};

    std::vector<int> distance(cell_count, -1);
    std::vector<int> parent(cell_count, -1);
    std::vector<int> queue;
    queue.reserve(cell_count);
    double anchor_x_sum = 0.0;
    double anchor_y_sum = 0.0;
    int anchor_count = 0;

    for (int y = min_y; y <= anchor_max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const size_t idx = static_cast<size_t>(y) * width + x;
            if (!skeleton[idx]) continue;
            distance[idx] = 0;
            queue.push_back(static_cast<int>(idx));
            anchor_x_sum += x;
            anchor_y_sum += y;
            ++anchor_count;
        }
    }
    if (queue.empty()) return out;
    const double anchor_x = anchor_x_sum / std::max(1, anchor_count);
    const double anchor_y = anchor_y_sum / std::max(1, anchor_count);

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
            if (!skeleton[ni] || distance[ni] >= 0) continue;
            distance[ni] = distance[static_cast<size_t>(flat)] + 1;
            parent[ni] = flat;
            queue.push_back(static_cast<int>(ni));
        }
    }

    int max_distance = 0;
    for (size_t idx = 0; idx < cell_count; ++idx) {
        if (skeleton[idx]) max_distance = std::max(max_distance, distance[idx]);
    }
    if (max_distance < 10) return out;

    struct Candidate {
        int flat = -1;
        int distance = 0;
        double score = 0.0;
    };
    std::vector<Candidate> candidates;

    const double diagonal = std::max(1.0, std::hypot(max_x - min_x, max_y - min_y));
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            const size_t idx = static_cast<size_t>(y) * width + x;
            if (!skeleton[idx] || distance[idx] < 0) continue;
            if (y <= anchor_max_y + 3) continue;
            if (skeleton_neighbor_count_v6(skeleton, width, height, x, y) > 1) continue;
            if (distance[idx] < static_cast<int>(std::floor(max_distance * 0.55))) continue;

            const double geodesic = static_cast<double>(distance[idx]) / max_distance;
            const double radial = std::hypot(x - anchor_x, y - anchor_y) / diagonal;
            const double downward = std::clamp(
                static_cast<double>(y - min_y) / std::max(1, y_span), 0.0, 1.0);
            // Geodesic branch length is the primary anatomical signal. Radial
            // displacement makes diagonal fingers competitive; downward position
            // is only a weak tie-breaker, unlike V5's dominant y bias.
            const double score = 0.76 * geodesic + 0.19 * radial + 0.05 * downward;
            candidates.push_back({static_cast<int>(idx), distance[idx], score});
        }
    }

    out.endpoint_count = static_cast<int>(candidates.size());
    if (candidates.empty()) return out;
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.distance > b.distance;
    });

    const Candidate best = candidates.front();
    if (candidates.size() >= 2) {
        const Candidate second = candidates[1];
        const int bx = best.flat % width;
        const int by = best.flat / width;
        const int sx = second.flat % width;
        const int sy = second.flat / width;
        const double separation = std::hypot(bx - sx, by - sy);
        if (second.distance >= static_cast<int>(std::floor(best.distance * kV6AmbiguousDistanceRatio)) &&
            second.score >= best.score * kV6AmbiguousScoreRatio &&
            separation >= kV6AmbiguousSeparationCells) {
            out.ambiguous = true;
            return out;
        }
    }

    int ex = best.flat % width;
    int ey = best.flat / width;

    // Estimate the outgoing branch direction from several skeleton steps behind
    // the endpoint, then extend to the actual visible silhouette boundary.
    int back = best.flat;
    for (int i = 0; i < 8 && parent[static_cast<size_t>(back)] >= 0; ++i) {
        back = parent[static_cast<size_t>(back)];
    }
    const int back_x = back % width;
    const int back_y = back / width;
    double dx = static_cast<double>(ex - back_x);
    double dy = static_cast<double>(ey - back_y);
    const double len = std::hypot(dx, dy);
    if (len > 1e-6) {
        dx /= len;
        dy /= len;
        int last_x = ex;
        int last_y = ey;
        for (double t = 0.5; t <= 20.0; t += 0.5) {
            const int tx = static_cast<int>(std::lround(ex + dx * t));
            const int ty = static_cast<int>(std::lround(ey + dy * t));
            if (tx < 0 || tx >= width || ty < 0 || ty >= height) break;
            if (!hand_mask[static_cast<size_t>(ty) * width + tx]) break;
            last_x = tx;
            last_y = ty;
        }
        ex = last_x;
        ey = last_y;
    }

    out.valid = true;
    out.gx = ex;
    out.gy = ey;
    out.geodesic_steps = best.distance;
    return out;
}

class FingertipTrackerV6 {
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

        const TrackingResult base_result =
            base_.update(calibration, surface, left_gray, right_gray, workspace);

        TrackingResult out;
        selected_mask_ = base_.selected_mask();
        out.foreground_samples = base_result.foreground_samples;
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

        const auto tip = skeleton_distal_tip_v6(
            selected_mask_,
            touchplus::depth::kDepthWidth,
            touchplus::depth::kDepthHeight);
        if (!tip.valid || tip.ambiguous) {
            out.confidence = "LOW";
            ++missing_frames_;
            last_result_ = out;
            return out;
        }

        const int px = tip.gx * touchplus::depth::kDepthScale + 1;
        const int py = tip.gy * touchplus::depth::kDepthScale + 1;
        out.pixel_x = px;
        out.pixel_y = py;

        int nearest_d_small = 0;
        int nearest_dist2 = std::numeric_limits<int>::max();
        for (int gy = 0; gy < touchplus::depth::kDepthHeight; ++gy) {
            for (int gx = 0; gx < touchplus::depth::kDepthWidth; ++gx) {
                const size_t idx = static_cast<size_t>(gy) * touchplus::depth::kDepthWidth + gx;
                if (!support[idx] || !selected_mask_[idx]) continue;
                const int dx = gx - tip.gx;
                const int dy = gy - tip.gy;
                const int d2 = dx * dx + dy * dy;
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

private:
    void clear_tracking_only() {
        selected_mask_.clear();
        last_result_ = {};
        have_smoothed_ = false;
        missing_frames_ = 0;
        smoothed_ = {};
    }

    FingertipTrackerV5 base_;
    TrackingResult last_result_{};
    std::vector<uint8_t> selected_mask_;
    bool have_smoothed_ = false;
    int missing_frames_ = 0;
    touchplus::surface::SurfacePoint smoothed_{};
};

} // namespace touchplus::tracking
