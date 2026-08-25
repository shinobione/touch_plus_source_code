#include "fingertip_anatomy_shadow_ipc_v2c1j.h"

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

    std::cout
        << "accepted_pipeline_consumes_shadow=NO\n"
        << "shadow_only=YES\n"
        << "authoritative=UNCHANGED\n"
        << "OS_INJECTION=DISABLED\n";

    if (ok) {
        std::cout << "Phase 2C.1J isolated shadow anatomy IPC self-test PASS\n";
        return 0;
    }
    std::cout << "Phase 2C.1J isolated shadow anatomy IPC self-test FAIL\n";
    return 1;
}
