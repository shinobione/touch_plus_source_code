#pragma once

// Phase 2C.1H diagnostic-only runtime wrapper.
//
// Runs the exact 2C.1G.1 wrapper first, re-evaluates its raw-dense diagnostic
// without changing any accepted result, then feeds a separate shadow-only
// contact proxy. The proxy can emit WOULD_DOWN / WOULD_UP telemetry but never
// touches ContactStateMachineV2C1, authoritative fingertip selection, stereo,
// calibration, surface state, smoothing, promotion or OS output.

#include "phase2c1g_raw_dense_diagnostic.h"
#include "contact_proxy_shadow_v2c1h.h"

#ifdef compute_depth_heatmap
#undef compute_depth_heatmap
#endif

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>

namespace touchplus::depth {
namespace shadow_contact_runtime_detail_v2c1h {

struct RuntimeStateV2C1H {
    touchplus::contact_shadow::ShadowContactProxyV2C1H proxy{};
    bool previous_would_contact = false;
};

inline RuntimeStateV2C1H& state() {
    static thread_local RuntimeStateV2C1H value;
    return value;
}

inline touchplus::contact_shadow::TargetSourceV2C1H map_target_source_v2c1h(
    const char* source) {
    if (source == nullptr) return touchplus::contact_shadow::TargetSourceV2C1H::None;
    if (std::strcmp(source, "FUSED") == 0)
        return touchplus::contact_shadow::TargetSourceV2C1H::Fused;
    if (std::strcmp(source, "ANATOMY") == 0)
        return touchplus::contact_shadow::TargetSourceV2C1H::Anatomy;
    if (std::strcmp(source, "GEOMETRY") == 0)
        return touchplus::contact_shadow::TargetSourceV2C1H::Geometry;
    return touchplus::contact_shadow::TargetSourceV2C1H::None;
}

inline touchplus::contact_shadow::ShadowContactInputV2C1H make_input_v2c1h(
    const touchplus::depth::raw_dense_diagnostic_detail_v2c1g::RawDenseDiagnosticV2C1G& d) {
    touchplus::contact_shadow::ShadowContactInputV2C1H input;
    input.frame = d.frame;
    input.source = map_target_source_v2c1h(d.target_source);
    input.target_x = d.target_x;
    input.target_y = d.target_y;
    input.raw_dense_count = d.raw_dense_count;
    input.h_p25_mm = d.local_h_p25_mm;
    input.h_median_mm = d.local_h_median_mm;
    input.sample_valid =
        d.attempted && d.raw_dense_count > 0 &&
        std::isfinite(d.local_h_p25_mm) &&
        std::isfinite(d.local_h_median_mm);
    return input;
}

inline void print_value_v2c1h(double value) {
    if (std::isfinite(value)) std::cout << value;
    else std::cout << "nan";
}

inline void report_v2c1h(
    const touchplus::depth::raw_dense_diagnostic_detail_v2c1g::RawDenseDiagnosticV2C1G& d,
    const touchplus::contact_shadow::ShadowContactOutputV2C1H& out,
    bool transition) {

    if (!transition && (d.frame % 15U) != 0U) return;

    const auto& hybrid =
        touchplus::depth::hybrid_refiner_runtime_detail_v10::state();

    std::cout << std::fixed << std::setprecision(1)
              << "[CONTACT_SHADOW] frame=" << d.frame
              << " operator_label="
              << touchplus::diagnostic::physical_label_name_v2c1a(
                    hybrid.physical_label)
              << " target_source=" << d.target_source
              << " target=" << d.target_x << ',' << d.target_y
              << " would_contact=" << (out.would_contact ? "YES" : "NO")
              << " event="
              << touchplus::contact_shadow::shadow_contact_event_name_v2c1h(
                    out.event)
              << " reason=" << out.reason
              << " raw_count=" << d.raw_dense_count
              << " H_p25=";
    print_value_v2c1h(d.local_h_p25_mm);
    std::cout << " H_median=";
    print_value_v2c1h(d.local_h_median_mm);
    std::cout << " spread=";
    print_value_v2c1h(out.distribution_spread_mm);
    std::cout << " approach_drop=";
    print_value_v2c1h(out.approach_drop_mm);
    std::cout << " plateau_H_span=";
    print_value_v2c1h(out.plateau_h_span_mm);
    std::cout << " plateau_motion_px=";
    print_value_v2c1h(out.plateau_motion_px);
    std::cout << " trusted_target=" << (out.trusted_target ? 1 : 0)
              << " dense_enough=" << (out.dense_enough ? 1 : 0)
              << " low_band=" << (out.low_band ? 1 : 0)
              << " approach_seen=" << (out.approach_seen ? 1 : 0)
              << " plateau=" << (out.plateau ? 1 : 0)
              << " candidate_count=" << out.candidate_count
              << " release_count=" << out.release_count
              << " label_used_for_decision=NO"
              << " shadow_only=YES"
              << " authoritative=UNCHANGED"
              << " OS_INJECTION=DISABLED\n";
}

inline void evaluate_and_report_v2c1h(
    const Calibration& calibration,
    const DepthWorkspace& workspace) {

    auto& runtime = state();
    auto& modern = touchplus::depth::tracking_runtime_detail::state();

    if (!modern.enabled || !modern.tracker.background_ready()) {
        runtime.proxy.clear();
        runtime.previous_would_contact = false;
        return;
    }

    const auto d =
        touchplus::depth::raw_dense_diagnostic_detail_v2c1g::
            evaluate_raw_dense_v2c1g(calibration, workspace);
    const auto input = make_input_v2c1h(d);
    const auto out = runtime.proxy.update(input);

    const bool transition =
        out.event != touchplus::contact_shadow::ShadowContactEventV2C1H::None ||
        out.would_contact != runtime.previous_would_contact;
    report_v2c1h(d, out, transition);
    runtime.previous_would_contact = out.would_contact;
}

} // namespace shadow_contact_runtime_detail_v2c1h

inline void compute_depth_heatmap_phase2c1h_wrapper(
    const Calibration& calibration,
    const std::vector<std::uint8_t>& left,
    const std::vector<std::uint8_t>& right,
    DepthWorkspace& workspace) {

    // Preserve the complete accepted runtime and 2C.1G.1 telemetry first.
    touchplus::depth::compute_depth_heatmap_phase2c1g_wrapper(
        calibration, left, right, workspace);

    shadow_contact_runtime_detail_v2c1h::evaluate_and_report_v2c1h(
        calibration, workspace);
}

} // namespace touchplus::depth

#define compute_depth_heatmap compute_depth_heatmap_phase2c1h_wrapper

// 2C.1I is layered after the complete 2C.1H wrapper. Because this file is the
// existing forced include for the live depth viewer, this preserves the build
// plumbing while allowing 2C.1I to wrap 2C.1H without changing authoritative
// runtime ownership.
#include "phase2c1i_shadow_dataset_runtime.h"
