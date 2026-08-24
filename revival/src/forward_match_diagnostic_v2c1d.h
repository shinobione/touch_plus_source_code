#pragma once

// Phase 2C.1D diagnostic-only replay for the existing LEFT->RIGHT matcher.
// Phase 2C.1E extends that replay counterfactually for authoritative
// TextureLow rejects: it keeps the authoritative rejection reason unchanged,
// but continues the shadow NCC/uniqueness calculation so telemetry can show
// whether a lower texture floor would even have a plausible forward match.
// Phase 2C.1E.1 makes the exported NCC/gap/disparity fields texture-shadow-only
// so the existing [FWD] aggregate cannot be contaminated by unrelated
// CorrelationLow/UniquenessFail probes from the same frame.
// Phase 2C.1F goes one step further: those exported fields are now populated
// only when the low-texture shadow also passes a counterfactual reverse search
// and the existing left/right disparity tolerance. This remains diagnostic-only.
//
// This file does NOT alter search_left_to_right(), search_right_to_left(),
// mutually_consistent_match(), disparity limits, patch size, authoritative NCC
// thresholds, calibration, Q, surface frame, identity/fusion, smoothing or
// contact semantics. Everything in this file is shadow telemetry only and
// cannot publish an authoritative match.

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

    // 2C.1F: exported match-quality values are populated only for an
    // authoritative TextureLow reject which counterfactually passes forward
    // correlation + uniqueness AND reverse/LR consistency. Existing tracker
    // aggregation therefore becomes a direct "full shadow mutual survived"
    // signal without changing the runtime's authoritative decision.
    double best_ncc = std::numeric_limits<double>::quiet_NaN();
    double second_ncc = std::numeric_limits<double>::quiet_NaN();
    double best_minus_second = std::numeric_limits<double>::quiet_NaN();
    double winning_disparity = std::numeric_limits<double>::quiet_NaN();
    int min_disparity = 0;
    int max_disparity = -1;

    bool authoritative_texture_reject = false;
    bool shadow_search_reached_candidate = false;
    bool shadow_passes_correlation = false;
    bool shadow_passes_uniqueness = false;
    bool shadow_reverse_valid = false;
    bool shadow_passes_lr_consistency = false;
    double shadow_reverse_disparity = std::numeric_limits<double>::quiet_NaN();
    double shadow_lr_delta = std::numeric_limits<double>::quiet_NaN();
};

struct ShadowReverseMatchV2C1F {
    bool valid = false;
    double disparity = std::numeric_limits<double>::quiet_NaN();
};

// Counterfactual reverse search used only after an authoritative LEFT->RIGHT
// TextureLow rejection has already occurred and the shadow forward match has
// passed the unchanged correlation/uniqueness gates. The reference texture
// early-return is bypassed, while zero_mean_ncc() and all NCC/uniqueness gates
// remain the same. Nothing from this helper can feed authoritative tracking.
inline ShadowReverseMatchV2C1F diagnose_right_to_left_low_texture_v2c1f(
    const std::vector<std::uint8_t>& right,
    const std::vector<std::uint8_t>& left,
    int x,
    int y,
    int min_disp,
    int max_disp) {

    using namespace touchplus::depth::robust_point_detail;

    ShadowReverseMatchV2C1F out;
    double mean = 0.0;
    double variance = 0.0;
    if (!patch_stats(right, x, y, mean, variance) || variance <= 1e-9) {
        return out;
    }

    min_disp = std::max(kMinDisparity, min_disp);
    max_disp = std::min(kMaxDisparity, max_disp);
    if (max_disp < min_disp || x + min_disp + kRadius >= touchplus::depth::kEyeWidth) {
        return out;
    }
    max_disp = std::min(max_disp, touchplus::depth::kEyeWidth - kRadius - 1 - x);
    if (max_disp < min_disp) {
        return out;
    }

    std::vector<double> scores(static_cast<std::size_t>(max_disp + 1), -1.0);
    int best_d = -1;
    double best = -1.0;
    for (int d = min_disp; d <= max_disp; ++d) {
        const int xl = x + d;
        const double score = zero_mean_ncc(right, left, x, xl, y, mean, variance);
        scores[static_cast<std::size_t>(d)] = score;
        if (score > best) {
            best = score;
            best_d = d;
        }
    }
    if (best_d < 0 || best < kMinCorrelation) {
        return out;
    }

    double second = -1.0;
    for (int d = min_disp; d <= max_disp; ++d) {
        if (std::abs(d - best_d) <= 2) continue;
        second = std::max(second, scores[static_cast<std::size_t>(d)]);
    }
    if (second > -0.5 && best - second < kMinCorrelationGap) {
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

    out.valid = true;
    out.disparity = disparity;
    return out;
}

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

    // Preserve the exact authoritative classification, but do not return early
    // for TextureLow. The remaining computation is counterfactual/shadow-only.
    const bool texture_rejected = variance < kMinTextureVariance;
    if (texture_rejected) {
        out.reason = ForwardFailureReasonV2C1D::TextureLow;
        out.authoritative_texture_reject = true;
    }

    min_disp = std::max(kMinDisparity, min_disp);
    max_disp = std::min({kMaxDisparity, max_disp, x - kRadius - 1});
    out.min_disparity = min_disp;
    out.max_disparity = max_disp;
    if (max_disp < min_disp) {
        if (!texture_rejected) out.reason = ForwardFailureReasonV2C1D::WindowEmpty;
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
        if (!texture_rejected) out.reason = ForwardFailureReasonV2C1D::NoValidCandidate;
        return out;
    }
    if (texture_rejected) out.shadow_search_reached_candidate = true;

    if (best < kMinCorrelation) {
        if (!texture_rejected) out.reason = ForwardFailureReasonV2C1D::CorrelationLow;
        return out;
    }
    if (texture_rejected) out.shadow_passes_correlation = true;

    double second = -1.0;
    for (int d = min_disp; d <= max_disp; ++d) {
        if (std::abs(d - best_d) <= 2) continue;
        second = std::max(second, scores[static_cast<std::size_t>(d)]);
    }

    if (second > -0.5 && best - second < kMinCorrelationGap) {
        if (!texture_rejected) out.reason = ForwardFailureReasonV2C1D::UniquenessFail;
        return out;
    }
    if (texture_rejected) out.shadow_passes_uniqueness = true;

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

    // 2C.1F: do not export a plausible-looking low-texture forward result
    // unless it also survives a counterfactual reverse search and the exact
    // existing LR tolerance. This prevents forward-only false optimism.
    if (texture_rejected) {
        const int right_x = static_cast<int>(std::lround(x - disparity));
        const int narrow_min = std::max(
            kMinDisparity, static_cast<int>(std::floor(disparity - 3.0)));
        const int narrow_max = std::min(
            kMaxDisparity, static_cast<int>(std::ceil(disparity + 3.0)));
        const auto reverse = diagnose_right_to_left_low_texture_v2c1f(
            right, left, right_x, y, narrow_min, narrow_max);
        out.shadow_reverse_valid = reverse.valid;
        if (!reverse.valid) {
            return out;
        }

        out.shadow_reverse_disparity = reverse.disparity;
        out.shadow_lr_delta = std::abs(reverse.disparity - disparity);
        if (out.shadow_lr_delta > kLeftRightTolerancePx) {
            return out;
        }

        out.shadow_passes_lr_consistency = true;
        out.best_ncc = best;
        out.second_ncc = second;
        if (second > -0.5) out.best_minus_second = best - second;
        out.winning_disparity = disparity;
        return out;
    }

    out.reason = ForwardFailureReasonV2C1D::Accepted;
    return out;
}

} // namespace touchplus::tracking
