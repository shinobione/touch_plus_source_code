#include "fingertip_anatomy_shadow_ipc_v2c1j.h"
#include "shadow_anatomy_probe_policy_v2c1j1.h"

#include <iostream>
#include <string_view>

namespace {

bool expect(bool condition, const char* label) {
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << label << '\n';
    return condition;
}

} // namespace

int main() {
    using namespace touchplus::tracking;
    using namespace touchplus::tracking::shadow_probe_v2c1j1;

    bool ok = true;
    const std::wstring_view accepted_frame{kAnatomyFrameMapNameV9};
    const std::wstring_view accepted_result{kAnatomyResultMapNameV9};
    const std::wstring_view shadow_frame{kShadowAnatomyFrameMapNameV2C1J};
    const std::wstring_view shadow_result{kShadowAnatomyResultMapNameV2C1J};

    ok &= expect(shadow_frame != accepted_frame,
        "shadow frame IPC is distinct from accepted anatomy frame IPC");
    ok &= expect(shadow_result != accepted_result,
        "shadow result IPC is distinct from accepted anatomy result IPC");
    ok &= expect(shadow_frame != shadow_result,
        "shadow frame/result IPC names are distinct");
    ok &= expect(sizeof(AnatomyFrameHeaderV9) == 64,
        "shared frame ABI remains 64 bytes");
    ok &= expect(sizeof(AnatomyResultPacketV9) == 96,
        "shared result ABI remains 96 bytes");

    ShadowAnatomyObservationV2C1J observation;
    ok &= expect(observation.status == AnatomyStatusV9::Unavailable &&
                 observation.tip_x == -1 && observation.tip_y == -1,
        "shadow observation defaults fail closed");

    ok &= expect(kProbePeriodFrames == 6,
        "2C.1J.1 shadow probe cadence is exactly one frame in six");
    ok &= expect(precheck(12, true, true, true) == ProbeGate::AcceptedHandValid,
        "healthy accepted hand suppresses shadow probing entirely");
    ok &= expect(precheck(12, false, false, true) == ProbeGate::BackgroundNotReady,
        "background-not-ready suppresses shadow probing");
    ok &= expect(precheck(12, true, false, false) == ProbeGate::AcceptedAnatomyNotUnavailable,
        "non-UNAVAILABLE accepted anatomy suppresses shadow probing");
    ok &= expect(precheck(13, true, false, true) == ProbeGate::Throttled,
        "hand-loss frames between probe slots are throttled");
    ok &= expect(precheck(12, true, false, true) == ProbeGate::Due,
        "hand-loss plus UNAVAILABLE anatomy reaches a scheduled probe slot");
    ok &= expect(validate_mask(ProbeGate::Due, kMinMaskCells - 1) == ProbeGate::MaskTooSmall,
        "tiny appearance masks fail closed before shadow inference");
    ok &= expect(validate_mask(ProbeGate::Due, kMaxMaskCells + 1) == ProbeGate::MaskTooLarge,
        "oversized appearance masks fail closed before shadow inference");
    ok &= expect(validate_mask(ProbeGate::Due, 2500) == ProbeGate::Due,
        "plausible hand-loss mask permits a shadow probe");

    std::cout
        << "probe_policy=HAND_LOSS_ONLY_1_IN_6\n"
        << "accepted_pipeline_consumes_shadow=NO\n"
        << "shadow_only=YES\n"
        << "authoritative=UNCHANGED\n"
        << "OS_INJECTION=DISABLED\n";

    if (ok) {
        std::cout << "Phase 2C.1J.1 isolated lightweight shadow anatomy self-test PASS\n";
        return 0;
    }
    std::cout << "Phase 2C.1J.1 isolated lightweight shadow anatomy self-test FAIL\n";
    return 1;
}
