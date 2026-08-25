#pragma once

// Phase 2C.1H shadow-only contact proxy.
//
// This logic consumes the diagnostic raw-dense pre-support signal introduced in
// 2C.1G/2C.1G.1 plus provisional 2D fingertip motion. It never feeds the real
// Phase 2C contact state machine. The goal is to test whether a conservative
// combination of robust low-H distribution + recent approach + terminal 2D/H
// plateau can separate physical contact from low hover without pretending the
// stereo system measures the true skin/table contact patch at H ~= 0.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace touchplus::contact_shadow {

enum class TargetSourceV2C1H {
    None,
    Fused,
    Anatomy,
    Geometry
};

inline const char* target_source_name_v2c1h(TargetSourceV2C1H source) {
    switch (source) {
        case TargetSourceV2C1H::Fused: return "FUSED";
        case TargetSourceV2C1H::Anatomy: return "ANATOMY";
        case TargetSourceV2C1H::Geometry: return "GEOMETRY";
        default: return "NONE";
    }
}

enum class ShadowContactEventV2C1H {
    None,
    WouldDown,
    WouldUp
};

inline const char* shadow_contact_event_name_v2c1h(
    ShadowContactEventV2C1H event) {
    switch (event) {
        case ShadowContactEventV2C1H::WouldDown: return "WOULD_DOWN";
        case ShadowContactEventV2C1H::WouldUp: return "WOULD_UP";
        default: return "NONE";
    }
}

struct ShadowContactInputV2C1H {
    bool sample_valid = false;
    std::uint32_t frame = 0;
    TargetSourceV2C1H source = TargetSourceV2C1H::None;
    int target_x = -1;
    int target_y = -1;
    int raw_dense_count = 0;
    double h_p25_mm = std::numeric_limits<double>::quiet_NaN();
    double h_median_mm = std::numeric_limits<double>::quiet_NaN();
};

struct ShadowContactOutputV2C1H {
    bool would_contact = false;
    ShadowContactEventV2C1H event = ShadowContactEventV2C1H::None;
    int candidate_count = 0;
    int release_count = 0;
    bool trusted_target = false;
    bool dense_enough = false;
    bool low_band = false;
    bool approach_seen = false;
    bool plateau = false;
    double distribution_spread_mm = std::numeric_limits<double>::quiet_NaN();
    double approach_drop_mm = std::numeric_limits<double>::quiet_NaN();
    double plateau_h_span_mm = std::numeric_limits<double>::quiet_NaN();
    double plateau_motion_px = std::numeric_limits<double>::quiet_NaN();
    const char* reason = "NO_SAMPLE";
};

constexpr int kMinDenseCountV2C1H = 8;
constexpr int kHoldMinDenseCountV2C1H = 6;
constexpr double kCandidateMedianMinHmmV2C1H = 8.0;
constexpr double kCandidateMedianMaxHmmV2C1H = 32.0;
constexpr double kCandidateP25MinHmmV2C1H = 4.0;
constexpr double kCandidateP25MaxHmmV2C1H = 28.0;
constexpr double kCandidateMaxSpreadHmmV2C1H = 14.0;
constexpr double kMinRecentApproachDropHmmV2C1H = 14.0;
constexpr int kPlateauSamplesV2C1H = 6;
constexpr std::uint32_t kPlateauMaxFrameSpanV2C1H = 10;
constexpr double kPlateauMaxHmmSpanV2C1H = 7.5;
constexpr double kPlateauMaxMotionPxV2C1H = 14.0;
constexpr int kCandidateFramesV2C1H = 3;
constexpr double kHoldMedianMaxHmmV2C1H = 36.0;
constexpr double kHoldP25MinHmmV2C1H = 0.0;
constexpr double kHoldMaxSpreadHmmV2C1H = 18.0;
constexpr int kReleaseFramesV2C1H = 2;
constexpr std::size_t kHistoryLimitV2C1H = 24;

class ShadowContactProxyV2C1H {
public:
    ShadowContactOutputV2C1H update(const ShadowContactInputV2C1H& input) {
        ShadowContactOutputV2C1H out;
        out.would_contact = latched_;
        out.candidate_count = candidate_count_;
        out.release_count = release_count_;

        const bool finite_h =
            std::isfinite(input.h_p25_mm) && std::isfinite(input.h_median_mm);
        out.trusted_target =
            input.source == TargetSourceV2C1H::Fused ||
            input.source == TargetSourceV2C1H::Anatomy;
        out.dense_enough = input.raw_dense_count >= kMinDenseCountV2C1H;

        if (!input.sample_valid || !finite_h) {
            return handle_unusable(out, "NO_SAMPLE", false);
        }
        if (!out.trusted_target) {
            return handle_unusable(out, "GEOMETRY_ONLY", true);
        }

        const double spread = input.h_median_mm - input.h_p25_mm;
        out.distribution_spread_mm = spread;

        append_history(input);
        compute_history_features(input, out);

        out.low_band =
            out.dense_enough &&
            input.h_median_mm >= kCandidateMedianMinHmmV2C1H &&
            input.h_median_mm <= kCandidateMedianMaxHmmV2C1H &&
            input.h_p25_mm >= kCandidateP25MinHmmV2C1H &&
            input.h_p25_mm <= kCandidateP25MaxHmmV2C1H &&
            spread >= 0.0 && spread <= kCandidateMaxSpreadHmmV2C1H;

        if (latched_) {
            const bool hold_band =
                input.raw_dense_count >= kHoldMinDenseCountV2C1H &&
                input.h_median_mm <= kHoldMedianMaxHmmV2C1H &&
                input.h_p25_mm >= kHoldP25MinHmmV2C1H &&
                spread >= 0.0 && spread <= kHoldMaxSpreadHmmV2C1H;
            if (hold_band) {
                release_count_ = 0;
                out.would_contact = true;
                out.candidate_count = candidate_count_;
                out.release_count = release_count_;
                out.reason = "WOULD_HELD";
                return out;
            }
            ++release_count_;
            if (release_count_ >= kReleaseFramesV2C1H) {
                reset_after_release();
                out.would_contact = false;
                out.event = ShadowContactEventV2C1H::WouldUp;
                out.candidate_count = 0;
                out.release_count = 0;
                out.reason = "WOULD_RELEASE";
                return out;
            }
            out.would_contact = true;
            out.release_count = release_count_;
            out.reason = "RELEASE_PENDING";
            return out;
        }

        if (!out.dense_enough) {
            candidate_count_ = 0;
            out.reason = "DENSE_TOO_SPARSE";
        } else if (input.h_p25_mm < kCandidateP25MinHmmV2C1H || spread < 0.0 ||
                   spread > kCandidateMaxSpreadHmmV2C1H) {
            candidate_count_ = 0;
            out.reason = "DISTRIBUTION_CONTAMINATED";
        } else if (!out.low_band) {
            candidate_count_ = 0;
            out.reason = "HEIGHT_NOT_TERMINAL";
        } else if (!out.approach_seen) {
            candidate_count_ = 0;
            out.reason = "NO_RECENT_APPROACH";
        } else if (!out.plateau) {
            candidate_count_ = 0;
            out.reason = "NOT_TERMINAL_PLATEAU";
        } else {
            ++candidate_count_;
            out.reason = "CONFIRMING";
            if (candidate_count_ >= kCandidateFramesV2C1H) {
                latched_ = true;
                release_count_ = 0;
                out.would_contact = true;
                out.event = ShadowContactEventV2C1H::WouldDown;
                out.reason = "WOULD_CONTACT";
            }
        }

        out.would_contact = latched_;
        out.candidate_count = candidate_count_;
        out.release_count = release_count_;
        return out;
    }

    void clear() {
        history_.clear();
        latched_ = false;
        candidate_count_ = 0;
        release_count_ = 0;
    }

private:
    struct HistorySample {
        std::uint32_t frame = 0;
        int x = -1;
        int y = -1;
        double h_median_mm = 0.0;
    };

    void reset_after_release() {
        history_.clear();
        latched_ = false;
        candidate_count_ = 0;
        release_count_ = 0;
    }

    ShadowContactOutputV2C1H handle_unusable(
        ShadowContactOutputV2C1H out,
        const char* reason,
        bool hard_identity_loss) {
        candidate_count_ = 0;
        if (!latched_) {
            out.would_contact = false;
            out.candidate_count = 0;
            out.reason = reason;
            return out;
        }

        if (hard_identity_loss) {
            reset_after_release();
            out.would_contact = false;
            out.event = ShadowContactEventV2C1H::WouldUp;
            out.candidate_count = 0;
            out.release_count = 0;
            out.reason = "WOULD_RELEASE_IDENTITY";
            return out;
        }

        ++release_count_;
        if (release_count_ >= kReleaseFramesV2C1H) {
            reset_after_release();
            out.would_contact = false;
            out.event = ShadowContactEventV2C1H::WouldUp;
            out.candidate_count = 0;
            out.release_count = 0;
            out.reason = "WOULD_RELEASE_NO_SAMPLE";
            return out;
        }
        out.would_contact = true;
        out.release_count = release_count_;
        out.reason = "NO_SAMPLE_GRACE";
        return out;
    }

    void append_history(const ShadowContactInputV2C1H& input) {
        history_.push_back({
            input.frame,
            input.target_x,
            input.target_y,
            input.h_median_mm
        });
        while (history_.size() > kHistoryLimitV2C1H) {
            history_.erase(history_.begin());
        }
    }

    void compute_history_features(
        const ShadowContactInputV2C1H& input,
        ShadowContactOutputV2C1H& out) const {
        if (history_.empty()) return;

        double recent_peak = input.h_median_mm;
        for (const auto& sample : history_) {
            recent_peak = std::max(recent_peak, sample.h_median_mm);
        }
        out.approach_drop_mm = recent_peak - input.h_median_mm;
        out.approach_seen =
            out.approach_drop_mm >= kMinRecentApproachDropHmmV2C1H;

        if (history_.size() < kPlateauSamplesV2C1H) return;
        const std::size_t start = history_.size() - kPlateauSamplesV2C1H;
        const auto& first = history_[start];
        const auto& current = history_.back();
        if (current.frame < first.frame ||
            current.frame - first.frame > kPlateauMaxFrameSpanV2C1H) {
            return;
        }

        double h_min = current.h_median_mm;
        double h_max = current.h_median_mm;
        double max_motion = 0.0;
        for (std::size_t i = start; i < history_.size(); ++i) {
            const auto& sample = history_[i];
            h_min = std::min(h_min, sample.h_median_mm);
            h_max = std::max(h_max, sample.h_median_mm);
            max_motion = std::max(
                max_motion,
                std::hypot(
                    static_cast<double>(sample.x - current.x),
                    static_cast<double>(sample.y - current.y)));
        }
        out.plateau_h_span_mm = h_max - h_min;
        out.plateau_motion_px = max_motion;
        out.plateau =
            out.plateau_h_span_mm <= kPlateauMaxHmmSpanV2C1H &&
            out.plateau_motion_px <= kPlateauMaxMotionPxV2C1H;
    }

    std::vector<HistorySample> history_;
    bool latched_ = false;
    int candidate_count_ = 0;
    int release_count_ = 0;
};

} // namespace touchplus::contact_shadow
