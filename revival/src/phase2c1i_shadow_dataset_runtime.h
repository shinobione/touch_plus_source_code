#pragma once

// Phase 2C.1I per-frame shadow contact dataset.
//
// This wrapper runs the complete 2C.1H runtime unchanged, then independently
// re-evaluates the same diagnostic raw-dense sample and an isolated copy of the
// 2C.1H shadow proxy solely to serialize a frame-by-frame offline-analysis CSV.
// Operator labels use numeric keys 1/2/3/0 so Phase 2A H/C shortcuts are never
// invoked. Labels are written to the CSV only and never enter proxy decisions.

#include "phase2c1h_shadow_contact_runtime.h"
#include "phase2c1i_shadow_dataset_csv.h"

#ifdef compute_depth_heatmap
#undef compute_depth_heatmap
#endif

#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace touchplus::depth {
namespace shadow_dataset_runtime_detail_v2c1i {

struct RuntimeStateV2C1I {
    touchplus::diagnostic::ShadowDatasetCsvV2C1I csv{};
    touchplus::diagnostic::ShadowDatasetLabelV2C1I label =
        touchplus::diagnostic::ShadowDatasetLabelV2C1I::None;
    std::array<bool, 4> previous_label_keys{};
    bool open_attempted = false;
    std::uint64_t rows = 0;
    touchplus::contact_shadow::ShadowContactProxyV2C1H proxy{};
};

inline RuntimeStateV2C1I& state() {
    static thread_local RuntimeStateV2C1I value;
    return value;
}

inline std::filesystem::path dataset_path_v2c1i() {
    std::array<wchar_t, 32768> module_path{};
    const DWORD length = GetModuleFileNameW(
        nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
    const std::filesystem::path directory =
        length > 0 && length < static_cast<DWORD>(module_path.size())
        ? std::filesystem::path(module_path.data()).parent_path()
        : std::filesystem::current_path();

    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &seconds);
    std::wostringstream name;
    name << L"touchplus-phase2c1i-shadow-dataset-"
         << std::put_time(&local, L"%Y%m%d-%H%M%S") << L".csv";
    return directory / name.str();
}

inline void ensure_open_v2c1i(RuntimeStateV2C1I& s) {
    if (s.csv.is_open() || s.open_attempted) return;
    s.open_attempted = true;
    const auto path = dataset_path_v2c1i();
    if (!s.csv.open(path)) {
        std::cerr << "[2C.1I] CSV open failed: " << path.string()
                  << " | dataset unavailable"
                  << " | label_used_for_decision=NO"
                  << " | OS_INJECTION=DISABLED\n";
        return;
    }
    std::cout << "[2C.1I] CSV=" << path.string()
              << " | ground_truth=NONE"
              << " | keys 1=HIGH 2=NEAR 3=CONTACT 0=NONE"
              << " | label_used_for_decision=NO"
              << " | shadow_only=YES"
              << " | authoritative=UNCHANGED"
              << " | OS_INJECTION=DISABLED\n";
}

inline void maybe_update_label_v2c1i(RuntimeStateV2C1I& s) {
    for (std::size_t i = 0;
         i < touchplus::diagnostic::kShadowDatasetKeysV2C1I.size(); ++i) {
        const int key = touchplus::diagnostic::kShadowDatasetKeysV2C1I[i];
        const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
        const bool rising = down && !s.previous_label_keys[i];
        s.previous_label_keys[i] = down;
        const auto label = touchplus::diagnostic::kShadowDatasetLabelsV2C1I[i];
        if (!rising || s.label == label) continue;

        s.label = label;
        s.csv.flush();
        std::cout << "[2C.1I] ground_truth="
                  << touchplus::diagnostic::shadow_dataset_label_name_v2c1i(s.label)
                  << " | key=" << static_cast<char>(key)
                  << " | label_used_for_decision=NO"
                  << " | OS_INJECTION=DISABLED\n";
    }
}

inline touchplus::diagnostic::ShadowDatasetRowV2C1I make_row_v2c1i(
    const touchplus::depth::raw_dense_diagnostic_detail_v2c1g::RawDenseDiagnosticV2C1G& d,
    const touchplus::contact_shadow::ShadowContactInputV2C1H& input,
    const touchplus::contact_shadow::ShadowContactOutputV2C1H& out,
    touchplus::diagnostic::ShadowDatasetLabelV2C1I label) {

    touchplus::diagnostic::ShadowDatasetRowV2C1I row;
    row.timestamp = std::chrono::system_clock::now();
    row.frame = d.frame;
    row.physical_label = label;
    row.sample_valid = input.sample_valid;
    row.target_source = d.target_source;
    row.target_x = d.target_x;
    row.target_y = d.target_y;
    row.raw_dense_count = d.raw_dense_count;
    row.nearest_px = d.nearest_raw_dense_px;
    row.nearest_h_mm = d.nearest_raw_dense_h_mm;
    row.nearest_disparity_px = d.nearest_raw_dense_disparity_px;
    row.h_min_mm = d.local_h_min_mm;
    row.h_p25_mm = d.local_h_p25_mm;
    row.h_median_mm = d.local_h_median_mm;
    row.spread_mm = out.distribution_spread_mm;
    row.approach_drop_mm = out.approach_drop_mm;
    row.plateau_h_span_mm = out.plateau_h_span_mm;
    row.plateau_motion_px = out.plateau_motion_px;
    row.trusted_target = out.trusted_target;
    row.dense_enough = out.dense_enough;
    row.low_band = out.low_band;
    row.approach_seen = out.approach_seen;
    row.plateau = out.plateau;
    row.candidate_count = out.candidate_count;
    row.release_count = out.release_count;
    row.would_contact = out.would_contact;
    row.event = touchplus::contact_shadow::shadow_contact_event_name_v2c1h(out.event);
    row.reason = out.reason;
    return row;
}

inline void evaluate_and_write_v2c1i(
    const Calibration& calibration,
    const DepthWorkspace& workspace) {

    auto& runtime = state();
    ensure_open_v2c1i(runtime);
    maybe_update_label_v2c1i(runtime);
    if (!runtime.csv.is_open()) return;

    auto& modern = touchplus::depth::tracking_runtime_detail::state();
    if (!modern.enabled || !modern.tracker.background_ready()) {
        runtime.proxy.clear();
    }

    const auto d =
        touchplus::depth::raw_dense_diagnostic_detail_v2c1g::
            evaluate_raw_dense_v2c1g(calibration, workspace);
    const auto input =
        touchplus::depth::shadow_contact_runtime_detail_v2c1h::
            make_input_v2c1h(d);
    const auto out = runtime.proxy.update(input);
    const auto row = make_row_v2c1i(d, input, out, runtime.label);

    if (runtime.csv.write(row)) {
        ++runtime.rows;
    }
}

} // namespace shadow_dataset_runtime_detail_v2c1i

inline void compute_depth_heatmap_phase2c1i_wrapper(
    const Calibration& calibration,
    const std::vector<std::uint8_t>& left,
    const std::vector<std::uint8_t>& right,
    DepthWorkspace& workspace) {

    // Preserve the exact 2C.1H runtime and its shadow telemetry first.
    touchplus::depth::compute_depth_heatmap_phase2c1h_wrapper(
        calibration, left, right, workspace);

    // Dataset-only replay: same raw-dense inputs, separate shadow state,
    // numeric ground-truth label written only after the decision is complete.
    shadow_dataset_runtime_detail_v2c1i::evaluate_and_write_v2c1i(
        calibration, workspace);
}

} // namespace touchplus::depth

#define compute_depth_heatmap compute_depth_heatmap_phase2c1i_wrapper

// Phase 2C.1J wraps the complete 2C.1I stack from a physically separate IPC
// channel. The shadow result remains telemetry-only and cannot enter accepted
// anatomy/fusion/contact behavior.
#include "phase2c1j_shadow_anatomy_runtime.h"
