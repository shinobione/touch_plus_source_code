#pragma once

// Phase 2C.1J diagnostic-only ungated anatomy probe.
//
// Runs the full accepted + 2C.1I stack first. It then derives a separate
// APPEARANCE_ONLY_24 half-resolution mask from the already-learned hybrid
// background and publishes LEFT + that mask through a distinct named-memory
// channel. A second Python sidecar may ignore the accepted hand_valid gate on
// this channel only. Its result is console/CSV telemetry and is never consumed
// by accepted anatomy, fusion, stereo, contact or OS output.

#include "phase2c1i_shadow_dataset_runtime.h"
#include "fingertip_anatomy_shadow_ipc_v2c1j.h"

#ifdef compute_depth_heatmap
#undef compute_depth_heatmap
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace touchplus::depth {
namespace shadow_anatomy_runtime_detail_v2c1j {

struct RuntimeStateV2C1J {
    touchplus::tracking::ShadowAnatomySidecarBridgeV2C1J bridge{};
    std::ofstream csv{};
    bool csv_open_attempted = false;
    std::uint64_t rows = 0;
    touchplus::tracking::AnatomyStatusV9 previous_status =
        touchplus::tracking::AnatomyStatusV9::Unavailable;
};

inline RuntimeStateV2C1J& state() {
    static thread_local RuntimeStateV2C1J value;
    return value;
}

inline std::filesystem::path csv_path_v2c1j() {
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
    name << L"touchplus-phase2c1j-shadow-anatomy-"
         << std::put_time(&local, L"%Y%m%d-%H%M%S") << L".csv";
    return directory / name.str();
}

inline void ensure_csv_open_v2c1j(RuntimeStateV2C1J& s) {
    if (s.csv.is_open() || s.csv_open_attempted) return;
    s.csv_open_attempted = true;
    const auto path = csv_path_v2c1j();
    s.csv.open(path, std::ios::out | std::ios::trunc);
    if (!s.csv) {
        std::cerr << "[2C.1J] CSV open failed: " << path.string()
                  << " | shadow anatomy telemetry unavailable"
                  << " | authoritative=UNCHANGED OS_INJECTION=DISABLED\n";
        return;
    }
    s.csv
        << "timestamp_utc,frame,physical_label,shadow_mask_cells,mask_mode,"
           "accepted_hand_valid,accepted_anatomy_status,accepted_anatomy_age,"
           "accepted_fusion_publish,accepted_identity_confidence,"
           "shadow_publish_ok,shadow_status,shadow_age,shadow_source,shadow_pose,"
           "shadow_candidates,shadow_tip_x,shadow_tip_y,shadow_axis_quality,"
           "shadow_hand_confidence,shadow_continuity,shadow_lateral_px,"
           "shadow_extension_px,shadow_reason_code,label_used_for_decision,"
           "shadow_only,accepted_pipeline_consumes_shadow,authoritative,OS_INJECTION\n";
    s.csv.flush();
    std::cout << "[2C.1J] CSV=" << path.string()
              << " | mask=APPEARANCE_ONLY_24"
              << " | IPC=SEPARATE"
              << " | accepted_pipeline_consumes_shadow=NO"
              << " | authoritative=UNCHANGED"
              << " | OS_INJECTION=DISABLED\n";
}

inline std::string timestamp_utc_v2c1j() {
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch());
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_s(&utc, &seconds);
    std::ostringstream value;
    value << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S")
          << '.' << std::setfill('0') << std::setw(3)
          << (milliseconds.count() % 1000) << 'Z';
    return value.str();
}

inline std::vector<std::uint8_t> make_shadow_mask_v2c1j(
    const std::vector<std::uint8_t>& left_gray,
    const std::vector<std::uint8_t>& background_left) {

    const std::size_t eye_pixels =
        static_cast<std::size_t>(kEyeWidth) * kEyeHeight;
    const std::size_t depth_cells =
        static_cast<std::size_t>(kDepthWidth) * kDepthHeight;
    std::vector<std::uint8_t> mask(depth_cells, 0);
    if (left_gray.size() < eye_pixels || background_left.size() < eye_pixels) {
        return mask;
    }

    for (int gy = 0; gy < kDepthHeight; ++gy) {
        for (int gx = 0; gx < kDepthWidth; ++gx) {
            const int px = std::clamp(gx * kDepthScale + 1, 0, kEyeWidth - 1);
            const int py = std::clamp(gy * kDepthScale + 1, 0, kEyeHeight - 1);
            const double delta = touchplus::tracking::appearance_delta_v4(
                left_gray, background_left, px, py);
            if (delta >= touchplus::tracking::kV5AppearanceOnlyDelta) {
                mask[static_cast<std::size_t>(gy) * kDepthWidth + gx] = 1;
            }
        }
    }
    return mask;
}

inline std::size_t count_mask_v2c1j(const std::vector<std::uint8_t>& mask) {
    return static_cast<std::size_t>(
        std::count_if(mask.begin(), mask.end(), [](std::uint8_t v) { return v != 0; }));
}

inline const char* shadow_label_name_v2c1j() {
    return touchplus::diagnostic::shadow_dataset_label_name_v2c1i(
        touchplus::depth::shadow_dataset_runtime_detail_v2c1i::state().label);
}

inline void write_csv_v2c1j(
    RuntimeStateV2C1J& s,
    std::uint32_t frame,
    std::size_t mask_cells,
    bool accepted_hand_valid,
    const touchplus::tracking::AnatomyObservationV9& accepted_anatomy,
    bool fusion_publish,
    const std::string& identity_confidence,
    bool publish_ok,
    const touchplus::tracking::ShadowAnatomyObservationV2C1J& shadow) {

    if (!s.csv.is_open()) return;
    s.csv << timestamp_utc_v2c1j() << ','
          << frame << ','
          << shadow_label_name_v2c1j() << ','
          << mask_cells << ','
          << "APPEARANCE_ONLY_24" << ','
          << (accepted_hand_valid ? 1 : 0) << ','
          << touchplus::tracking::anatomy_status_name_v9(accepted_anatomy.status) << ','
          << accepted_anatomy.age_frames << ','
          << (fusion_publish ? 1 : 0) << ','
          << identity_confidence << ','
          << (publish_ok ? 1 : 0) << ','
          << touchplus::tracking::anatomy_status_name_v9(shadow.status) << ','
          << shadow.age_frames << ','
          << touchplus::tracking::anatomy_source_name_v9(shadow.source) << ','
          << touchplus::tracking::anatomy_pose_name_v9(shadow.pose_mode) << ','
          << shadow.candidate_count << ','
          << shadow.tip_x << ',' << shadow.tip_y << ','
          << std::fixed << std::setprecision(3)
          << shadow.axis_quality << ','
          << shadow.hand_confidence << ','
          << shadow.continuity << ','
          << shadow.lateral_px << ','
          << shadow.extension_px << ','
          << shadow.reason_code << ','
          << "0,1,0,UNCHANGED,DISABLED\n";
    ++s.rows;
    if ((s.rows % 30U) == 0U) s.csv.flush();
}

inline void report_v2c1j(
    std::uint32_t frame,
    std::size_t mask_cells,
    bool accepted_hand_valid,
    const touchplus::tracking::AnatomyObservationV9& accepted_anatomy,
    bool fusion_publish,
    bool publish_ok,
    const touchplus::tracking::ShadowAnatomyObservationV2C1J& shadow,
    bool transition) {

    if (!transition && (frame % 15U) != 0U) return;
    std::cout << std::fixed << std::setprecision(2)
              << "[ANATOMY_SHADOW] frame=" << frame
              << " label=" << shadow_label_name_v2c1j()
              << " mask_cells=" << mask_cells
              << " mask_mode=APPEARANCE_ONLY_24"
              << " accepted_hand=" << (accepted_hand_valid ? 1 : 0)
              << " accepted_anatomy="
              << touchplus::tracking::anatomy_status_name_v9(accepted_anatomy.status)
              << " accepted_fusion=" << (fusion_publish ? 1 : 0)
              << " publish_ok=" << (publish_ok ? 1 : 0)
              << " shadow_status="
              << touchplus::tracking::anatomy_status_name_v9(shadow.status)
              << " shadow_age=" << shadow.age_frames
              << " shadow_source="
              << touchplus::tracking::anatomy_source_name_v9(shadow.source)
              << " shadow_pose="
              << touchplus::tracking::anatomy_pose_name_v9(shadow.pose_mode)
              << " candidates=" << shadow.candidate_count
              << " tip=" << shadow.tip_x << ',' << shadow.tip_y
              << " axis_q=" << shadow.axis_quality
              << " hand_conf=" << shadow.hand_confidence
              << " continuity=" << shadow.continuity
              << " reason_code=" << shadow.reason_code
              << " accepted_pipeline_consumes_shadow=NO"
              << " shadow_only=YES"
              << " authoritative=UNCHANGED"
              << " OS_INJECTION=DISABLED\n";
}

inline void evaluate_v2c1j(
    const std::vector<std::uint8_t>& left_gray) {

    auto& s = state();
    ensure_csv_open_v2c1j(s);

    auto& modern = touchplus::depth::tracking_runtime_detail::state();
    auto& hybrid = touchplus::depth::hybrid_refiner_runtime_detail_v10::state();
    const auto frame = modern.tracker.frame_id();
    if (frame == 0) return;

    const auto& accepted_result = modern.tracker.last_result();
    const auto& accepted_anatomy = modern.tracker.last_anatomy_observation();
    const auto& fusion = modern.tracker.last_fusion();

    std::vector<std::uint8_t> mask(
        static_cast<std::size_t>(kDepthWidth) * kDepthHeight, 0);
    if (hybrid.background_ready) {
        mask = make_shadow_mask_v2c1j(left_gray, hybrid.background_left);
    }
    const std::size_t mask_cells = count_mask_v2c1j(mask);

    const bool publish_ok = s.bridge.publish_frame(
        frame,
        left_gray,
        mask,
        accepted_result.hand_valid,
        hybrid.background_ready);
    const auto shadow = s.bridge.read_result(frame, 3);

    const bool transition = shadow.status != s.previous_status;
    report_v2c1j(
        frame, mask_cells, accepted_result.hand_valid, accepted_anatomy,
        fusion.publish, publish_ok, shadow, transition);
    write_csv_v2c1j(
        s, frame, mask_cells, accepted_result.hand_valid, accepted_anatomy,
        fusion.publish, modern.tracker.identity_confidence(), publish_ok, shadow);
    s.previous_status = shadow.status;
}

} // namespace shadow_anatomy_runtime_detail_v2c1j

inline void compute_depth_heatmap_phase2c1j_wrapper(
    const Calibration& calibration,
    const std::vector<std::uint8_t>& left,
    const std::vector<std::uint8_t>& right,
    DepthWorkspace& workspace) {

    // Run the complete accepted + 2C.1G/H/I stack first.
    touchplus::depth::compute_depth_heatmap_phase2c1i_wrapper(
        calibration, left, right, workspace);

    // Separate IPC + separate result. Diagnostic output only.
    shadow_anatomy_runtime_detail_v2c1j::evaluate_v2c1j(left);
}

} // namespace touchplus::depth

#define compute_depth_heatmap compute_depth_heatmap_phase2c1j_wrapper
