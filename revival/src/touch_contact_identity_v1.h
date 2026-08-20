#pragma once

#include <cmath>
#include <cstdint>
#include <string>

namespace touchplus::contact {

// Phase 2C.1B contact-identity adapter.
//
// Phase 2B intentionally exposes two raw identity namespaces:
//   - GEOMETRY+ANATOMY => persistent V8 geometry branch id
//   - ANATOMY_ONLY     => high-bit sidecar anatomy id
// A single physical fingertip may legitimately alternate between those sources.
// This adapter creates a short-lived semantic contact identity without changing
// any accepted Phase 2B ownership or ids. It aliases ONLY conservative
// cross-source transitions with continuous 2D + metric motion. Same-source raw-id
// changes remain real identity switches.

enum class ContactIdentitySourceV1 {
    Unknown,
    GeometryAnatomy,
    AnatomyOnly,
};

inline const char* contact_identity_source_name_v1(ContactIdentitySourceV1 source) {
    switch (source) {
        case ContactIdentitySourceV1::GeometryAnatomy: return "GEOMETRY+ANATOMY";
        case ContactIdentitySourceV1::AnatomyOnly: return "ANATOMY_ONLY";
        default: return "UNKNOWN";
    }
}

enum class ContactIdentityInterruptionV1 {
    Transient,
    Hard,
};

struct ContactIdentityConfigV1 {
    // Cross-mode aliasing is deliberately tighter than the detector's broad
    // pre-contact motion reset. This bridge exists only to survive source-mode
    // churn, not to hide real motion or identity changes.
    double max_cross_mode_xy_mm = 18.0;
    double max_cross_mode_h_mm = 12.0;
    double max_cross_mode_tip_px = 30.0;

    // Any valid sample beyond these broader bounds begins a new semantic identity
    // even when the raw id itself did not change.
    double max_epoch_xy_mm = 45.0;
    double max_epoch_h_mm = 28.0;
    double max_epoch_tip_px = 90.0;
};

struct ContactIdentityObservationV1 {
    bool valid = false;
    std::uint64_t raw_identity_id = 0;
    ContactIdentitySourceV1 source = ContactIdentitySourceV1::Unknown;
    double x_mm = 0.0;
    double y_mm = 0.0;
    double h_mm = 0.0;
    int tip_x = -1;
    int tip_y = -1;
};

struct ContactIdentityDecisionV1 {
    std::uint64_t contact_identity_id = 0;
    std::uint64_t raw_identity_id = 0;
    ContactIdentitySourceV1 source = ContactIdentitySourceV1::Unknown;
    bool bridged = false;
    bool switched = false;
    std::string reason = "identity-unavailable";
};

class ContactIdentityContinuityV1 {
public:
    explicit ContactIdentityContinuityV1(ContactIdentityConfigV1 config = {}) : config_(config) {}

    void reset() {
        clear_epoch();
    }

    void note_invalid(ContactIdentityInterruptionV1 interruption) {
        if (interruption == ContactIdentityInterruptionV1::Hard) {
            clear_epoch();
            return;
        }
        // Preserve the current alias for the same raw identity across the sparse
        // pre-contact gap handled by 2C.1A, but do NOT allow a cross-mode alias to
        // be created across an invalid semantic sample. One fresh stable valid
        // sample re-arms cross-mode bridging.
        cross_mode_bridge_armed_ = false;
    }

    ContactIdentityDecisionV1 update(const ContactIdentityObservationV1& obs) {
        if (!valid_observation(obs)) {
            ContactIdentityDecisionV1 out;
            out.raw_identity_id = obs.raw_identity_id;
            out.source = obs.source;
            out.reason = "identity-invalid-observation";
            return out;
        }

        if (!have_previous_) {
            return start_new_epoch(obs, "contact-identity-acquired");
        }

        const double xy_delta = metric_xy_delta(obs, previous_);
        const double h_delta = std::abs(obs.h_mm - previous_.h_mm);
        const double tip_delta = pixel_delta(obs, previous_);

        if (xy_delta > config_.max_epoch_xy_mm || h_delta > config_.max_epoch_h_mm ||
            tip_delta > config_.max_epoch_tip_px) {
            return start_new_epoch(obs, "contact-identity-motion-reset");
        }

        if (obs.source == previous_.source) {
            if (obs.raw_identity_id != previous_.raw_identity_id) {
                return start_new_epoch(obs, "same-source-raw-id-switch");
            }
            previous_ = obs;
            cross_mode_bridge_armed_ = true;
            return current_decision(obs, false, false, "contact-identity-stable");
        }

        if (!is_cross_mode_pair(obs.source, previous_.source)) {
            return start_new_epoch(obs, "identity-source-unknown-reset");
        }

        if (!cross_mode_bridge_armed_) {
            return start_new_epoch(obs, "cross-mode-after-gap-reset");
        }

        if (!raw_id_matches_source_encoding(obs.source, obs.raw_identity_id) ||
            !raw_id_matches_source_encoding(previous_.source, previous_.raw_identity_id)) {
            return start_new_epoch(obs, "cross-mode-raw-encoding-reset");
        }

        const std::uint64_t bound = bound_raw_id(obs.source);
        if (bound != 0 && bound != obs.raw_identity_id) {
            return start_new_epoch(obs, "cross-mode-bound-raw-id-switch");
        }

        if (xy_delta > config_.max_cross_mode_xy_mm || h_delta > config_.max_cross_mode_h_mm ||
            tip_delta > config_.max_cross_mode_tip_px) {
            return start_new_epoch(obs, "contact-identity-motion-reset");
        }

        bind_raw_id(obs.source, obs.raw_identity_id);
        previous_ = obs;
        cross_mode_bridge_armed_ = true;
        return current_decision(obs, true, false, "cross-mode-physical-identity-bridge");
    }

    std::uint64_t current_contact_identity_id() const { return contact_identity_id_; }
    std::uint64_t geometry_raw_id() const { return geometry_raw_id_; }
    std::uint64_t anatomy_raw_id() const { return anatomy_raw_id_; }

private:
    static constexpr std::uint64_t kAnatomyHighBit = 0x8000000000000000ULL;

    static bool finite(double v) { return std::isfinite(v); }

    static bool valid_observation(const ContactIdentityObservationV1& obs) {
        return obs.valid && obs.raw_identity_id != 0 && obs.source != ContactIdentitySourceV1::Unknown &&
            finite(obs.x_mm) && finite(obs.y_mm) && finite(obs.h_mm) && obs.tip_x >= 0 && obs.tip_y >= 0;
    }

    static bool is_cross_mode_pair(ContactIdentitySourceV1 a, ContactIdentitySourceV1 b) {
        return (a == ContactIdentitySourceV1::GeometryAnatomy && b == ContactIdentitySourceV1::AnatomyOnly) ||
            (a == ContactIdentitySourceV1::AnatomyOnly && b == ContactIdentitySourceV1::GeometryAnatomy);
    }

    static bool raw_id_matches_source_encoding(ContactIdentitySourceV1 source, std::uint64_t raw_id) {
        if (source == ContactIdentitySourceV1::GeometryAnatomy) return (raw_id & kAnatomyHighBit) == 0;
        if (source == ContactIdentitySourceV1::AnatomyOnly) return (raw_id & kAnatomyHighBit) != 0;
        return false;
    }

    static double metric_xy_delta(const ContactIdentityObservationV1& a, const ContactIdentityObservationV1& b) {
        return std::hypot(a.x_mm - b.x_mm, a.y_mm - b.y_mm);
    }

    static double pixel_delta(const ContactIdentityObservationV1& a, const ContactIdentityObservationV1& b) {
        return std::hypot(static_cast<double>(a.tip_x - b.tip_x), static_cast<double>(a.tip_y - b.tip_y));
    }

    std::uint64_t bound_raw_id(ContactIdentitySourceV1 source) const {
        if (source == ContactIdentitySourceV1::GeometryAnatomy) return geometry_raw_id_;
        if (source == ContactIdentitySourceV1::AnatomyOnly) return anatomy_raw_id_;
        return 0;
    }

    void bind_raw_id(ContactIdentitySourceV1 source, std::uint64_t raw_id) {
        if (source == ContactIdentitySourceV1::GeometryAnatomy) geometry_raw_id_ = raw_id;
        else if (source == ContactIdentitySourceV1::AnatomyOnly) anatomy_raw_id_ = raw_id;
    }

    ContactIdentityDecisionV1 current_decision(const ContactIdentityObservationV1& obs, bool bridged,
                                               bool switched, const char* reason) const {
        ContactIdentityDecisionV1 out;
        out.contact_identity_id = contact_identity_id_;
        out.raw_identity_id = obs.raw_identity_id;
        out.source = obs.source;
        out.bridged = bridged;
        out.switched = switched;
        out.reason = reason;
        return out;
    }

    ContactIdentityDecisionV1 start_new_epoch(const ContactIdentityObservationV1& obs, const char* reason) {
        const bool switched = have_previous_;
        contact_identity_id_ = next_contact_identity_id_++;
        if (contact_identity_id_ == 0) contact_identity_id_ = next_contact_identity_id_++;
        geometry_raw_id_ = 0;
        anatomy_raw_id_ = 0;
        bind_raw_id(obs.source, obs.raw_identity_id);
        previous_ = obs;
        have_previous_ = true;
        cross_mode_bridge_armed_ = true;
        return current_decision(obs, false, switched, reason);
    }

    void clear_epoch() {
        contact_identity_id_ = 0;
        geometry_raw_id_ = 0;
        anatomy_raw_id_ = 0;
        previous_ = {};
        have_previous_ = false;
        cross_mode_bridge_armed_ = false;
    }

    ContactIdentityConfigV1 config_{};
    ContactIdentityObservationV1 previous_{};
    std::uint64_t contact_identity_id_ = 0;
    std::uint64_t next_contact_identity_id_ = 1;
    std::uint64_t geometry_raw_id_ = 0;
    std::uint64_t anatomy_raw_id_ = 0;
    bool have_previous_ = false;
    bool cross_mode_bridge_armed_ = false;
};

} // namespace touchplus::contact
