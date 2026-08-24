#pragma once

// Phase 2C.1D diagnostic-only replay for the existing LEFT->RIGHT matcher.
//
// This file does NOT alter search_left_to_right(), mutually_consistent_match(),
// disparity limits, patch size, NCC thresholds, calibration, Q, surface frame,
// identity/fusion, smoothing or contact semantics. It only replays the exact
// forward-stage decision path after an authoritative forward failure so the
// runtime can classify why that probe died.

#include "depth_point_robust.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace touchplus::tracking {

enum class ForwardFailureReasonV2C1D {
    Accepted,
    PatchOutOfBounds,
    TextureLow,
    WindowEmpty,
    NoValidCandidate,
    CorrelationLow,
    UniquenessFail
};

struct ForwardMatchDiagnosticV2C1D {
    ForwardFailureReasonV2C1D reason = ForwardFailureReasonV2C1D::NoValidCandidate;
    double reference_variance = std::numeric_limits<double>::quiet_NaN();
    double best_ncc = std::numeric_limits<double>::quiet_NaN();
    double second_ncc = std::numeric_limits<double>::quiet_NaN();
    double best_minus_second = std::numeric_limits<double>::quiet_NaN();
    double winning_disparity = std::numeric_limits<double>::quiet_NaN();
    int min_disparity = 0;
    int max_disparity = -1;
};

inline ForwardMatchDiagnosticV2C1D diagnose_left_to_right_v2c1d(
    const std::vector<std::uint8_t>& left,
    const std::vector<std::uint8_t>& right,
    int x,
    int y,
    int min_disp,
    int max_disp) {

    using namespace touchplus::depth::robust_point_detail;

    ForwardMatchDiagnosticV2C1D out;
    double mean = 0.0;
    double variance = 0.0;
    if (!patch_stats(left, x, y, mean, variance)) {
        out.reason = ForwardFailureReasonV2C1D::PatchOutOfBounds;
        return out;
    }
    out.reference_variance = variance;
    if (variance < kMinTextureVariance) {
        out.reason = ForwardFailureReasonV2C1D::TextureLow;
        return out;
    }

    min_disp = std::max(kMinDisparity, min_disp);
    max_disp = std::min({kMaxDisparity, max_disp, x - kRadius - 1});
    out.min_disparity = min_disp;
    out.max_disparity = max_disp;
    if (max_disp < min_disp) {
        out.reason = ForwardFailureReasonV2C1D::WindowEmpty;
        return out;
    }

    std::vector<double> scores(static_cast<std::size_t>(max_disp + 1), -1.0);
    int best_d = -1;
    double best = -1.0;
    for (int d = min_disp; d <= max_disp; ++d) {
        const int xr = x - d;
        if (xr < kRadius) continue;
        const double score = zero_mean_ncc(left, right, x, xr, y, mean, variance);
        scores[static_cast<std::size_t>(d)] = score;
        if (score > best) {
            best = score;
            best_d = d;
        }
    }

    if (best_d < 0) {
        out.reason = ForwardFailureReasonV2C1D::NoValidCandidate;
        return out;
    }

    out.best_ncc = best;
    out.winning_disparity = static_cast<double>(best_d);
    if (best < kMinCorrelation) {
        out.reason = ForwardFailureReasonV2C1D::CorrelationLow;
        return out;
    }

    double second = -1.0;
    for (int d = min_disp; d <= max_disp; ++d) {
        if (std::abs(d - best_d) <= 2) continue;
        second = std::max(second, scores[static_cast<std::size_t>(d)]);
    }
    out.second_ncc = second;
    if (second > -0.5) out.best_minus_second = best - second;

    if (second > -0.5 && best - second < kMinCorrelationGap) {
        out.reason = ForwardFailureReasonV2C1D::UniquenessFail;
        return out;
    }

    double disparity = static_cast<double>(best_d);
    if (best_d > min_disp && best_d < max_disp) {
        const double s0 = scores[static_cast<std::size_t>(best_d - 1)];
        const double s1 = scores[static_cast<std::size_t>(best_d)];
        const double s2 = scores[static_cast<std::size_t>(best_d + 1)];
        if (s0 > -0.5 && s2 > -0.5) {
            const double denom = s0 - 2.0 * s1 + s2;
            if (std::abs(denom) > 1e-9) {
                disparity += std::clamp(0.5 * (s0 - s2) / denom, -1.0, 1.0);
            }
        }
    }

    out.winning_disparity = disparity;
    out.reason = ForwardFailureReasonV2C1D::Accepted;
    return out;
}

} // namespace touchplus::tracking
