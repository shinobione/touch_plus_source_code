#pragma once

// Phase 2B.10C counterfactual promotion gate.
//
// This helper is diagnostic only. It may report that B would have been selected,
// but it never returns a metric fingertip and must never mutate the accepted A
// path, smoothing, surface state, contact state, or runtime/OS output.

#include <cmath>
#include <cstddef>
#include <limits>
#include <string>

namespace touchplus::tracking {

constexpr double kPromotionMaxShiftPxV10C = 18.0;
constexpr double kPromotionMaxMetricDeltaMmV10C = 18.0;
constexpr double kPromotionMaxHeightDeltaMmV10C = 12.0;

enum class PromotionDecisionV10C {
    KeepA,
    WouldSelectB
};

enum class PromotionReasonV10C : std::size_t {
    IdentityUnknown,
    IdentityStale,
    IdentityNotCurrent,
    RefinerInward,
    RefinerRejected,
    BOnlyIneligible,
    AInvalid,
    BInvalid,
    InvalidEvidence,
    EvidenceNotStrictlyBetter,
    Invalid2DDelta,
    Excessive2DDelta,
    NonFiniteMetricDelta,
    ExcessiveMetricDelta,
    StrictEvidenceGain,
    Count
};

inline const char* promotion_decision_name_v10c(PromotionDecisionV10C decision) {
    return decision == PromotionDecisionV10C::WouldSelectB
        ? "WOULD_SELECT_B"
        : "KEEP_A";
}

inline const char* promotion_reason_name_v10c(PromotionReasonV10C reason) {
    switch (reason) {
        case PromotionReasonV10C::IdentityUnknown: return "IDENTITY_UNKNOWN";
        case PromotionReasonV10C::IdentityStale: return "IDENTITY_STALE";
        case PromotionReasonV10C::IdentityNotCurrent: return "IDENTITY_NOT_CURRENT";
        case PromotionReasonV10C::RefinerInward: return "REFINER_INWARD";
        case PromotionReasonV10C::RefinerRejected: return "REFINER_REJECTED";
        case PromotionReasonV10C::BOnlyIneligible: return "B_ONLY_INELIGIBLE";
        case PromotionReasonV10C::AInvalid: return "A_INVALID";
        case PromotionReasonV10C::BInvalid: return "B_INVALID";
        case PromotionReasonV10C::InvalidEvidence: return "INVALID_EVIDENCE";
        case PromotionReasonV10C::EvidenceNotStrictlyBetter: return "EVIDENCE_NOT_STRICTLY_BETTER";
        case PromotionReasonV10C::Invalid2DDelta: return "INVALID_2D_DELTA";
        case PromotionReasonV10C::Excessive2DDelta: return "EXCESSIVE_2D_DELTA";
        case PromotionReasonV10C::NonFiniteMetricDelta: return "NONFINITE_METRIC_DELTA";
        case PromotionReasonV10C::ExcessiveMetricDelta: return "EXCESSIVE_METRIC_DELTA";
        case PromotionReasonV10C::StrictEvidenceGain: return "STRICT_EVIDENCE_GAIN";
        default: return "UNKNOWN_REASON";
    }
}

struct PromotionGateInputV10C {
    bool identity_accepted = false;
    bool identity_current = false;
    bool identity_stale = false;
    bool refiner_accepted = false;
    bool refiner_inward = false;
    bool a_valid = false;
    bool b_valid = false;
    int a_pixel_x = -1;
    int a_pixel_y = -1;
    int b_pixel_x = -1;
    int b_pixel_y = -1;
    std::string a_stereo_confidence = "NOT_RUN";
    std::string b_stereo_confidence = "NOT_RUN";
    int a_support = 0;
    int b_support = 0;
    double a_x_mm = std::numeric_limits<double>::quiet_NaN();
    double a_y_mm = std::numeric_limits<double>::quiet_NaN();
    double a_h_mm = std::numeric_limits<double>::quiet_NaN();
    double b_x_mm = std::numeric_limits<double>::quiet_NaN();
    double b_y_mm = std::numeric_limits<double>::quiet_NaN();
    double b_h_mm = std::numeric_limits<double>::quiet_NaN();
};

struct PromotionGateResultV10C {
    PromotionDecisionV10C decision = PromotionDecisionV10C::KeepA;
    PromotionReasonV10C reason = PromotionReasonV10C::IdentityUnknown;
    double shift_2d_px = std::numeric_limits<double>::quiet_NaN();
    double delta_h_mm = std::numeric_limits<double>::quiet_NaN();
    double delta_xyz_mm = std::numeric_limits<double>::quiet_NaN();
};

inline int promotion_confidence_rank_v10c(const std::string& confidence) {
    if (confidence == "HIGH") return 2;
    if (confidence == "MEDIUM") return 1;
    return 0;
}

inline PromotionGateResultV10C evaluate_promotion_gate_v10c(
    const PromotionGateInputV10C& input) {

    PromotionGateResultV10C out;
    auto keep = [&](PromotionReasonV10C reason) {
        out.decision = PromotionDecisionV10C::KeepA;
        out.reason = reason;
        return out;
    };

    if (input.identity_stale) return keep(PromotionReasonV10C::IdentityStale);
    if (!input.identity_accepted) return keep(PromotionReasonV10C::IdentityUnknown);
    if (!input.identity_current) return keep(PromotionReasonV10C::IdentityNotCurrent);
    if (input.refiner_inward) return keep(PromotionReasonV10C::RefinerInward);
    if (!input.refiner_accepted) return keep(PromotionReasonV10C::RefinerRejected);

    // B-only evidence remains diagnostic and can never become a counterfactual
    // selection in this slice.
    if (!input.a_valid && input.b_valid) return keep(PromotionReasonV10C::BOnlyIneligible);
    if (!input.a_valid) return keep(PromotionReasonV10C::AInvalid);
    if (!input.b_valid) return keep(PromotionReasonV10C::BInvalid);

    if (input.a_pixel_x < 0 || input.a_pixel_y < 0 ||
        input.b_pixel_x < 0 || input.b_pixel_y < 0) {
        return keep(PromotionReasonV10C::Invalid2DDelta);
    }
    out.shift_2d_px = std::hypot(
        static_cast<double>(input.b_pixel_x - input.a_pixel_x),
        static_cast<double>(input.b_pixel_y - input.a_pixel_y));
    if (!std::isfinite(out.shift_2d_px)) {
        return keep(PromotionReasonV10C::Invalid2DDelta);
    }
    if (out.shift_2d_px > kPromotionMaxShiftPxV10C) {
        return keep(PromotionReasonV10C::Excessive2DDelta);
    }

    const double dx = input.b_x_mm - input.a_x_mm;
    const double dy = input.b_y_mm - input.a_y_mm;
    out.delta_h_mm = input.b_h_mm - input.a_h_mm;
    out.delta_xyz_mm = std::sqrt(
        dx * dx + dy * dy + out.delta_h_mm * out.delta_h_mm);
    if (!std::isfinite(dx) || !std::isfinite(dy) ||
        !std::isfinite(out.delta_h_mm) || !std::isfinite(out.delta_xyz_mm)) {
        return keep(PromotionReasonV10C::NonFiniteMetricDelta);
    }
    if (out.delta_xyz_mm > kPromotionMaxMetricDeltaMmV10C ||
        std::abs(out.delta_h_mm) > kPromotionMaxHeightDeltaMmV10C) {
        return keep(PromotionReasonV10C::ExcessiveMetricDelta);
    }

    const int a_rank = promotion_confidence_rank_v10c(input.a_stereo_confidence);
    const int b_rank = promotion_confidence_rank_v10c(input.b_stereo_confidence);
    if (a_rank == 0 || b_rank == 0 || input.a_support < 0 || input.b_support < 0) {
        return keep(PromotionReasonV10C::InvalidEvidence);
    }

    // Conservative Pareto rule: B may improve confidence or support, but it
    // may degrade neither, and at least one dimension must improve strictly.
    const bool confidence_not_worse = b_rank >= a_rank;
    const bool support_not_worse = input.b_support >= input.a_support;
    const bool strictly_better = b_rank > a_rank || input.b_support > input.a_support;
    if (!confidence_not_worse || !support_not_worse || !strictly_better) {
        return keep(PromotionReasonV10C::EvidenceNotStrictlyBetter);
    }

    out.decision = PromotionDecisionV10C::WouldSelectB;
    out.reason = PromotionReasonV10C::StrictEvidenceGain;
    return out;
}

} // namespace touchplus::tracking
