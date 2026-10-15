#pragma once

// Phase 2B.10D gated authoritative promotion layer.
//
// Keep the accepted Phase 2B.9C.2 tracker and all metric/stereo behavior intact.
// The Ractiv-inspired full-resolution distal refiner is physically validated as
// a useful 2D diagnostic. B is evaluated through the same robust Touch+ stereo
// primitives in parallel. The unchanged 2B.10C gate may promote one complete B
// pixel/raw-metric sample, but only behind explicit runtime opt-in. Default mode
// leaves the accepted A result and smoothing path unchanged.

#include "depth_surface_frame_runtime.h"
#include "contact_state_machine_v2c1.h"
#include "fingertip_authoritative_selection_v10d.h"
#include "fingertip_promotion_gate_v10c.h"
#include "fingertip_refiner_v10.h"
#include "fingertip_stereo_shadow_v10b.h"
#include "phase2c1a_diagnostic.h"

#ifdef point_depth
#undef point_depth
#endif
#ifdef compute_depth_heatmap
#undef compute_depth_heatmap
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <vector>

namespace touchplus::depth {
namespace hybrid_refiner_runtime_detail_v10 {

inline std::atomic<bool>& promotion_enabled_config_v10d() {
    static std::atomic<bool> value{false};
    return value;
}

inline bool promotion_enabled_v10d() {
    return promotion_enabled_config_v10d().load(std::memory_order_acquire);
}

inline const char* promotion_mode_name_v10d() {
    return promotion_enabled_v10d() ? "ENABLED" : "DISABLED";
}

struct RuntimeStateV10 {
    bool previous_b_down = false;
    bool background_learning = false;
    bool background_ready = false;
    int background_frames = 0;
    std::vector<std::uint32_t> background_sum;
    std::vector<std::uint8_t> background_left;
    touchplus::tracking::DistalRefineResultV10 last{};
    touchplus::tracking::ShadowStereoResultV10B shadow{};
    touchplus::tracking::PromotionGateResultV10C gate{};
    bool gate_has_result = false;
    touchplus::tracking::TrackingResult accepted_a{};
    touchplus::tracking::AuthoritativeSelectionV10D selection{};
    bool selection_has_result = false;
    touchplus::tracking::PromotionSmootherV10D promotion_smoother{};
    touchplus::tracking::PromotionSelectionStatsV10D selection_stats{};
    std::uint64_t selection_identity_id = 0;
    touchplus::contact::ContactStateMachineV2C1 contact_machine{};
    touchplus::contact::ContactOutputV2C1 contact{};
    touchplus::diagnostic::DiagnosticCsvV2C1A diagnostic_csv{};
    touchplus::diagnostic::PhysicalLabelV2C1A physical_label =
        touchplus::diagnostic::PhysicalLabelV2C1A::None;
    std::array<bool, 4> previous_label_keys{};
    std::uint64_t diagnostic_frame = 0;
    bool diagnostic_open_attempted = false;
    std::uint64_t attempts = 0;
    std::uint64_t accepts = 0;
    std::uint64_t shadow_attempts = 0;
    std::uint64_t shadow_valid = 0;
    std::uint64_t both_valid = 0;
    std::uint64_t a_only_valid = 0;
    std::uint64_t b_only_valid = 0;
    std::uint64_t gate_evaluations = 0;
    std::uint64_t gate_keep_a = 0;
    std::uint64_t gate_would_select_b = 0;
    std::array<std::uint64_t,
        static_cast<std::size_t>(touchplus::tracking::PromotionReasonV10C::Count)>
        gate_reason_counts{};
    std::uint64_t report_counter = 0;
};

inline RuntimeStateV10& state() {
    static thread_local RuntimeStateV10 value;
    return value;
}

inline void start_background(RuntimeStateV10& s) {
    const std::size_t eye_pixels =
        static_cast<std::size_t>(kEyeWidth) * kEyeHeight;
    s.background_learning = true;
    s.background_ready = false;
    s.background_frames = 0;
    s.background_sum.assign(eye_pixels, 0u);
    s.background_left.clear();
    s.last = {};
    s.shadow = {};
    s.gate = {};
    s.gate_has_result = false;
    s.accepted_a = {};
    s.selection = {};
    s.selection_has_result = false;
    s.promotion_smoother = {};
    s.selection_stats = {};
    s.selection_identity_id = 0;
    s.attempts = 0;
    s.accepts = 0;
    s.shadow_attempts = 0;
    s.shadow_valid = 0;
    s.both_valid = 0;
    s.a_only_valid = 0;
    s.b_only_valid = 0;
    s.gate_evaluations = 0;
    s.gate_keep_a = 0;
    s.gate_would_select_b = 0;
    s.gate_reason_counts.fill(0);
    std::cout
        << "[HYBRID] 2B.10D refiner background learning started | "
        << "promotion_mode=" << promotion_mode_name_v10d() << " "
        << "OS_INJECTION=DISABLED\n";
}

inline void maybe_start_background(RuntimeStateV10& s) {
    const bool down = (GetAsyncKeyState('B') & 0x8000) != 0;
    const bool rising = down && !s.previous_b_down;
    s.previous_b_down = down;
    if (rising) start_background(s);
}

inline std::filesystem::path diagnostic_csv_path_v2c1a() {
    std::array<wchar_t, 32768> module_path{};
    const DWORD length = GetModuleFileNameW(
        nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
    std::filesystem::path directory =
        length > 0 && length < static_cast<DWORD>(module_path.size())
        ? std::filesystem::path(module_path.data()).parent_path()
        : std::filesystem::current_path();
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &seconds);
    std::wostringstream name;
    name << L"touchplus-phase2c1a-" << std::put_time(&local, L"%Y%m%d-%H%M%S")
         << L".csv";
    return directory / name.str();
}

inline void ensure_diagnostic_open_v2c1a(RuntimeStateV10& s) {
    if (s.diagnostic_csv.is_open() || s.diagnostic_open_attempted) return;
    s.diagnostic_open_attempted = true;
    const auto path = diagnostic_csv_path_v2c1a();
    if (!s.diagnostic_csv.open(path)) {
        std::cerr << "[2C.1A] CSV open failed: " << path.string()
                  << " | diagnostic capture unavailable"
                  << " | OS_INJECTION=DISABLED\n";
        return;
    }
    std::cout << "[2C.1A] CSV=" << path.string()
              << " | label=NONE | keys H=HIGH N=NEAR C=CONTACT 0=NONE"
              << " | OS_INJECTION=DISABLED\n";
}

inline void maybe_update_physical_label_v2c1a(RuntimeStateV10& s) {
    constexpr std::array<int, 4> keys{'H', 'N', 'C', '0'};
    constexpr std::array<touchplus::diagnostic::PhysicalLabelV2C1A, 4> labels{
        touchplus::diagnostic::PhysicalLabelV2C1A::High,
        touchplus::diagnostic::PhysicalLabelV2C1A::Near,
        touchplus::diagnostic::PhysicalLabelV2C1A::Contact,
        touchplus::diagnostic::PhysicalLabelV2C1A::None};
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const bool down = (GetAsyncKeyState(keys[i]) & 0x8000) != 0;
        const bool rising = down && !s.previous_label_keys[i];
        s.previous_label_keys[i] = down;
        if (!rising || s.physical_label == labels[i]) continue;
        s.physical_label = labels[i];
        s.diagnostic_csv.flush();
        std::cout << "[2C.1A] physical_label="
                  << touchplus::diagnostic::physical_label_name_v2c1a(
                      s.physical_label)
                  << " | OS_INJECTION=DISABLED\n";
    }
}

inline void accumulate_background(
    RuntimeStateV10& s,
    const std::vector<std::uint8_t>& left_gray) {

    if (!s.background_learning) return;
    const std::size_t eye_pixels =
        static_cast<std::size_t>(kEyeWidth) * kEyeHeight;
    if (left_gray.size() < eye_pixels || s.background_sum.size() != eye_pixels) {
        s.background_learning = false;
        s.background_ready = false;
        s.background_sum.clear();
        std::cout << "[HYBRID] refiner background aborted: invalid LEFT frame size.\n";
        return;
    }

    for (std::size_t i = 0; i < eye_pixels; ++i) {
        s.background_sum[i] += left_gray[i];
    }
    ++s.background_frames;

    if (s.background_frames < touchplus::tracking::kV5BackgroundFrames) return;

    s.background_left.resize(eye_pixels);
    for (std::size_t i = 0; i < eye_pixels; ++i) {
        s.background_left[i] = static_cast<std::uint8_t>(
            s.background_sum[i] /
            static_cast<std::uint32_t>(s.background_frames));
    }
    s.background_sum.clear();
    s.background_learning = false;
    s.background_ready = true;
    std::cout
        << "[HYBRID] 2B.10D refiner background READY | frames="
        << s.background_frames
        << " | promotion_mode=" << promotion_mode_name_v10d()
        << " | authoritative_default=A"
        << " | OS_INJECTION=DISABLED\n";
}

inline bool modern_identity_stale_v10c(
    const touchplus::depth::tracking_runtime_detail::RuntimeState& modern) {

    const auto& observation = modern.tracker.last_anatomy_observation();
    const auto& anatomy = modern.tracker.last_anatomy_decision();
    return observation.status == touchplus::tracking::AnatomyStatusV9::Stale ||
        anatomy.stale ||
        anatomy.sync_status == touchplus::tracking::AnatomySyncStatusV9::TooOld;
}

inline bool modern_identity_current_v10c(
    const touchplus::depth::tracking_runtime_detail::RuntimeState& modern) {

    const auto& anatomy = modern.tracker.last_anatomy_decision();
    const bool sync_current =
        anatomy.sync_status == touchplus::tracking::AnatomySyncStatusV9::CurrentFrame ||
        anatomy.sync_status == touchplus::tracking::AnatomySyncStatusV9::MotionCompensated;
    return anatomy.publish &&
        anatomy.state == touchplus::tracking::AnatomyTrackStateV9::Locked &&
        !anatomy.stale && !anatomy.explicit_reject && !anatomy.jump_rejected &&
        sync_current;
}

inline touchplus::tracking::AuthoritativeSampleV10D sample_from_a_v10d(
    const touchplus::tracking::TrackingResult& a,
    const std::string& stereo_confidence) {
    return {
        a.fingertip_valid,
        a.pixel_x,
        a.pixel_y,
        a.raw_tip.x_mm,
        a.raw_tip.y_mm,
        a.raw_tip.h_mm,
        stereo_confidence,
        a.refinement_support
    };
}

inline touchplus::tracking::AuthoritativeSampleV10D sample_from_b_v10d(
    const touchplus::tracking::ShadowStereoResultV10B& b) {
    return {
        b.valid,
        b.pixel_x,
        b.pixel_y,
        b.raw_tip.x_mm,
        b.raw_tip.y_mm,
        b.raw_tip.h_mm,
        b.stereo_confidence,
        b.refinement_support
    };
}

inline void reset_promotion_smoother_v10d(RuntimeStateV10& hybrid) {
    hybrid.promotion_smoother = {};
    hybrid.selection_identity_id = 0;
}

inline void apply_authoritative_selection_v10d(
    RuntimeStateV10& hybrid,
    touchplus::depth::tracking_runtime_detail::RuntimeState& modern) {

    const bool enabled = promotion_enabled_v10d();
    const auto a = sample_from_a_v10d(
        hybrid.accepted_a, modern.tracker.stereo_confidence());
    const auto b = sample_from_b_v10d(hybrid.shadow);
    hybrid.selection = touchplus::tracking::select_authoritative_sample_v10d(
        enabled, hybrid.gate, a, b);
    hybrid.selection_has_result = true;
    touchplus::tracking::record_authoritative_selection_v10d(
        hybrid.selection.source, hybrid.selection_stats);

    // OFF is deliberately a no-op on the accepted result and accepted tracker
    // smoothing state. The experimental smoother exists only in explicit ON
    // mode and consumes the same complete source sample selected above.
    if (!enabled) return;

    const auto& fusion = modern.tracker.last_fusion();
    if (!hybrid.selection.sample.valid || !fusion.publish ||
        fusion.identity_id == 0) {
        reset_promotion_smoother_v10d(hybrid);
        return;
    }
    if (hybrid.selection_identity_id != fusion.identity_id) {
        hybrid.promotion_smoother = {};
        hybrid.selection_identity_id = fusion.identity_id;
    }
    if (!touchplus::tracking::consume_selected_sample_v10d(
            true, hybrid.selection.sample, hybrid.promotion_smoother)) {
        reset_promotion_smoother_v10d(hybrid);
        return;
    }

    const auto& selected = hybrid.selection.sample;
    modern.result.pixel_x = selected.pixel_x;
    modern.result.pixel_y = selected.pixel_y;
    modern.result.raw_tip.x_mm = selected.x_mm;
    modern.result.raw_tip.y_mm = selected.y_mm;
    modern.result.raw_tip.h_mm = selected.h_mm;
    modern.result.smoothed_tip.x_mm = hybrid.promotion_smoother.x_mm;
    modern.result.smoothed_tip.y_mm = hybrid.promotion_smoother.y_mm;
    modern.result.smoothed_tip.h_mm = hybrid.promotion_smoother.h_mm;
    modern.result.refinement_support = selected.support;
    modern.result.fingertip_valid = true;
    modern.result.confidence =
        modern.tracker.identity_confidence() == "HIGH" &&
        selected.stereo_confidence == "HIGH" ? "HIGH" : "MEDIUM";
}

inline void evaluate_and_record_gate_v10c(
    RuntimeStateV10& hybrid,
    touchplus::depth::tracking_runtime_detail::RuntimeState& modern) {

    const auto& fusion = modern.tracker.last_fusion();
    const auto& a = hybrid.accepted_a;
    const auto& r = hybrid.last;
    const auto& b = hybrid.shadow;

    touchplus::tracking::PromotionGateInputV10C input;
    input.identity_stale = modern_identity_stale_v10c(modern);
    input.identity_accepted = fusion.publish && fusion.identity_id != 0 &&
        touchplus::tracking::promotion_confidence_rank_v10c(
            modern.tracker.identity_confidence()) > 0;
    input.identity_current = modern_identity_current_v10c(modern);
    input.refiner_accepted = r.accepted &&
        r.status == touchplus::tracking::DistalRefineStatusV10::Accepted;
    input.refiner_inward =
        r.status == touchplus::tracking::DistalRefineStatusV10::MovedTowardPalm ||
        (r.accepted && (!std::isfinite(r.forward_px) || r.forward_px < 0.0));
    input.a_valid = a.fingertip_valid;
    input.b_valid = b.valid;
    input.a_pixel_x = a.pixel_x;
    input.a_pixel_y = a.pixel_y;
    input.b_pixel_x = b.pixel_x;
    input.b_pixel_y = b.pixel_y;
    input.a_stereo_confidence = modern.tracker.stereo_confidence();
    input.b_stereo_confidence = b.stereo_confidence;
    input.a_support = a.refinement_support;
    input.b_support = b.refinement_support;
    input.a_x_mm = a.raw_tip.x_mm;
    input.a_y_mm = a.raw_tip.y_mm;
    input.a_h_mm = a.raw_tip.h_mm;
    input.b_x_mm = b.raw_tip.x_mm;
    input.b_y_mm = b.raw_tip.y_mm;
    input.b_h_mm = b.raw_tip.h_mm;

    hybrid.gate = touchplus::tracking::evaluate_promotion_gate_v10c(input);
    hybrid.gate_has_result = true;
    ++hybrid.gate_evaluations;
    if (hybrid.gate.decision ==
        touchplus::tracking::PromotionDecisionV10C::WouldSelectB) {
        ++hybrid.gate_would_select_b;
    } else {
        ++hybrid.gate_keep_a;
    }
    const auto reason_index = static_cast<std::size_t>(hybrid.gate.reason);
    if (reason_index < hybrid.gate_reason_counts.size()) {
        ++hybrid.gate_reason_counts[reason_index];
    }
    apply_authoritative_selection_v10d(hybrid, modern);
}

inline touchplus::contact::FingertipSourceV2C1 contact_source_v2c1(
    const RuntimeStateV10& hybrid) {

    if (hybrid.selection_has_result &&
        hybrid.selection.source ==
            touchplus::tracking::AuthoritativeSourceV10D::B) {
        return touchplus::contact::FingertipSourceV2C1::B;
    }
    return touchplus::contact::FingertipSourceV2C1::A;
}

inline void print_contact_telemetry_v2c1(
    const char* prefix,
    const touchplus::contact::ContactOutputV2C1& contact) {

    std::cout
        << std::fixed << std::setprecision(1)
        << prefix
        << " contact_state="
        << touchplus::contact::contact_state_name_v2c1(contact.state)
        << " contact_event="
        << touchplus::contact::contact_event_name_v2c1(contact.event)
        << " contact_reason=" << contact.reason
        << " identity_id=" << contact.identity_id
        << " fingertip_source="
        << touchplus::contact::fingertip_source_name_v2c1(
            contact.fingertip_source)
        << " X=" << contact.x_mm
        << " Y=" << contact.y_mm
        << " H=" << contact.h_mm;
    if (contact.delta_valid) {
        std::cout
            << " dH=" << contact.dh_mm
            << " dXY=" << contact.dxy_mm;
    } else {
        std::cout << " dH=nan dXY=nan";
    }
    std::cout
        << " candidate_count=" << contact.candidate_count
        << " release_count=" << contact.release_count
        << " DOWN_total=" << contact.down_total
        << " UP_total=" << contact.up_total
        << " OS_INJECTION=DISABLED\n";
}

inline void update_contact_v2c1(
    RuntimeStateV10& hybrid,
    touchplus::depth::tracking_runtime_detail::RuntimeState& modern) {

    const auto& fusion = modern.tracker.last_fusion();
    touchplus::contact::ContactInputV2C1 input;
    input.identity_accepted = modern.enabled && fusion.publish &&
        fusion.identity_id != 0 &&
        touchplus::tracking::promotion_confidence_rank_v10c(
            modern.tracker.identity_confidence()) > 0;
    input.identity_current = modern_identity_current_v10c(modern);
    input.identity_stale = modern_identity_stale_v10c(modern);
    input.sample_valid = modern.result.fingertip_valid;
    input.identity_id = fusion.identity_id;
    input.fingertip_source = contact_source_v2c1(hybrid);
    input.x_mm = modern.result.smoothed_tip.x_mm;
    input.y_mm = modern.result.smoothed_tip.y_mm;
    input.h_mm = modern.result.smoothed_tip.h_mm;

    hybrid.contact = hybrid.contact_machine.update(input);

    ensure_diagnostic_open_v2c1a(hybrid);
    touchplus::diagnostic::DiagnosticRowV2C1A row;
    row.timestamp = std::chrono::system_clock::now();
    row.frame = ++hybrid.diagnostic_frame;
    row.physical_label = hybrid.physical_label;
    row.identity_id = input.identity_id;
    row.identity_accepted = input.identity_accepted;
    row.identity_current = input.identity_current;
    row.identity_stale = input.identity_stale;
    row.fingertip_valid = input.sample_valid;
    row.fingertip_source = touchplus::contact::fingertip_source_name_v2c1(
        input.fingertip_source);
    row.raw_h_mm = input.sample_valid
        ? modern.result.raw_tip.h_mm
        : std::numeric_limits<double>::quiet_NaN();
    row.smoothed_h_mm = input.sample_valid
        ? input.h_mm
        : std::numeric_limits<double>::quiet_NaN();
    row.contact_state = touchplus::contact::contact_state_name_v2c1(
        hybrid.contact.state);
    row.contact_event = touchplus::contact::contact_event_name_v2c1(
        hybrid.contact.event);
    row.rejection_reason = hybrid.contact.reason;
    if (!hybrid.diagnostic_csv.write(row)) {
        std::cerr << "[2C.1A] CSV write failed at frame=" << row.frame << '\n';
    }
    if (hybrid.contact.state_changed ||
        hybrid.contact.event == touchplus::contact::ContactEventV2C1::TouchDown ||
        hybrid.contact.event == touchplus::contact::ContactEventV2C1::TouchUp) {
        print_contact_telemetry_v2c1("[CONTACT_TRANSITION]", hybrid.contact);
    }
}

inline void overlay_refined_candidate(
    std::vector<std::uint8_t>& heatmap_bgra,
    const touchplus::tracking::DistalRefineResultV10& refined) {

    if (!refined.accepted || refined.refined_x < 0 || refined.refined_y < 0) return;
    if (heatmap_bgra.size() <
        static_cast<std::size_t>(kDepthWidth) * kDepthHeight * 4) return;

    const int gx = refined.refined_x / kDepthScale;
    const int gy = refined.refined_y / kDepthScale;
    auto set_green = [&](int x, int y) {
        if (x < 0 || x >= kDepthWidth || y < 0 || y >= kDepthHeight) return;
        const std::size_t i =
            (static_cast<std::size_t>(y) * kDepthWidth + x) * 4;
        // BGRA: green.
        heatmap_bgra[i] = 0;
        heatmap_bgra[i + 1] = 255;
        heatmap_bgra[i + 2] = 0;
        heatmap_bgra[i + 3] = 255;
    };

    for (int d = -7; d <= 7; ++d) {
        set_green(gx + d, gy + d);
        set_green(gx + d, gy - d);
    }
    for (int d = -3; d <= 3; ++d) {
        set_green(gx + d, gy);
        set_green(gx, gy + d);
    }
}

inline void run_refiner(
    RuntimeStateV10& hybrid,
    touchplus::depth::tracking_runtime_detail::RuntimeState& modern,
    const Calibration& calibration,
    const std::vector<std::uint8_t>& left_gray,
    const std::vector<std::uint8_t>& right_gray,
    const DepthWorkspace& workspace,
    std::vector<std::uint8_t>& heatmap_bgra) {

    hybrid.last = {};
    hybrid.shadow = {};
    hybrid.gate = {};
    hybrid.gate_has_result = false;
    hybrid.accepted_a = modern.result;
    hybrid.selection = {};
    hybrid.selection_has_result = false;
    if (hybrid.background_learning) {
        accumulate_background(hybrid, left_gray);
        return;
    }
    if (!hybrid.background_ready) return;
    if (!modern.enabled || !modern.tracker.background_ready()) return;

    const auto& fusion = modern.tracker.last_fusion();
    const auto& identity = modern.tracker.last_identity();
    const auto& anatomy = modern.tracker.last_anatomy_observation();
    const auto& mask = modern.tracker.selected_mask();

    // Hard ownership boundary: 2B.10B can refine only an already-published
    // modern fused identity. It can never invent/reacquire a fingertip itself.
    if (!fusion.publish || fusion.pixel_x < 0 || fusion.pixel_y < 0 ||
        !identity.palm_valid || mask.empty()) {
        evaluate_and_record_gate_v10c(hybrid, modern);
        return;
    }

    ++hybrid.attempts;
    const double palm_x =
        identity.palm_gx * kDepthScale + 1.0;
    const double palm_y =
        identity.palm_gy * kDepthScale + 1.0;

    hybrid.last = touchplus::tracking::refine_distal_tip_v10(
        left_gray,
        hybrid.background_left,
        kEyeWidth,
        kEyeHeight,
        mask,
        kDepthWidth,
        kDepthHeight,
        kDepthScale,
        fusion.pixel_x,
        fusion.pixel_y,
        palm_x,
        palm_y,
        anatomy.axis_dx,
        anatomy.axis_dy);

    if (!hybrid.last.accepted) {
        evaluate_and_record_gate_v10c(hybrid, modern);
        return;
    }

    ++hybrid.accepts;
    overlay_refined_candidate(heatmap_bgra, hybrid.last);

    // Evaluate B independently through the same robust stereo primitives used
    // by A. It remains ineligible until the unchanged gate and explicit 10D
    // runtime opt-in both allow one coherent same-frame sample.
    ++hybrid.shadow_attempts;
    const auto& surface = touchplus::surface::live_surface_model();
    hybrid.shadow = touchplus::tracking::evaluate_shadow_stereo_v10b(
        calibration,
        surface,
        left_gray,
        right_gray,
        workspace,
        mask,
        hybrid.last.refined_x,
        hybrid.last.refined_y,
        modern.tracker.identity_confidence());

    const bool a_valid = hybrid.accepted_a.fingertip_valid;
    const bool b_valid = hybrid.shadow.valid;
    if (b_valid) ++hybrid.shadow_valid;
    if (a_valid && b_valid) ++hybrid.both_valid;
    else if (a_valid) ++hybrid.a_only_valid;
    else if (b_valid) ++hybrid.b_only_valid;

    // 2B.10D selection happens only after both same-frame candidates and the
    // unchanged 2B.10C gate have been evaluated.
    evaluate_and_record_gate_v10c(hybrid, modern);
}

inline void maybe_report(RuntimeStateV10& hybrid) {
    ++hybrid.report_counter;
    if ((hybrid.report_counter % 30) != 0) return;

    print_contact_telemetry_v2c1("[CONTACT] heartbeat |", hybrid.contact);

    if (hybrid.background_learning) {
        std::cout
            << "[HYBRID] heartbeat | mode=2B.10D_GATED_PROMOTION"
            << " | background=LEARNING "
            << hybrid.background_frames << "/"
            << touchplus::tracking::kV5BackgroundFrames
            << " | promotion_mode=" << promotion_mode_name_v10d()
            << " selected_source=A OS_INJECTION=DISABLED\n";
        return;
    }
    if (!hybrid.background_ready) {
        std::cout
            << "[HYBRID] heartbeat | mode=2B.10D_GATED_PROMOTION"
            << " | background=NOT_READY | press B with clear still scene"
            << " | promotion_mode=" << promotion_mode_name_v10d()
            << " selected_source=A OS_INJECTION=DISABLED\n";
        return;
    }

    auto& modern = touchplus::depth::tracking_runtime_detail::state();
    const auto& a = hybrid.accepted_a;
    const auto& r = hybrid.last;
    const auto& b = hybrid.shadow;

    std::cout
        << std::fixed << std::setprecision(1)
        << "[HYBRID] heartbeat | mode=2B.10D_GATED_PROMOTION"
        << " | promotion_mode=" << promotion_mode_name_v10d()
        << " | refiner="
        << touchplus::tracking::distal_refine_status_name_v10(r.status)
        << " | coarse=" << r.coarse_x << "," << r.coarse_y
        << " refined=" << r.refined_x << "," << r.refined_y
        << " refiner_shift_px=" << r.shift_px
        << " forward_px=" << r.forward_px
        << " lateral_px=" << r.lateral_px
        << " | A=" << (a.fingertip_valid ? "VALID" : "INVALID")
        << " A_confidence=" << modern.tracker.stereo_confidence()
        << " A_support=" << a.refinement_support;

    if (a.fingertip_valid) {
        std::cout
            << " rawXYZ=(" << a.raw_tip.x_mm << "," << a.raw_tip.y_mm
            << ",H=" << a.raw_tip.h_mm << ")";
    }

    std::cout
        << " | B=" << (b.valid ? "VALID" : (b.attempted ? "INVALID" : "NOT_RUN"))
        << " B_confidence=" << b.stereo_confidence
        << " B_support=" << b.refinement_support
        << " reason=" << b.reason;

    if (b.valid) {
        std::cout
            << " rawXYZ=(" << b.raw_tip.x_mm << "," << b.raw_tip.y_mm
            << ",H=" << b.raw_tip.h_mm << ")";
    }

    if (a.fingertip_valid && b.valid) {
        const double dx = b.raw_tip.x_mm - a.raw_tip.x_mm;
        const double dy = b.raw_tip.y_mm - a.raw_tip.y_mm;
        const double dh = b.raw_tip.h_mm - a.raw_tip.h_mm;
        const double dxyz = std::sqrt(dx * dx + dy * dy + dh * dh);
        std::cout
            << " | BminusA=(dX=" << dx << ",dY=" << dy
            << ",dH=" << dh << ",dXYZ=" << dxyz << ")";
    }

    if (hybrid.gate_has_result) {
        std::cout
            << " | promotion_gate="
            << touchplus::tracking::promotion_decision_name_v10c(
                hybrid.gate.decision)
            << " gate_reason="
            << touchplus::tracking::promotion_reason_name_v10c(
                hybrid.gate.reason)
            << " shift_px=" << hybrid.gate.shift_2d_px
            << " dH=" << hybrid.gate.delta_h_mm
            << " dXYZ=" << hybrid.gate.delta_xyz_mm;
    } else {
        std::cout << " | promotion_gate=KEEP_A"
                  << " shift_px=nan dH=nan dXYZ=nan";
    }

    if (hybrid.selection_has_result) {
        std::cout
            << " | selected_source="
            << touchplus::tracking::authoritative_source_name_v10d(
                hybrid.selection.source)
            << " selected_reason=" << hybrid.selection.reason;
    } else {
        std::cout << " | selected_source=A selected_reason=GATE_NOT_EVALUATED";
    }

    std::cout
        << " | counts refine=" << hybrid.accepts << "/" << hybrid.attempts
        << " shadow=" << hybrid.shadow_valid << "/" << hybrid.shadow_attempts
        << " both=" << hybrid.both_valid
        << " A_only=" << hybrid.a_only_valid
        << " B_only=" << hybrid.b_only_valid
        << " gate=" << hybrid.gate_would_select_b << "/"
        << hybrid.gate_evaluations
        << " KEEP_A=" << hybrid.gate_keep_a
        << " WOULD_SELECT_B=" << hybrid.gate_would_select_b
        << " selected_A=" << hybrid.selection_stats.selected_a
        << " selected_B=" << hybrid.selection_stats.selected_b
        << " source_switches=" << hybrid.selection_stats.source_switches
        << " reasons={";

    bool first_reason = true;
    for (std::size_t i = 0; i < hybrid.gate_reason_counts.size(); ++i) {
        if (hybrid.gate_reason_counts[i] == 0) continue;
        if (!first_reason) std::cout << ",";
        first_reason = false;
        std::cout
            << touchplus::tracking::promotion_reason_name_v10c(
                static_cast<touchplus::tracking::PromotionReasonV10C>(i))
            << ":" << hybrid.gate_reason_counts[i];
    }

    std::cout
        << "}"
        << " | modern_identity=AUTHORITATIVE"
        << " OS_INJECTION=DISABLED\n";
}

} // namespace hybrid_refiner_runtime_detail_v10

inline void set_hybrid_promotion_enabled_v10d(bool enabled) {
    hybrid_refiner_runtime_detail_v10::promotion_enabled_config_v10d().store(
        enabled, std::memory_order_release);
}

inline bool hybrid_promotion_enabled_v10d() {
    return hybrid_refiner_runtime_detail_v10::promotion_enabled_v10d();
}

inline void compute_depth_heatmap_hybrid_v10_wrapper(
    const Calibration& c,
    const std::vector<uint8_t>& left,
    const std::vector<uint8_t>& right,
    DepthWorkspace& workspace) {

    // Run the exact accepted implementation first. In default OFF mode no later
    // operation mutates its A result or smoothing state.
    touchplus::depth::compute_depth_heatmap_fingertip_wrapper(
        c, left, right, workspace);

    auto& modern = tracking_runtime_detail::state();
    auto& hybrid = hybrid_refiner_runtime_detail_v10::state();
    hybrid_refiner_runtime_detail_v10::maybe_update_physical_label_v2c1a(hybrid);
    hybrid_refiner_runtime_detail_v10::run_refiner(
        hybrid, modern, c, left, right, workspace, workspace.heatmap_bgra);
    hybrid_refiner_runtime_detail_v10::update_contact_v2c1(hybrid, modern);
}

inline PointDepth point_depth_surface_runtime_hybrid_v10_wrapper(
    const Calibration& c,
    const std::vector<uint8_t>& left,
    const std::vector<uint8_t>& right,
    int cursor_x,
    int cursor_y) {

    auto& hybrid = hybrid_refiner_runtime_detail_v10::state();
    hybrid_refiner_runtime_detail_v10::maybe_start_background(hybrid);

    // Preserve all accepted Phase 2A/2B keyboard handling and cursor-depth
    // behavior. Fingertip promotion never enables contact or OS output.
    PointDepth result = touchplus::depth::point_depth_surface_runtime_wrapper(
        c, left, right, cursor_x, cursor_y);

    hybrid_refiner_runtime_detail_v10::maybe_report(hybrid);
    return result;
}

} // namespace touchplus::depth

#define point_depth point_depth_surface_runtime_hybrid_v10_wrapper
#define compute_depth_heatmap compute_depth_heatmap_hybrid_v10_wrapper
