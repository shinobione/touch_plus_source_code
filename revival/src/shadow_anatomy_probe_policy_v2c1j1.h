#pragma once

// Phase 2C.1J.1 lightweight shadow-anatomy probe policy.
//
// The shadow anatomy path is diagnostic only. To avoid perturbing the accepted
// Touch+ capture/viewer, do not even build the appearance-only shadow mask while
// the accepted V8/V9 hand gate is healthy. Probe only when the accepted hand is
// invalid AND accepted anatomy is UNAVAILABLE, and only at a low fixed cadence.

#include <cstddef>
#include <cstdint>

namespace touchplus::tracking::shadow_probe_v2c1j1 {

constexpr std::uint32_t kProbePeriodFrames = 6; // ~5 Hz at the accepted ~30 fps stream.
constexpr std::size_t kShadowDepthCells = 320u * 240u;
constexpr std::size_t kMinMaskCells = 120u;
constexpr std::size_t kMaxMaskCells = (kShadowDepthCells * 65u) / 100u;

enum class ProbeGate {
    Due,
    NoFrame,
    BackgroundNotReady,
    AcceptedHandValid,
    AcceptedAnatomyNotUnavailable,
    Throttled,
    MaskTooSmall,
    MaskTooLarge,
};

inline const char* probe_gate_name(ProbeGate gate) {
    switch (gate) {
        case ProbeGate::Due: return "DUE";
        case ProbeGate::NoFrame: return "NO_FRAME";
        case ProbeGate::BackgroundNotReady: return "BACKGROUND_NOT_READY";
        case ProbeGate::AcceptedHandValid: return "ACCEPTED_HAND_VALID";
        case ProbeGate::AcceptedAnatomyNotUnavailable: return "ACCEPTED_ANATOMY_NOT_UNAVAILABLE";
        case ProbeGate::Throttled: return "THROTTLED";
        case ProbeGate::MaskTooSmall: return "MASK_TOO_SMALL";
        case ProbeGate::MaskTooLarge: return "MASK_TOO_LARGE";
    }
    return "UNKNOWN";
}

inline ProbeGate precheck(
    std::uint32_t frame,
    bool background_ready,
    bool accepted_hand_valid,
    bool accepted_anatomy_unavailable) {

    if (frame == 0) return ProbeGate::NoFrame;
    if (!background_ready) return ProbeGate::BackgroundNotReady;
    if (accepted_hand_valid) return ProbeGate::AcceptedHandValid;
    if (!accepted_anatomy_unavailable) {
        return ProbeGate::AcceptedAnatomyNotUnavailable;
    }
    if ((frame % kProbePeriodFrames) != 0U) return ProbeGate::Throttled;
    return ProbeGate::Due;
}

inline ProbeGate validate_mask(ProbeGate precheck_gate, std::size_t mask_cells) {
    if (precheck_gate != ProbeGate::Due) return precheck_gate;
    if (mask_cells < kMinMaskCells) return ProbeGate::MaskTooSmall;
    if (mask_cells > kMaxMaskCells) return ProbeGate::MaskTooLarge;
    return ProbeGate::Due;
}

} // namespace touchplus::tracking::shadow_probe_v2c1j1
