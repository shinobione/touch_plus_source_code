#pragma once

#include "touch_contact_identity_v1.h"

#include <cmath>
#include <cstdint>
#include <string>

namespace touchplus::contact {

// Phase 2C.1C.1 deliberately does NOT change the identity epoch emitted by
// ContactIdentityContinuityV1. A cross-mode transition after a transient gap
// still starts a new adapter contact identity. This small layer only decides
// whether the contact detector may keep using its previous semantic identity
// long enough to preserve bounded pre-contact metric history.
//
// In other words:
//   adapter identity: old epoch -> NEW epoch (2C.1B safety remains intact)
//   detector identity: may remain stable for one tightly-gated pre-contact handoff
//
// No non-metric sample is accepted here. Both sides of the handoff must be real,
// accepted Phase 2B metric fingertip observations.

struct ContactPrecontactHandoffConfigV1 {
    double current_h_max_mm = 10.0;
    double min_terminal_drop_mm = 5.0;
    double predicted_h_max_mm = 2.0;
    double max_xy_delta_mm = 16.0;
    double max_tip_delta_px = 22.0;
    int max_last_valid_age_frames = 4;
};

struct ContactPrecontactHandoffDecisionV1 {
    std::uint64_t adapter_contact_identity_id = 0;
    std::uint64_t detector_contact_identity_id = 0;
    std::uint64_t previous_adapter_contact_identity_id = 0;
    bool handoff_applied = false;
    bool handoff_candidate = false;
    int last_valid_age_frames = 0;
    double previous_h_mm = 0.0;
    double current_h_mm = 0.0;
    double terminal_drop_mm = 0.0;
    double predicted_next_h_mm = 0.0;
    double xy_delta_mm = 0.0;
    double tip_delta_px = 0.0;
    std::string reason = "precontact-handoff-unavailable";
};

class ContactPrecontactIdentityHandoffV1 {
public:
    explicit ContactPrecontactIdentityHandoffV1(ContactPrecontactHandoffConfigV1 config = {})
        : config_(config) {}

    void reset() {
        frame_ = 0;
        clear_all();
    }

    void note_invalid(ContactIdentityInterruptionV1 interruption) {
        ++frame_;
        if (interruption == ContactIdentityInterruptionV1::Hard) {
            clear_all();
            return;
        }
        had_transient_gap_ = true;
    }

    ContactPrecontactHandoffDecisionV1 update(const ContactIdentityDecisionV1& identity,
                                               const ContactIdentityObservationV1& obs) {
        ++frame_;
        ContactPrecontactHandoffDecisionV1 out;
        out.adapter_contact_identity_id = identity.contact_identity_id;
        out.current_h_mm = obs.h_mm;

        if (!valid(identity, obs)) {
            out.reason = "precontact-handoff-invalid-observation";
            return out;
        }

        if (!have_last_valid_) {
            detector_contact_identity_id_ = identity.contact_identity_id;
            bind_or_reset_epoch(identity, obs, true);
            store_last(identity, obs);
            out.detector_contact_identity_id = detector_contact_identity_id_;
            out.reason = "precontact-handoff-initial-identity";
            return out;
        }

        out.previous_adapter_contact_identity_id = last_adapter_contact_identity_id_;
        out.last_valid_age_frames = frame_ >= last_valid_frame_
            ? static_cast<int>(frame_ - last_valid_frame_) : 0;
        out.previous_h_mm = last_observation_.h_mm;
        out.terminal_drop_mm = last_observation_.h_mm - obs.h_mm;
        out.predicted_next_h_mm = obs.h_mm - out.terminal_drop_mm;
        out.xy_delta_mm = std::hypot(obs.x_mm - last_observation_.x_mm,
                                     obs.y_mm - last_observation_.y_mm);
        out.tip_delta_px = std::hypot(static_cast<double>(obs.tip_x - last_observation_.tip_x),
                                      static_cast<double>(obs.tip_y - last_observation_.tip_y));

        if (identity.contact_identity_id == last_adapter_contact_identity_id_) {
            bind_raw(obs.source, obs.raw_identity_id);
            store_last(identity, obs);
            out.detector_contact_identity_id = detector_contact_identity_id_;
            out.reason = "precontact-handoff-same-adapter-identity";
            had_transient_gap_ = false;
            return out;
        }

        out.handoff_candidate = identity.reason == "cross-mode-after-gap-reset" &&
            had_transient_gap_ && is_cross_mode_pair(last_observation_.source, obs.source);

        if (out.handoff_candidate && safe_cross_gap_handoff(identity, obs, out)) {
            // Keep the detector identity stable while 2C.1B still owns a new
            // adapter epoch. Preserve both raw-source bindings so the later 2D
            // occlusion proxy can detect contradictions across the handoff.
            bind_raw(obs.source, obs.raw_identity_id);
            store_last(identity, obs);
            out.detector_contact_identity_id = detector_contact_identity_id_;
            out.handoff_applied = true;
            out.reason = "deferred-precontact-history-handoff";
            had_transient_gap_ = false;
            return out;
        }

        // Any other adapter epoch change is a real detector identity change.
        detector_contact_identity_id_ = identity.contact_identity_id;
        bind_or_reset_epoch(identity, obs, true);
        store_last(identity, obs);
        out.detector_contact_identity_id = detector_contact_identity_id_;
        if (out.handoff_candidate) out.reason = handoff_reject_reason(identity, obs, out);
        else out.reason = "precontact-handoff-not-applicable";
        had_transient_gap_ = false;
        return out;
    }

    std::uint64_t current_detector_identity_id() const { return detector_contact_identity_id_; }
    std::uint64_t geometry_raw_id() const { return geometry_raw_id_; }
    std::uint64_t anatomy_raw_id() const { return anatomy_raw_id_; }

private:
    static constexpr std::uint64_t kAnatomyHighBit = 0x8000000000000000ULL;

    static bool valid(const ContactIdentityDecisionV1& identity,
                      const ContactIdentityObservationV1& obs) {
        return identity.contact_identity_id != 0 && obs.valid && obs.raw_identity_id != 0 &&
            obs.source != ContactIdentitySourceV1::Unknown && std::isfinite(obs.x_mm) &&
            std::isfinite(obs.y_mm) && std::isfinite(obs.h_mm) && obs.tip_x >= 0 && obs.tip_y >= 0;
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

    std::uint64_t bound_raw(ContactIdentitySourceV1 source) const {
        if (source == ContactIdentitySourceV1::GeometryAnatomy) return geometry_raw_id_;
        if (source == ContactIdentitySourceV1::AnatomyOnly) return anatomy_raw_id_;
        return 0;
    }

    void bind_raw(ContactIdentitySourceV1 source, std::uint64_t raw_id) {
        if (source == ContactIdentitySourceV1::GeometryAnatomy) geometry_raw_id_ = raw_id;
        else if (source == ContactIdentitySourceV1::AnatomyOnly) anatomy_raw_id_ = raw_id;
    }

    void bind_or_reset_epoch(const ContactIdentityDecisionV1& identity,
                             const ContactIdentityObservationV1& obs,
                             bool clear_bindings) {
        if (clear_bindings) {
            geometry_raw_id_ = 0;
            anatomy_raw_id_ = 0;
        }
        bind_raw(obs.source, obs.raw_identity_id);
        last_adapter_contact_identity_id_ = identity.contact_identity_id;
    }

    void store_last(const ContactIdentityDecisionV1& identity,
                    const ContactIdentityObservationV1& obs) {
        last_observation_ = obs;
        last_adapter_contact_identity_id_ = identity.contact_identity_id;
        last_valid_frame_ = frame_;
        have_last_valid_ = true;
    }

    bool safe_cross_gap_handoff(const ContactIdentityDecisionV1& identity,
                                const ContactIdentityObservationV1& obs,
                                const ContactPrecontactHandoffDecisionV1& out) const {
        if (!identity.switched) return false;
        if (!raw_id_matches_source_encoding(last_observation_.source, last_observation_.raw_identity_id) ||
            !raw_id_matches_source_encoding(obs.source, obs.raw_identity_id)) return false;
        const std::uint64_t existing = bound_raw(obs.source);
        if (existing != 0 && existing != obs.raw_identity_id) return false;
        if (out.last_valid_age_frames <= 0 ||
            out.last_valid_age_frames > config_.max_last_valid_age_frames) return false;
        if (obs.h_mm > config_.current_h_max_mm) return false;
        if (out.terminal_drop_mm < config_.min_terminal_drop_mm) return false;
        if (out.predicted_next_h_mm > config_.predicted_h_max_mm) return false;
        if (out.xy_delta_mm > config_.max_xy_delta_mm) return false;
        if (out.tip_delta_px > config_.max_tip_delta_px) return false;
        return true;
    }

    std::string handoff_reject_reason(const ContactIdentityDecisionV1& identity,
                                      const ContactIdentityObservationV1& obs,
                                      const ContactPrecontactHandoffDecisionV1& out) const {
        if (!identity.switched) return "precontact-handoff-reject-not-switched";
        if (!raw_id_matches_source_encoding(last_observation_.source, last_observation_.raw_identity_id) ||
            !raw_id_matches_source_encoding(obs.source, obs.raw_identity_id))
            return "precontact-handoff-reject-raw-encoding";
        const std::uint64_t existing = bound_raw(obs.source);
        if (existing != 0 && existing != obs.raw_identity_id)
            return "precontact-handoff-reject-bound-raw-switch";
        if (out.last_valid_age_frames <= 0 || out.last_valid_age_frames > config_.max_last_valid_age_frames)
            return "precontact-handoff-reject-stale";
        if (obs.h_mm > config_.current_h_max_mm) return "precontact-handoff-reject-not-near-surface";
        if (out.terminal_drop_mm < config_.min_terminal_drop_mm) return "precontact-handoff-reject-no-terminal-descent";
        if (out.predicted_next_h_mm > config_.predicted_h_max_mm) return "precontact-handoff-reject-no-surface-crossing";
        if (out.xy_delta_mm > config_.max_xy_delta_mm) return "precontact-handoff-reject-xy";
        if (out.tip_delta_px > config_.max_tip_delta_px) return "precontact-handoff-reject-tip";
        return "precontact-handoff-reject-unspecified";
    }

    void clear_all() {
        have_last_valid_ = false;
        had_transient_gap_ = false;
        last_observation_ = {};
        last_adapter_contact_identity_id_ = 0;
        detector_contact_identity_id_ = 0;
        last_valid_frame_ = 0;
        geometry_raw_id_ = 0;
        anatomy_raw_id_ = 0;
    }

    ContactPrecontactHandoffConfigV1 config_{};
    std::uint64_t frame_ = 0;
    bool have_last_valid_ = false;
    bool had_transient_gap_ = false;
    ContactIdentityObservationV1 last_observation_{};
    std::uint64_t last_adapter_contact_identity_id_ = 0;
    std::uint64_t detector_contact_identity_id_ = 0;
    std::uint64_t last_valid_frame_ = 0;
    std::uint64_t geometry_raw_id_ = 0;
    std::uint64_t anatomy_raw_id_ = 0;
};

} // namespace touchplus::contact
