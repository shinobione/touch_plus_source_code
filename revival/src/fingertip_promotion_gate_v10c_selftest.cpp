#include "fingertip_promotion_gate_v10c.h"

#include <iostream>
#include <limits>
#include <string>

namespace {

using touchplus::tracking::PromotionDecisionV10C;
using touchplus::tracking::PromotionGateInputV10C;
using touchplus::tracking::PromotionReasonV10C;
using touchplus::tracking::evaluate_promotion_gate_v10c;

bool expect(bool condition, const std::string& message) {
    if (condition) return true;
    std::cerr << "[FAIL] " << message << '\n';
    return false;
}

PromotionGateInputV10C eligible_input() {
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

bool test_b_better() {
    const auto out = evaluate_promotion_gate_v10c(eligible_input());
    auto support_only = eligible_input();
    support_only.a_stereo_confidence = "HIGH";
    support_only.b_stereo_confidence = "HIGH";
    support_only.a_support = 4;
    support_only.b_support = 5;
    const auto support_only_out = evaluate_promotion_gate_v10c(support_only);

    return expect(out.decision == PromotionDecisionV10C::WouldSelectB,
                  "strict confidence/support gain should be counterfactually eligible") &&
           expect(out.reason == PromotionReasonV10C::StrictEvidenceGain,
                  "eligible B should report STRICT_EVIDENCE_GAIN") &&
           expect(support_only_out.decision == PromotionDecisionV10C::WouldSelectB,
                  "strict support-only gain without confidence regression should be eligible");
}

bool test_b_inferior() {
    auto input = eligible_input();
    input.b_stereo_confidence = "MEDIUM";
    input.b_support = 3;
    const auto out = evaluate_promotion_gate_v10c(input);
    return expect(out.decision == PromotionDecisionV10C::KeepA,
                  "inferior B evidence must keep A") &&
           expect(out.reason == PromotionReasonV10C::EvidenceNotStrictlyBetter,
                  "inferior B should report evidence rejection");
}

bool test_b_only() {
    auto input = eligible_input();
    input.a_valid = false;
    const auto out = evaluate_promotion_gate_v10c(input);
    return expect(out.decision == PromotionDecisionV10C::KeepA,
                  "B-only must never be eligible") &&
           expect(out.reason == PromotionReasonV10C::BOnlyIneligible,
                  "B-only should have an explicit reason");
}

bool test_identity_unknown_and_stale() {
    auto unknown = eligible_input();
    unknown.identity_accepted = false;
    const auto unknown_out = evaluate_promotion_gate_v10c(unknown);

    auto stale = eligible_input();
    stale.identity_stale = true;
    stale.identity_current = false;
    const auto stale_out = evaluate_promotion_gate_v10c(stale);

    auto not_current = eligible_input();
    not_current.identity_current = false;
    const auto not_current_out = evaluate_promotion_gate_v10c(not_current);

    return expect(unknown_out.decision == PromotionDecisionV10C::KeepA &&
                      unknown_out.reason == PromotionReasonV10C::IdentityUnknown,
                  "UNKNOWN identity must keep A") &&
           expect(stale_out.decision == PromotionDecisionV10C::KeepA &&
                      stale_out.reason == PromotionReasonV10C::IdentityStale,
                  "stale identity must keep A") &&
           expect(not_current_out.decision == PromotionDecisionV10C::KeepA &&
                      not_current_out.reason == PromotionReasonV10C::IdentityNotCurrent,
                  "non-current identity must keep A");
}

bool test_non_finite_metric_delta() {
    auto input = eligible_input();
    input.b_h_mm = std::numeric_limits<double>::quiet_NaN();
    const auto out = evaluate_promotion_gate_v10c(input);
    return expect(out.decision == PromotionDecisionV10C::KeepA,
                  "non-finite metric delta must keep A") &&
           expect(out.reason == PromotionReasonV10C::NonFiniteMetricDelta,
                  "non-finite metric delta should have an explicit reason");
}

bool test_excessive_metric_delta() {
    auto input = eligible_input();
    input.b_x_mm = input.a_x_mm + 30.0;
    const auto out = evaluate_promotion_gate_v10c(input);
    return expect(out.decision == PromotionDecisionV10C::KeepA,
                  "excessive metric delta must keep A") &&
           expect(out.reason == PromotionReasonV10C::ExcessiveMetricDelta,
                  "excessive metric delta should have an explicit reason");
}

bool test_excessive_2d_delta() {
    auto input = eligible_input();
    input.b_pixel_x = input.a_pixel_x + 19;
    input.b_pixel_y = input.a_pixel_y;
    const auto out = evaluate_promotion_gate_v10c(input);
    return expect(out.decision == PromotionDecisionV10C::KeepA,
                  "excessive 2D delta must keep A") &&
           expect(out.reason == PromotionReasonV10C::Excessive2DDelta,
                  "excessive 2D delta should have an explicit reason");
}

bool test_inward_and_rejected_refiner() {
    auto inward = eligible_input();
    inward.refiner_accepted = false;
    inward.refiner_inward = true;
    const auto inward_out = evaluate_promotion_gate_v10c(inward);

    auto rejected = eligible_input();
    rejected.refiner_accepted = false;
    const auto rejected_out = evaluate_promotion_gate_v10c(rejected);

    return expect(inward_out.decision == PromotionDecisionV10C::KeepA &&
                      inward_out.reason == PromotionReasonV10C::RefinerInward,
                  "inward refinement must keep A") &&
           expect(rejected_out.decision == PromotionDecisionV10C::KeepA &&
                      rejected_out.reason == PromotionReasonV10C::RefinerRejected,
                  "rejected refiner must keep A");
}

} // namespace

int main() {
    bool ok = true;
    ok &= test_b_better();
    ok &= test_b_inferior();
    ok &= test_b_only();
    ok &= test_identity_unknown_and_stale();
    ok &= test_non_finite_metric_delta();
    ok &= test_excessive_metric_delta();
    ok &= test_excessive_2d_delta();
    ok &= test_inward_and_rejected_refiner();

    if (!ok) return 1;
    std::cout << "[PASS] Phase 2B.10C counterfactual promotion gate regression\n";
    return 0;
}
