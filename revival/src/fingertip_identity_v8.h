#pragma once

#include "fingertip_tracker_v7.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace touchplus::tracking {

// Phase 2B.8 — temporal palm + persistent finger identity.
//
// V7 proved that a plausible palm/branch decomposition is not sufficient when
// every frame independently re-elects the "best" distal branch. V8 therefore
// keeps identity in image space before stereo refinement:
//   supported silhouette
//     -> validated palm observation
//     -> finger-like branch descriptors
//     -> short palm persistence
//     -> persistent branch association
//     -> unexplained 2D jump rejection
//     -> only LOCKED identity may reach the stereo matcher.
//
// A rejected/ambiguous identity is deliberately UNKNOWN. Strong stereo support
// must never resurrect an anatomically rejected pixel.

constexpr double kV8MinPalmCoreFill = 0.72;
constexpr double kV8MinPalmRadiusCells = 6.0;
constexpr double kV8MaxPalmRadiusBBoxRatio = 0.48;
constexpr double kV8MinFingerExtensionRadiusScale = 0.62;
constexpr double kV8MaxFingerWidthPalmRatio = 0.85;
constexpr double kV8MinBranchLinearity = 0.72;
constexpr double kV8StaticAmbiguityScoreGap = 0.09;
constexpr double kV8AssociationAmbiguityGap = 0.10;
constexpr double kV8MinAssociationScore = 0.58;
constexpr int kV8AcquireFrames = 3;
constexpr int kV8MaxIdentityMisses = 3;

inline double clamp01_v8(double v) {
    return std::clamp(v, 0.0, 1.0);
}

inline double wrap_angle_v8(double a) {
    constexpr double pi = 3.14159265358979323846;
    while (a > pi) a -= 2.0 * pi;
    while (a < -pi) a += 2.0 * pi;
    return a;
}

inline double angle_distance_v8(double a, double b) {
    return std::abs(wrap_angle_v8(a - b));
}

inline double median_v8(std::vector<double> values) {
    if (values.empty()) return 0.0;
    const size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(mid), values.end());
    double m = values[mid];
    if ((values.size() & 1U) == 0U) {
        const auto lo = std::max_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(mid));
        m = (*lo + m) * 0.5;
    }
    return m;
}

struct FingerBranchV8 {
    bool valid = false;
    int root_gx = -1;
    int root_gy = -1;
    int skeleton_tip_gx = -1;
    int skeleton_tip_gy = -1;
    int tip_gx = -1;
    int tip_gy = -1;
    int cells = 0;
    double extension = 0.0;
    double extension_ratio = 0.0;
    double root_angle = 0.0;
    double direction_angle = 0.0;
    double proximal_width = 0.0;
    double mid_width = 0.0;
    double distal_width = 0.0;
    double linearity = 0.0;
    double geometry_score = 0.0;
};

struct IdentityObservationV8 {
    bool hand_valid = false;
    bool palm_valid = false;
    bool static_ambiguous = false;
    int palm_gx = -1;
    int palm_gy = -1;
    double palm_radius = 0.0;
    double palm_core_fill = 0.0;
    double palm_score = 0.0;
    int raw_branch_count = 0;
    int rejected_forearm_branches = 0;
    std::vector<FingerBranchV8> candidates;
};

enum class IdentityStateV8 {
    Unknown,
    Acquiring,
    Locked
};

inline const char* identity_state_name_v8(IdentityStateV8 state) {
    switch (state) {
        case IdentityStateV8::Acquiring: return "ACQUIRING";
        case IdentityStateV8::Locked: return "LOCKED";
        default: return "UNKNOWN";
    }
}

struct IdentityDecisionV8 {
    bool has_candidate = false;
    bool publish = false;
    bool ambiguous = false;
    bool palm_rejected = false;
    bool jump_rejected = false;
    bool association_rejected = false;
    int tip_gx = -1;
    int tip_gy = -1;
    std::uint64_t branch_id = 0;
    IdentityStateV8 state = IdentityStateV8::Unknown;
    std::string confidence = "LOW";
    double association_score = 0.0;
    double palm_residual = 0.0;
    double tip_residual = 0.0;
};

inline int best_static_candidate_index_v8(const IdentityObservationV8& obs) {
    if (obs.candidates.empty()) return -1;
    int best = 0;
    for (int i = 1; i < static_cast<int>(obs.candidates.size()); ++i) {
        if (obs.candidates[i].geometry_score > obs.candidates[best].geometry_score) {
            best = i;
        }
    }
    return best;
}

inline IdentityObservationV8 analyze_finger_identity_v8(
    const std::vector<uint8_t>& hand_mask,
    int width,
    int height) {

    IdentityObservationV8 out;
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
    if (hand_cells < kV6MinHandCells || max_x <= min_x || max_y <= min_y) return out;
    out.hand_valid = true;

    // Reuse only V7's interior-distance palm proposal. V8 deliberately ignores
    // V7's frame-local branch winner and validates the palm independently.
    const PalmBranchTipV7 v7 = palm_core_fingertip_v7(hand_mask, width, height);
    out.palm_gx = v7.palm_gx;
    out.palm_gy = v7.palm_gy;
    out.palm_radius = v7.palm_radius;
    if (out.palm_gx < 0 || out.palm_gy < 0 || out.palm_radius < kV8MinPalmRadiusCells) {
        return out;
    }

    const int bbox_w = max_x - min_x + 1;
    const int bbox_h = max_y - min_y + 1;
    const double bbox_span = static_cast<double>(std::max(bbox_w, bbox_h));
    if (out.palm_radius > bbox_span * kV8MaxPalmRadiusBBoxRatio) return out;

    // A real palm core should be a filled interior patch, not a narrow finger,
    // wrist or accidental photometric tail. Measure occupancy inside 0.72R.
    const double core_radius = std::max(3.0, out.palm_radius * 0.72);
    int core_total = 0;
    int core_inside = 0;
    const int cr = static_cast<int>(std::ceil(core_radius));
    for (int y = out.palm_gy - cr; y <= out.palm_gy + cr; ++y) {
        for (int x = out.palm_gx - cr; x <= out.palm_gx + cr; ++x) {
            if (x < 0 || x >= width || y < 0 || y >= height) continue;
            const double d = std::hypot(
                static_cast<double>(x - out.palm_gx),
                static_cast<double>(y - out.palm_gy));
            if (d > core_radius) continue;
            ++core_total;
            if (hand_mask[static_cast<size_t>(y) * width + x]) ++core_inside;
        }
    }
    out.palm_core_fill = core_total > 0
        ? static_cast<double>(core_inside) / core_total : 0.0;

    const double entry_depth = static_cast<double>(out.palm_gy - min_y);
    const bool entry_relation_ok =
        entry_depth >= std::max(5.0, out.palm_radius * 0.35);
    const bool center_inside =
        hand_mask[static_cast<size_t>(out.palm_gy) * width + out.palm_gx] != 0;
    out.palm_score =
        0.55 * clamp01_v8((out.palm_core_fill - 0.55) / 0.35) +
        0.25 * clamp01_v8(entry_depth / std::max(1.0, out.palm_radius * 1.2)) +
        0.20 * clamp01_v8(out.palm_radius / std::max(1.0, bbox_span * 0.20));

    if (!center_inside || !entry_relation_ok || out.palm_core_fill < kV8MinPalmCoreFill) {
        return out;
    }
    out.palm_valid = true;

    const std::vector<int> boundary_dist =
        chamfer_inside_distance_v7(hand_mask, width, height);
    const std::vector<uint8_t> skeleton =
        zhang_suen_skeleton_v6(hand_mask, width, height);
    if (mask_count_v6(skeleton) < 10) return out;

    const double cut_radius = std::max(
        kV8MinPalmRadiusCells,
        out.palm_radius * kV7PalmCutRadiusScale);
    const int y_span = std::max(1, max_y - min_y);
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

    constexpr int dx8[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    constexpr int dy8[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

    std::vector<int> labels(cells, -1);
    std::vector<int> queue;
    queue.reserve(cells);
    int next_label = 0;

    struct RawBranch {
        bool touches_top = false;
        bool attaches_palm = false;
        std::vector<int> flats;
    };
    std::vector<RawBranch> raw_branches;

    for (int sy = 0; sy < height; ++sy) {
        for (int sx = 0; sx < width; ++sx) {
            const size_t seed = static_cast<size_t>(sy) * width + sx;
            if (!external[seed] || labels[seed] >= 0) continue;

            RawBranch raw;
            queue.clear();
            queue.push_back(static_cast<int>(seed));
            labels[seed] = next_label;
            size_t head = 0;

            while (head < queue.size()) {
                const int flat = queue[head++];
                raw.flats.push_back(flat);
                const int y = flat / width;
                const int x = flat - y * width;
                if (y <= entry_limit) raw.touches_top = true;
                const double radial = std::hypot(
                    static_cast<double>(x - out.palm_gx),
                    static_cast<double>(y - out.palm_gy));
                if (radial <= cut_radius + kV7PalmAttachBandCells) {
                    raw.attaches_palm = true;
                }

                for (int n = 0; n < 8; ++n) {
                    const int nx = x + dx8[n];
                    const int ny = y + dy8[n];
                    if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
                    const size_t ni = static_cast<size_t>(ny) * width + nx;
                    if (external[ni] && labels[ni] < 0) {
                        labels[ni] = next_label;
                        queue.push_back(static_cast<int>(ni));
                    }
                }
            }
            raw_branches.push_back(std::move(raw));
            ++next_label;
        }
    }
    out.raw_branch_count = static_cast<int>(raw_branches.size());

    for (const auto& raw : raw_branches) {
        if (raw.flats.size() < static_cast<size_t>(kV7MinSkeletonBranchCells) ||
            !raw.attaches_palm) {
            continue;
        }
        if (raw.touches_top) {
            ++out.rejected_forearm_branches;
            continue;
        }

        FingerBranchV8 b;
        b.cells = static_cast<int>(raw.flats.size());

        double min_radial = std::numeric_limits<double>::infinity();
        double max_radial = -1.0;
        int root_flat = -1;
        int tip_flat = -1;
        double mx = 0.0;
        double my = 0.0;

        for (const int flat : raw.flats) {
            const int y = flat / width;
            const int x = flat - y * width;
            const double radial = std::hypot(
                static_cast<double>(x - out.palm_gx),
                static_cast<double>(y - out.palm_gy));
            if (radial < min_radial) {
                min_radial = radial;
                root_flat = flat;
            }
            if (radial > max_radial) {
                max_radial = radial;
                tip_flat = flat;
            }
            mx += x;
            my += y;
        }
        if (root_flat < 0 || tip_flat < 0) continue;
        mx /= raw.flats.size();
        my /= raw.flats.size();

        b.root_gx = root_flat % width;
        b.root_gy = root_flat / width;
        b.skeleton_tip_gx = tip_flat % width;
        b.skeleton_tip_gy = tip_flat / width;
        b.extension = std::max(0.0, max_radial - cut_radius);
        b.extension_ratio = b.extension / std::max(1.0, out.palm_radius);
        if (b.extension_ratio < kV8MinFingerExtensionRadiusScale) continue;

        double cxx = 0.0;
        double cxy = 0.0;
        double cyy = 0.0;
        std::vector<double> proximal_widths;
        std::vector<double> mid_widths;
        std::vector<double> distal_widths;

        for (const int flat : raw.flats) {
            const int y = flat / width;
            const int x = flat - y * width;
            const double rx = x - mx;
            const double ry = y - my;
            cxx += rx * rx;
            cxy += rx * ry;
            cyy += ry * ry;

            const double radial = std::hypot(
                static_cast<double>(x - out.palm_gx),
                static_cast<double>(y - out.palm_gy));
            const double frac = b.extension > 1e-6
                ? clamp01_v8((radial - cut_radius) / b.extension) : 0.0;
            const double local_width =
                2.0 * static_cast<double>(boundary_dist[flat]) / 3.0;
            if (frac < 0.34) proximal_widths.push_back(local_width);
            else if (frac < 0.72) mid_widths.push_back(local_width);
            else distal_widths.push_back(local_width);
        }

        const double trace = cxx + cyy;
        const double disc = std::sqrt(std::max(
            0.0, (cxx - cyy) * (cxx - cyy) + 4.0 * cxy * cxy));
        const double lambda1 = (trace + disc) * 0.5;
        b.linearity = trace > 1e-9 ? lambda1 / trace : 0.0;
        if (b.linearity < kV8MinBranchLinearity) continue;

        b.proximal_width = median_v8(std::move(proximal_widths));
        b.mid_width = median_v8(std::move(mid_widths));
        b.distal_width = median_v8(std::move(distal_widths));
        if (b.mid_width <= 0.0) b.mid_width = b.proximal_width;
        if (b.distal_width <= 0.0) b.distal_width = b.mid_width;

        const double max_finger_width =
            std::max(b.mid_width, b.distal_width);
        const double width_ratio =
            max_finger_width / std::max(1.0, out.palm_radius);
        if (width_ratio > kV8MaxFingerWidthPalmRatio) continue;

        const double rdx = static_cast<double>(b.root_gx - out.palm_gx);
        const double rdy = static_cast<double>(b.root_gy - out.palm_gy);
        const double ddx = static_cast<double>(b.skeleton_tip_gx - b.root_gx);
        const double ddy = static_cast<double>(b.skeleton_tip_gy - b.root_gy);
        const double direction_len = std::hypot(ddx, ddy);
        if (direction_len < 3.0) continue;

        b.root_angle = std::atan2(rdy, rdx);
        b.direction_angle = std::atan2(ddy, ddx);

        const bool taper_ok =
            b.distal_width <= std::max(3.0, b.proximal_width * 1.60) &&
            b.mid_width <= std::max(3.0, b.proximal_width * 1.55);
        const double extension_score =
            clamp01_v8((b.extension_ratio - 0.55) / 1.55);
        const double width_score =
            1.0 - clamp01_v8((width_ratio - 0.24) / 0.60);
        const double linearity_score =
            clamp01_v8((b.linearity - 0.68) / 0.30);
        const double taper_score = taper_ok ? 1.0 : 0.35;
        b.geometry_score =
            0.38 * extension_score +
            0.24 * width_score +
            0.25 * linearity_score +
            0.13 * taper_score;

        if (b.geometry_score < 0.48) continue;

        // Local distal corridor: extend only along the terminal branch axis.
        // This is intentionally much narrower than V7's palm-origin cone.
        const double ux = ddx / direction_len;
        const double uy = ddy / direction_len;
        int visible_x = b.skeleton_tip_gx;
        int visible_y = b.skeleton_tip_gy;
        double best_projection = direction_len;
        const double max_perp =
            std::max(2.5, std::max(b.mid_width, b.distal_width) * 0.90);

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (!hand_mask[static_cast<size_t>(y) * width + x]) continue;
                const double vx = static_cast<double>(x - b.root_gx);
                const double vy = static_cast<double>(y - b.root_gy);
                const double projection = vx * ux + vy * uy;
                if (projection < direction_len - 3.0) continue;
                const double perpendicular = std::abs(vx * uy - vy * ux);
                if (perpendicular > max_perp) continue;
                if (projection > best_projection) {
                    best_projection = projection;
                    visible_x = x;
                    visible_y = y;
                }
            }
        }

        b.tip_gx = visible_x;
        b.tip_gy = visible_y;
        b.valid = true;
        out.candidates.push_back(b);
    }

    std::sort(
        out.candidates.begin(), out.candidates.end(),
        [](const FingerBranchV8& a, const FingerBranchV8& b) {
            if (a.geometry_score != b.geometry_score) {
                return a.geometry_score > b.geometry_score;
            }
            return a.extension > b.extension;
        });

    if (out.candidates.size() >= 2) {
        const auto& a = out.candidates[0];
        const auto& b = out.candidates[1];
        const double separation = std::hypot(
            static_cast<double>(a.tip_gx - b.tip_gx),
            static_cast<double>(a.tip_gy - b.tip_gy));
        out.static_ambiguous =
            (a.geometry_score - b.geometry_score) < kV8StaticAmbiguityScoreGap &&
            separation >= std::max(8.0, out.palm_radius * 0.60);
    }

    return out;
}

struct BranchTrackSignatureV8 {
    double root_angle = 0.0;
    double direction_angle = 0.0;
    double extension_ratio = 0.0;
    double width_ratio = 0.0;
    double geometry_score = 0.0;
    int tip_gx = -1;
    int tip_gy = -1;
};

class TemporalIdentityGateV8 {
public:
    IdentityDecisionV8 update(const IdentityObservationV8& obs) {
        IdentityDecisionV8 out;
        out.state = state_;

        if (!obs.hand_valid || !obs.palm_valid || obs.candidates.empty()) {
            register_miss();
            out.state = state_;
            return out;
        }

        if (have_palm_) {
            const double predicted_x = palm_x_ + palm_vx_;
            const double predicted_y = palm_y_ + palm_vy_;
            out.palm_residual = std::hypot(
                obs.palm_gx - predicted_x,
                obs.palm_gy - predicted_y);
            const double palm_gate = std::max(6.0, palm_radius_ * 0.60);
            const double ratio = obs.palm_radius / std::max(1.0, palm_radius_);
            if (out.palm_residual > palm_gate || ratio < 0.65 || ratio > 1.50) {
                out.palm_rejected = true;
                register_miss();
                out.state = state_;
                return out;
            }
        }

        const int static_best = best_static_candidate_index_v8(obs);
        if (static_best < 0) {
            register_miss();
            out.state = state_;
            return out;
        }

        if (state_ == IdentityStateV8::Unknown || !have_branch_) {
            if (obs.static_ambiguous) {
                out.ambiguous = true;
                register_miss();
                out.state = state_;
                return out;
            }
            start_acquisition(obs, obs.candidates[static_best]);
            fill_candidate(out, obs.candidates[static_best]);
            out.state = state_;
            out.branch_id = branch_id_;
            out.confidence = "LOW";
            return out;
        }

        const double palm_dx =
            static_cast<double>(obs.palm_gx) - palm_x_;
        const double palm_dy =
            static_cast<double>(obs.palm_gy) - palm_y_;

        int best_index = -1;
        double best_score = -1.0;
        double second_score = -1.0;
        double best_tip_residual = std::numeric_limits<double>::infinity();

        for (int i = 0; i < static_cast<int>(obs.candidates.size()); ++i) {
            double tip_residual = 0.0;
            const double score = association_score(
                obs, obs.candidates[i], palm_dx, palm_dy, tip_residual);
            if (score > best_score) {
                second_score = best_score;
                best_score = score;
                best_tip_residual = tip_residual;
                best_index = i;
            } else if (score > second_score) {
                second_score = score;
            }
        }

        if (best_index < 0) {
            out.association_rejected = true;
            register_miss();
            out.state = state_;
            return out;
        }

        const double jump_gate = std::max(10.0, palm_radius_ * 0.85);
        out.tip_residual = best_tip_residual;
        if (best_tip_residual > jump_gate) {
            out.jump_rejected = true;
            register_miss();
            out.state = state_;
            return out;
        }

        if (best_score < kV8MinAssociationScore) {
            out.association_rejected = true;
            register_miss();
            out.state = state_;
            return out;
        }

        if (second_score >= 0.0 &&
            (best_score - second_score) < kV8AssociationAmbiguityGap) {
            out.ambiguous = true;
            register_miss();
            out.state = state_;
            return out;
        }

        const FingerBranchV8& matched = obs.candidates[best_index];
        update_track(obs, matched);
        out.association_score = best_score;
        fill_candidate(out, matched);

        ++acquire_count_;
        misses_ = 0;
        if (state_ == IdentityStateV8::Acquiring &&
            acquire_count_ >= kV8AcquireFrames) {
            state_ = IdentityStateV8::Locked;
        }

        out.state = state_;
        out.branch_id = branch_id_;
        if (state_ == IdentityStateV8::Locked) {
            out.publish = true;
            out.confidence =
                (best_score >= 0.78 && matched.geometry_score >= 0.70 &&
                 obs.palm_score >= 0.65)
                ? "HIGH" : "MEDIUM";
        } else {
            out.confidence = "LOW";
        }
        return out;
    }

    void clear() {
        state_ = IdentityStateV8::Unknown;
        have_palm_ = false;
        have_branch_ = false;
        acquire_count_ = 0;
        misses_ = 0;
        branch_id_ = 0;
        next_branch_id_ = 1;
        palm_x_ = palm_y_ = palm_radius_ = 0.0;
        palm_vx_ = palm_vy_ = 0.0;
        branch_ = {};
    }

    IdentityStateV8 state() const { return state_; }
    std::uint64_t branch_id() const { return branch_id_; }

private:
    static double width_ratio(const FingerBranchV8& b, double palm_radius) {
        return std::max(b.mid_width, b.distal_width) / std::max(1.0, palm_radius);
    }

    void fill_candidate(IdentityDecisionV8& out, const FingerBranchV8& b) const {
        out.has_candidate = true;
        out.tip_gx = b.tip_gx;
        out.tip_gy = b.tip_gy;
    }

    void start_acquisition(
        const IdentityObservationV8& obs,
        const FingerBranchV8& b) {

        state_ = IdentityStateV8::Acquiring;
        acquire_count_ = 1;
        misses_ = 0;
        branch_id_ = next_branch_id_++;
        have_palm_ = true;
        have_branch_ = true;
        palm_x_ = static_cast<double>(obs.palm_gx);
        palm_y_ = static_cast<double>(obs.palm_gy);
        palm_radius_ = obs.palm_radius;
        palm_vx_ = palm_vy_ = 0.0;
        store_branch(obs, b);
    }

    void store_branch(
        const IdentityObservationV8& obs,
        const FingerBranchV8& b) {

        branch_.root_angle = b.root_angle;
        branch_.direction_angle = b.direction_angle;
        branch_.extension_ratio = b.extension_ratio;
        branch_.width_ratio = width_ratio(b, obs.palm_radius);
        branch_.geometry_score = b.geometry_score;
        branch_.tip_gx = b.tip_gx;
        branch_.tip_gy = b.tip_gy;
    }

    void update_track(
        const IdentityObservationV8& obs,
        const FingerBranchV8& b) {

        const double new_x = static_cast<double>(obs.palm_gx);
        const double new_y = static_cast<double>(obs.palm_gy);
        const double dx = new_x - palm_x_;
        const double dy = new_y - palm_y_;
        palm_vx_ = 0.55 * palm_vx_ + 0.45 * dx;
        palm_vy_ = 0.55 * palm_vy_ + 0.45 * dy;
        palm_x_ = new_x;
        palm_y_ = new_y;
        palm_radius_ = 0.70 * palm_radius_ + 0.30 * obs.palm_radius;

        const double old_root = branch_.root_angle;
        const double old_dir = branch_.direction_angle;
        store_branch(obs, b);
        branch_.root_angle = wrap_angle_v8(
            old_root + 0.45 * wrap_angle_v8(branch_.root_angle - old_root));
        branch_.direction_angle = wrap_angle_v8(
            old_dir + 0.45 * wrap_angle_v8(branch_.direction_angle - old_dir));
    }

    double association_score(
        const IdentityObservationV8& obs,
        const FingerBranchV8& b,
        double palm_dx,
        double palm_dy,
        double& tip_residual) const {

        const double root_delta =
            angle_distance_v8(b.root_angle, branch_.root_angle);
        const double dir_delta =
            angle_distance_v8(b.direction_angle, branch_.direction_angle);
        const double length_delta = std::abs(
            b.extension_ratio - branch_.extension_ratio) /
            std::max(0.45, branch_.extension_ratio);
        const double candidate_width = width_ratio(b, obs.palm_radius);
        const double width_delta = std::abs(
            candidate_width - branch_.width_ratio) /
            std::max(0.18, branch_.width_ratio);

        const double expected_tip_x = branch_.tip_gx + palm_dx;
        const double expected_tip_y = branch_.tip_gy + palm_dy;
        tip_residual = std::hypot(
            b.tip_gx - expected_tip_x,
            b.tip_gy - expected_tip_y);
        const double jump_scale = std::max(10.0, palm_radius_ * 0.85);

        const double root_score =
            1.0 - clamp01_v8(root_delta / 0.75);
        const double dir_score =
            1.0 - clamp01_v8(dir_delta / 0.85);
        const double length_score =
            1.0 - clamp01_v8(length_delta / 0.75);
        const double width_score =
            1.0 - clamp01_v8(width_delta / 0.90);
        const double tip_score =
            1.0 - clamp01_v8(tip_residual / jump_scale);

        return
            0.20 * root_score +
            0.20 * dir_score +
            0.15 * length_score +
            0.10 * width_score +
            0.25 * tip_score +
            0.10 * clamp01_v8(b.geometry_score);
    }

    void register_miss() {
        ++misses_;
        if (misses_ > kV8MaxIdentityMisses) {
            state_ = IdentityStateV8::Unknown;
            have_palm_ = false;
            have_branch_ = false;
            acquire_count_ = 0;
            branch_id_ = 0;
            palm_vx_ = palm_vy_ = 0.0;
            branch_ = {};
        }
    }

    IdentityStateV8 state_ = IdentityStateV8::Unknown;
    bool have_palm_ = false;
    bool have_branch_ = false;
    int acquire_count_ = 0;
    int misses_ = 0;
    std::uint64_t branch_id_ = 0;
    std::uint64_t next_branch_id_ = 1;
    double palm_x_ = 0.0;
    double palm_y_ = 0.0;
    double palm_radius_ = 0.0;
    double palm_vx_ = 0.0;
    double palm_vy_ = 0.0;
    BranchTrackSignatureV8 branch_{};
};

inline bool confidence_passes_v8(const std::string& confidence) {
    return confidence == "MEDIUM" || confidence == "HIGH";
}

inline bool final_identity_stereo_gate_v8(
    const std::string& identity_confidence,
    const std::string& stereo_confidence) {

    return confidence_passes_v8(identity_confidence) &&
           confidence_passes_v8(stereo_confidence);
}

} // namespace touchplus::tracking
