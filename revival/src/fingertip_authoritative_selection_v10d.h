#pragma once

// Phase 2B.10D authoritative source selection.
//
// This layer does not change the physically validated 2B.10C gate. It only
// promotes the complete B sample when explicit runtime opt-in is enabled and
// that existing gate reports WOULD_SELECT_B. Pixel and metric fields are copied
// as one unit so mixed-source output cannot be constructed here.

#include "fingertip_promotion_gate_v10c.h"

#include <cmath>
#include <cstdint>
#include <string>

namespace touchplus::tracking {

enum class AuthoritativeSourceV10D {
    A,
    B
};

inline const char* authoritative_source_name_v10d(
    AuthoritativeSourceV10D source) {
    return source == AuthoritativeSourceV10D::B ? "B" : "A";
}

struct AuthoritativeSampleV10D {
    bool valid = false;
    int pixel_x = -1;
    int pixel_y = -1;
    double x_mm = 0.0;
    double y_mm = 0.0;
    double h_mm = 0.0;
    std::string stereo_confidence = "NOT_RUN";
    int support = 0;
};

inline bool authoritative_sample_finite_v10d(
    const AuthoritativeSampleV10D& sample) {
    return sample.valid && sample.pixel_x >= 0 && sample.pixel_y >= 0 &&
        std::isfinite(sample.x_mm) && std::isfinite(sample.y_mm) &&
        std::isfinite(sample.h_mm);
}

struct AuthoritativeSelectionV10D {
    AuthoritativeSourceV10D source = AuthoritativeSourceV10D::A;
    AuthoritativeSampleV10D sample{};
    std::string reason = "GATE_NOT_EVALUATED";
};

inline AuthoritativeSelectionV10D select_authoritative_sample_v10d(
    bool promotion_enabled,
    const PromotionGateResultV10C& gate,
    const AuthoritativeSampleV10D& a,
    const AuthoritativeSampleV10D& b) {

    AuthoritativeSelectionV10D out;
    out.sample = a;

    if (!promotion_enabled) {
        out.reason = "PROMOTION_DISABLED";
        return out;
    }
    if (gate.decision != PromotionDecisionV10C::WouldSelectB) {
        out.reason = promotion_reason_name_v10c(gate.reason);
        return out;
    }

    // The unchanged 2B.10C gate already establishes these conditions. Keep a
    // final defensive coherence check at the copy boundary without adding or
    // changing any numeric threshold.
    if (!authoritative_sample_finite_v10d(a) ||
        !authoritative_sample_finite_v10d(b)) {
        out.reason = "GATE_SAMPLE_INCOHERENT";
        return out;
    }

    out.source = AuthoritativeSourceV10D::B;
    out.sample = b;
    out.reason = promotion_reason_name_v10c(gate.reason);
    return out;
}

struct PromotionSmootherV10D {
    bool initialized = false;
    double x_mm = 0.0;
    double y_mm = 0.0;
    double h_mm = 0.0;
    std::uint64_t consumed_samples = 0;
};

inline bool consume_selected_sample_v10d(
    bool promotion_enabled,
    const AuthoritativeSampleV10D& sample,
    PromotionSmootherV10D& smoother) {

    if (!promotion_enabled || !authoritative_sample_finite_v10d(sample)) {
        return false;
    }

    constexpr double alpha = 0.32;
    if (smoother.initialized) {
        smoother.x_mm = smoother.x_mm * (1.0 - alpha) + sample.x_mm * alpha;
        smoother.y_mm = smoother.y_mm * (1.0 - alpha) + sample.y_mm * alpha;
        smoother.h_mm = smoother.h_mm * (1.0 - alpha) + sample.h_mm * alpha;
    } else {
        smoother.x_mm = sample.x_mm;
        smoother.y_mm = sample.y_mm;
        smoother.h_mm = sample.h_mm;
        smoother.initialized = true;
    }
    ++smoother.consumed_samples;
    return true;
}

struct PromotionSelectionStatsV10D {
    std::uint64_t selected_a = 0;
    std::uint64_t selected_b = 0;
    std::uint64_t source_switches = 0;
    bool have_previous = false;
    AuthoritativeSourceV10D previous = AuthoritativeSourceV10D::A;
};

inline void record_authoritative_selection_v10d(
    AuthoritativeSourceV10D source,
    PromotionSelectionStatsV10D& stats) {
    if (source == AuthoritativeSourceV10D::B) ++stats.selected_b;
    else ++stats.selected_a;
    if (stats.have_previous && stats.previous != source) {
        ++stats.source_switches;
    }
    stats.previous = source;
    stats.have_previous = true;
}

inline constexpr bool kOsInjectionEnabledV10D = false;

} // namespace touchplus::tracking
