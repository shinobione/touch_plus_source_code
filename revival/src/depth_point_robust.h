#pragma once

// Force-included only for touchplus_depth_viewer. Rename the original
// point_depth implementation while depth_math.h is parsed, then provide the
// hardened runtime implementation under the public point_depth name.
#define point_depth point_depth_legacy
#include "depth_math.h"
#undef point_depth

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <numeric>
#include <vector>

namespace touchplus::depth {
namespace robust_point_detail {

constexpr int kRadius = 4;                 // 9x9 patch
constexpr int kMinDisparity = 8;
constexpr int kMaxDisparity = 192;
constexpr double kMinTextureVariance = 90.0;
constexpr double kMinCorrelation = 0.78;
constexpr double kMinCorrelationGap = 0.055;
constexpr double kLeftRightTolerancePx = 1.25;
constexpr double kNeighborTolerancePx = 1.75;
constexpr int kMinNeighborAgreement = 3;
constexpr size_t kTemporalWindow = 7;

struct Match {
    bool valid = false;
    double disparity = 0.0;
    double score = -1.0;
    double second_score = -1.0;
    double texture_variance = 0.0;
};

inline double median(std::vector<double> values) {
    if (values.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + mid, values.end());
    double result = values[mid];
    if ((values.size() & 1U) == 0U) {
        const auto lower = std::max_element(values.begin(), values.begin() + mid);
        result = (*lower + result) * 0.5;
    }
    return result;
}

inline bool patch_stats(
    const std::vector<uint8_t>& image,
    int x,
    int y,
    double& mean,
    double& variance) {

    if (x < kRadius || x >= kEyeWidth - kRadius ||
        y < kRadius || y >= kEyeHeight - kRadius) {
        return false;
    }

    constexpr int side = kRadius * 2 + 1;
    constexpr int area = side * side;
    double sum = 0.0;
    double sum2 = 0.0;
    for (int yy = -kRadius; yy <= kRadius; ++yy) {
        const size_t row = static_cast<size_t>(y + yy) * kEyeWidth;
        for (int xx = -kRadius; xx <= kRadius; ++xx) {
            const double value = image[row + x + xx];
            sum += value;
            sum2 += value * value;
        }
    }
    mean = sum / area;
    variance = std::max(0.0, sum2 / area - mean * mean);
    return true;
}

inline double zero_mean_ncc(
    const std::vector<uint8_t>& reference,
    const std::vector<uint8_t>& candidate,
    int reference_x,
    int candidate_x,
    int y,
    double reference_mean,
    double reference_variance) {

    double candidate_mean = 0.0;
    double candidate_variance = 0.0;
    if (!patch_stats(candidate, candidate_x, y, candidate_mean, candidate_variance) ||
        candidate_variance < kMinTextureVariance * 0.35 ||
        reference_variance <= 1e-9 || candidate_variance <= 1e-9) {
        return -1.0;
    }

    constexpr int side = kRadius * 2 + 1;
    constexpr int area = side * side;
    double numerator = 0.0;
    double ref_energy = 0.0;
    double candidate_energy = 0.0;
    for (int yy = -kRadius; yy <= kRadius; ++yy) {
        const size_t row = static_cast<size_t>(y + yy) * kEyeWidth;
        for (int xx = -kRadius; xx <= kRadius; ++xx) {
            const double a = reference[row + reference_x + xx] - reference_mean;
            const double b = candidate[row + candidate_x + xx] - candidate_mean;
            numerator += a * b;
            ref_energy += a * a;
            candidate_energy += b * b;
        }
    }

    const double denom = std::sqrt(ref_energy * candidate_energy);
    if (denom <= static_cast<double>(area) * 1e-6) {
        return -1.0;
    }
    return numerator / denom;
}

inline Match search_left_to_right(
    const std::vector<uint8_t>& left,
    const std::vector<uint8_t>& right,
    int x,
    int y,
    int min_disp,
    int max_disp) {

    Match result;
    double mean = 0.0;
    double variance = 0.0;
    if (!patch_stats(left, x, y, mean, variance) || variance < kMinTextureVariance) {
        return result;
    }
    result.texture_variance = variance;

    min_disp = std::max(kMinDisparity, min_disp);
    max_disp = std::min({kMaxDisparity, max_disp, x - kRadius - 1});
    if (max_disp < min_disp) {
        return result;
    }

    std::vector<double> scores(static_cast<size_t>(max_disp + 1), -1.0);
    int best_d = -1;
    double best = -1.0;
    for (int d = min_disp; d <= max_disp; ++d) {
        const int xr = x - d;
        if (xr < kRadius) {
            continue;
        }
        const double score = zero_mean_ncc(left, right, x, xr, y, mean, variance);
        scores[static_cast<size_t>(d)] = score;
        if (score > best) {
            best = score;
            best_d = d;
        }
    }
    if (best_d < 0 || best < kMinCorrelation) {
        return result;
    }

    double second = -1.0;
    for (int d = min_disp; d <= max_disp; ++d) {
        if (std::abs(d - best_d) <= 2) {
            continue;
        }
        second = std::max(second, scores[static_cast<size_t>(d)]);
    }
    if (second > -0.5 && best - second < kMinCorrelationGap) {
        return result;
    }

    double disparity = static_cast<double>(best_d);
    if (best_d > min_disp && best_d < max_disp) {
        const double s0 = scores[static_cast<size_t>(best_d - 1)];
        const double s1 = scores[static_cast<size_t>(best_d)];
        const double s2 = scores[static_cast<size_t>(best_d + 1)];
        if (s0 > -0.5 && s2 > -0.5) {
            const double denom = s0 - 2.0 * s1 + s2;
            if (std::abs(denom) > 1e-9) {
                const double offset = std::clamp(0.5 * (s0 - s2) / denom, -1.0, 1.0);
                disparity += offset;
            }
        }
    }

    result.valid = true;
    result.disparity = disparity;
    result.score = best;
    result.second_score = second;
    return result;
}

inline Match search_right_to_left(
    const std::vector<uint8_t>& right,
    const std::vector<uint8_t>& left,
    int x,
    int y,
    int min_disp,
    int max_disp) {

    Match result;
    double mean = 0.0;
    double variance = 0.0;
    if (!patch_stats(right, x, y, mean, variance) || variance < kMinTextureVariance) {
        return result;
    }
    result.texture_variance = variance;

    min_disp = std::max(kMinDisparity, min_disp);
    max_disp = std::min(kMaxDisparity, max_disp);
    if (max_disp < min_disp || x + min_disp + kRadius >= kEyeWidth) {
        return result;
    }
    max_disp = std::min(max_disp, kEyeWidth - kRadius - 1 - x);

    std::vector<double> scores(static_cast<size_t>(max_disp + 1), -1.0);
    int best_d = -1;
    double best = -1.0;
    for (int d = min_disp; d <= max_disp; ++d) {
        const int xl = x + d;
        const double score = zero_mean_ncc(right, left, x, xl, y, mean, variance);
        scores[static_cast<size_t>(d)] = score;
        if (score > best) {
            best = score;
            best_d = d;
        }
    }
    if (best_d < 0 || best < kMinCorrelation) {
        return result;
    }

    double second = -1.0;
    for (int d = min_disp; d <= max_disp; ++d) {
        if (std::abs(d - best_d) <= 2) {
            continue;
        }
        second = std::max(second, scores[static_cast<size_t>(d)]);
    }
    if (second > -0.5 && best - second < kMinCorrelationGap) {
        return result;
    }

    double disparity = static_cast<double>(best_d);
    if (best_d > min_disp && best_d < max_disp) {
        const double s0 = scores[static_cast<size_t>(best_d - 1)];
        const double s1 = scores[static_cast<size_t>(best_d)];
        const double s2 = scores[static_cast<size_t>(best_d + 1)];
        if (s0 > -0.5 && s2 > -0.5) {
            const double denom = s0 - 2.0 * s1 + s2;
            if (std::abs(denom) > 1e-9) {
                const double offset = std::clamp(0.5 * (s0 - s2) / denom, -1.0, 1.0);
                disparity += offset;
            }
        }
    }

    result.valid = true;
    result.disparity = disparity;
    result.score = best;
    result.second_score = second;
    return result;
}

inline Match mutually_consistent_match(
    const std::vector<uint8_t>& left,
    const std::vector<uint8_t>& right,
    int x,
    int y,
    int min_disp = kMinDisparity,
    int max_disp = kMaxDisparity) {

    Match forward = search_left_to_right(left, right, x, y, min_disp, max_disp);
    if (!forward.valid) {
        return {};
    }

    const int right_x = static_cast<int>(std::lround(x - forward.disparity));
    const int narrow_min = std::max(kMinDisparity, static_cast<int>(std::floor(forward.disparity - 3.0)));
    const int narrow_max = std::min(kMaxDisparity, static_cast<int>(std::ceil(forward.disparity + 3.0)));
    Match reverse = search_right_to_left(right, left, right_x, y, narrow_min, narrow_max);
    if (!reverse.valid || std::abs(reverse.disparity - forward.disparity) > kLeftRightTolerancePx) {
        return {};
    }
    return forward;
}

struct TemporalState {
    int x = -1000;
    int y = -1000;
    std::deque<double> disparities;
    std::deque<double> z_values;
};

inline TemporalState& temporal_state() {
    static thread_local TemporalState state;
    return state;
}

inline void push_bounded(std::deque<double>& values, double value) {
    values.push_back(value);
    while (values.size() > kTemporalWindow) {
        values.pop_front();
    }
}

} // namespace robust_point_detail

inline PointDepth point_depth(
    const Calibration& c,
    const std::vector<uint8_t>& left,
    const std::vector<uint8_t>& right,
    int x,
    int y) {

    using namespace robust_point_detail;
    PointDepth result;

    if (left.size() < static_cast<size_t>(kEyeWidth) * kEyeHeight ||
        right.size() < static_cast<size_t>(kEyeWidth) * kEyeHeight ||
        x < kRadius + kMinDisparity || x >= kEyeWidth - kRadius ||
        y < kRadius || y >= kEyeHeight - kRadius) {
        return result;
    }

    const Match center = mutually_consistent_match(left, right, x, y);
    if (!center.valid) {
        return result;
    }

    // A single good-looking patch can still lock onto repeated print or an
    // edge. Require local disparity consensus around the cursor before Q.
    constexpr std::array<std::array<int, 2>, 8> offsets{{
        {{-5, 0}}, {{5, 0}}, {{0, -5}}, {{0, 5}},
        {{-4, -4}}, {{4, -4}}, {{-4, 4}}, {{4, 4}}
    }};
    int agreeing = 1; // center itself
    std::vector<double> local_disparities{center.disparity};
    const int local_min = std::max(kMinDisparity, static_cast<int>(std::floor(center.disparity - 4.0)));
    const int local_max = std::min(kMaxDisparity, static_cast<int>(std::ceil(center.disparity + 4.0)));
    for (const auto& offset : offsets) {
        const int nx = x + offset[0];
        const int ny = y + offset[1];
        if (nx < kRadius + kMinDisparity || nx >= kEyeWidth - kRadius ||
            ny < kRadius || ny >= kEyeHeight - kRadius) {
            continue;
        }
        const Match neighbor = mutually_consistent_match(
            left, right, nx, ny, local_min, local_max);
        if (neighbor.valid &&
            std::abs(neighbor.disparity - center.disparity) <= kNeighborTolerancePx) {
            ++agreeing;
            local_disparities.push_back(neighbor.disparity);
        }
    }
    if (agreeing < kMinNeighborAgreement) {
        return result;
    }

    double disparity = median(local_disparities);
    double z = camera_z_from_q(c, static_cast<double>(x), static_cast<double>(y), disparity);
    if (!std::isfinite(z) || z < 140.0 || z > 3000.0) {
        return result;
    }

    // Short temporal median kills one-frame disparity catastrophes while
    // retaining motion responsiveness. Moving the cursor significantly resets
    // the history so values from unrelated surfaces never bleed together.
    TemporalState& state = temporal_state();
    if (std::abs(x - state.x) > 3 || std::abs(y - state.y) > 3) {
        state.disparities.clear();
        state.z_values.clear();
    }
    state.x = x;
    state.y = y;

    if (state.z_values.size() >= 3) {
        std::vector<double> history(state.z_values.begin(), state.z_values.end());
        const double z_med = median(history);
        std::vector<double> deviations;
        deviations.reserve(history.size());
        for (const double value : history) {
            deviations.push_back(std::abs(value - z_med));
        }
        const double mad = median(deviations);
        const double gate = std::max(22.0, 5.0 * mad + 8.0);
        if (std::abs(z - z_med) > gate) {
            return result;
        }
    }

    push_bounded(state.disparities, disparity);
    push_bounded(state.z_values, z);
    disparity = median(std::vector<double>(state.disparities.begin(), state.disparities.end()));
    z = camera_z_from_q(c, static_cast<double>(x), static_cast<double>(y), disparity);
    if (!std::isfinite(z) || z < 140.0 || z > 3000.0) {
        return result;
    }

    result.valid = true;
    result.disparity_px = disparity;
    result.z_mm = z;
    // Preserve the existing field but turn it into a simple confidence-like
    // diagnostic: lower is better, zero is perfect NCC.
    result.average_cost = (1.0 - center.score) * 255.0;
    return result;
}

} // namespace touchplus::depth
