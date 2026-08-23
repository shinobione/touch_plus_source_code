#include "fingertip_authoritative_selection_v10d.h"

#include <iostream>
#include <limits>
#include <string>

namespace {

using namespace touchplus::tracking;

bool expect(bool condition, const std::string& message) {
    if (condition) return true;
    std::cerr << "[FAIL] " << message << '\n';
    return false;
}

PromotionGateInputV10C eligible_gate_input() {
    PromotionGateInputV10C input;
    input.identity_accepted = true;
    input.identity_current = true;
    input.refiner_accepted = true;
    input.a_valid = true;
    input.b_valid = true;
    input.a_pixel_x = 307;
    input.a_pixel_y = 154;
    input.b_pixel_x = 300;
    input.b_pixel_y = 150;
    input.a_stereo_confidence = "MEDIUM";
    input.b_stereo_confidence = "HIGH";
    input.a_support = 4;
    input.b_support = 7;
    input.a_x_mm = 1.2;
    input.a_y_mm = -54.7;
    input.a_h_mm = 74.6;
    input.b_x_mm = 0.7;
    input.b_y_mm = -54.5;
    input.b_h_mm = 75.8;
    return input;
}

AuthoritativeSampleV10D a_sample() {
    return {true, 307, 154, 1.2, -54.7, 74.6, "MEDIUM", 4};
}

AuthoritativeSampleV10D b_sample() {
    return {true, 300, 150, 0.7, -54.5, 75.8, "HIGH", 7};
}

AuthoritativeSelectionV10D select_for(
    bool enabled,
    const PromotionGateInputV10C& input) {
    return select_authoritative_sample_v10d(
        enabled, evaluate_promotion_gate_v10c(input), a_sample(), b_sample());
}

bool test_off_would_select_b_keeps_a() {
    const auto selected = select_for(false, eligible_gate_input());
    return expect(selected.source == AuthoritativeSourceV10D::A,
                  "OFF + WOULD_SELECT_B must select A") &&
           expect(selected.reason == "PROMOTION_DISABLED",
                  "disabled selection must be explicit");
}

bool test_on_would_select_b_selects_b() {
    const auto selected = select_for(true, eligible_gate_input());
    return expect(selected.source == AuthoritativeSourceV10D::B,
                  "ON + WOULD_SELECT_B must select B") &&
           expect(selected.reason == "STRICT_EVIDENCE_GAIN",
                  "promoted B must retain the 2B.10C reason");
}

bool test_on_keep_a_selects_a() {
    auto input = eligible_gate_input();
    input.b_stereo_confidence = "MEDIUM";
    input.b_support = 3;
    return expect(select_for(true, input).source == AuthoritativeSourceV10D::A,
                  "ON + KEEP_A must select A");
}

bool test_b_only_keeps_a() {
    auto input = eligible_gate_input();
    input.a_valid = false;
    auto a = a_sample();
    a.valid = false;
    const auto gate = evaluate_promotion_gate_v10c(input);
    const auto selected = select_authoritative_sample_v10d(
        true, gate, a, b_sample());
    return expect(gate.reason == PromotionReasonV10C::BOnlyIneligible,
                  "B-only must be rejected by the unchanged gate") &&
           expect(selected.source == AuthoritativeSourceV10D::A,
                  "B-only must select A");
}

bool test_unknown_stale_and_non_current_keep_a() {
    auto unknown = eligible_gate_input();
    unknown.identity_accepted = false;
    auto stale = eligible_gate_input();
    stale.identity_stale = true;
    stale.identity_current = false;
    auto non_current = eligible_gate_input();
    non_current.identity_current = false;
    return expect(select_for(true, unknown).source == AuthoritativeSourceV10D::A,
                  "UNKNOWN identity must select A") &&
           expect(select_for(true, stale).source == AuthoritativeSourceV10D::A,
                  "stale identity must select A") &&
           expect(select_for(true, non_current).source == AuthoritativeSourceV10D::A,
                  "non-current identity must select A");
}

bool test_nonfinite_and_excessive_delta_keep_a() {
    auto nonfinite = eligible_gate_input();
    nonfinite.b_h_mm = std::numeric_limits<double>::quiet_NaN();
    auto excessive_metric = eligible_gate_input();
    excessive_metric.b_x_mm += 30.0;
    auto excessive_pixel = eligible_gate_input();
    excessive_pixel.b_pixel_x = excessive_pixel.a_pixel_x + 19;
    excessive_pixel.b_pixel_y = excessive_pixel.a_pixel_y;
    return expect(select_for(true, nonfinite).source == AuthoritativeSourceV10D::A,
                  "non-finite delta must select A") &&
           expect(select_for(true, excessive_metric).source == AuthoritativeSourceV10D::A,
                  "excessive metric delta must select A") &&
           expect(select_for(true, excessive_pixel).source == AuthoritativeSourceV10D::A,
                  "excessive pixel delta must select A");
}

bool test_inward_and_rejected_keep_a() {
    auto inward = eligible_gate_input();
    inward.refiner_accepted = false;
    inward.refiner_inward = true;
    auto rejected = eligible_gate_input();
    rejected.refiner_accepted = false;
    return expect(select_for(true, inward).source == AuthoritativeSourceV10D::A,
                  "inward refiner output must select A") &&
           expect(select_for(true, rejected).source == AuthoritativeSourceV10D::A,
                  "rejected refiner output must select A");
}

bool sample_matches(
    const AuthoritativeSampleV10D& actual,
    const AuthoritativeSampleV10D& expected) {
    return actual.valid == expected.valid &&
        actual.pixel_x == expected.pixel_x && actual.pixel_y == expected.pixel_y &&
        actual.x_mm == expected.x_mm && actual.y_mm == expected.y_mm &&
        actual.h_mm == expected.h_mm &&
        actual.stereo_confidence == expected.stereo_confidence &&
        actual.support == expected.support;
}

bool test_pixel_and_metric_are_atomic() {
    const auto gate = evaluate_promotion_gate_v10c(eligible_gate_input());
    const auto selected_a = select_authoritative_sample_v10d(
        false, gate, a_sample(), b_sample());
    const auto selected_b = select_authoritative_sample_v10d(
        true, gate, a_sample(), b_sample());
    return expect(sample_matches(selected_a.sample, a_sample()),
                  "A selection must copy all A pixel/metric/evidence fields") &&
           expect(sample_matches(selected_b.sample, b_sample()),
                  "B selection must copy all B pixel/metric/evidence fields");
}

bool test_smoothing_and_counters_are_gated() {
    PromotionSmootherV10D smoother;
    const bool consumed_off = consume_selected_sample_v10d(
        false, b_sample(), smoother);
    const bool consumed_on = consume_selected_sample_v10d(
        true, b_sample(), smoother);

    PromotionSelectionStatsV10D stats;
    record_authoritative_selection_v10d(AuthoritativeSourceV10D::A, stats);
    record_authoritative_selection_v10d(AuthoritativeSourceV10D::B, stats);
    record_authoritative_selection_v10d(AuthoritativeSourceV10D::B, stats);
    record_authoritative_selection_v10d(AuthoritativeSourceV10D::A, stats);

    return expect(!consumed_off && consumed_on && smoother.consumed_samples == 1,
                  "smoothing may consume B only when promotion is enabled") &&
           expect(stats.selected_a == 2 && stats.selected_b == 2 &&
                      stats.source_switches == 2,
                  "selection counters and source switches must be cumulative") &&
           expect(!kOsInjectionEnabledV10D,
                  "OS injection must remain disabled");
}

} // namespace

int main() {
    bool ok = true;
    ok &= test_off_would_select_b_keeps_a();
    ok &= test_on_would_select_b_selects_b();
    ok &= test_on_keep_a_selects_a();
    ok &= test_b_only_keeps_a();
    ok &= test_unknown_stale_and_non_current_keep_a();
    ok &= test_nonfinite_and_excessive_delta_keep_a();
    ok &= test_inward_and_rejected_keep_a();
    ok &= test_pixel_and_metric_are_atomic();
    ok &= test_smoothing_and_counters_are_gated();

    if (!ok) return 1;
    std::cout << "[PASS] Phase 2B.10D gated authoritative promotion regression\n";
    return 0;
}
