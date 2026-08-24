#include "contact_proxy_shadow_v2c1h.h"

#include <cstdlib>
#include <iostream>

namespace {

using touchplus::contact_shadow::ShadowContactEventV2C1H;
using touchplus::contact_shadow::ShadowContactInputV2C1H;
using touchplus::contact_shadow::ShadowContactProxyV2C1H;
using touchplus::contact_shadow::TargetSourceV2C1H;

[[noreturn]] void fail(const char* message) {
    std::cerr << "Phase 2C.1H self-test FAIL: " << message << '\n';
    std::exit(1);
}

ShadowContactInputV2C1H sample(
    std::uint32_t frame,
    double p25,
    double median,
    int x,
    int y,
    TargetSourceV2C1H source = TargetSourceV2C1H::Fused,
    int count = 20) {
    ShadowContactInputV2C1H in;
    in.sample_valid = true;
    in.frame = frame;
    in.source = source;
    in.target_x = x;
    in.target_y = y;
    in.raw_dense_count = count;
    in.h_p25_mm = p25;
    in.h_median_mm = median;
    return in;
}

void test_physical_shape_reaches_shadow_contact_once() {
    ShadowContactProxyV2C1H proxy;
    std::uint32_t frame = 1;
    int down_events = 0;

    for (int i = 0; i < 7; ++i) {
        const auto out = proxy.update(sample(frame++, 89.0, 92.0, 320, 210 + i));
        if (out.would_contact) fail("HIGH hover produced contact");
    }
    for (int i = 0; i < 7; ++i) {
        const double median = 48.0 - i * 2.0;
        const auto out = proxy.update(sample(frame++, median - 2.0, median, 320, 225 + i * 2));
        if (out.would_contact) fail("NEAR descent produced premature contact");
    }
    for (int i = 0; i < 9; ++i) {
        const double median = 21.0 + (i % 2 ? 0.6 : 0.0);
        const auto out = proxy.update(sample(
            frame++, median - 2.5, median, 321 + (i % 2), 239 + (i % 2),
            i >= 3 ? TargetSourceV2C1H::Anatomy : TargetSourceV2C1H::Fused));
        if (out.event == ShadowContactEventV2C1H::WouldDown) ++down_events;
    }
    if (down_events != 1) fail("terminal approach did not produce exactly one WOULD_DOWN");
}

void test_low_hover_without_approach_never_contacts() {
    ShadowContactProxyV2C1H proxy;
    for (std::uint32_t frame = 1; frame <= 20; ++frame) {
        const auto out = proxy.update(sample(frame, 20.0, 22.0, 320, 240));
        if (out.would_contact || out.event == ShadowContactEventV2C1H::WouldDown) {
            fail("static low hover produced contact without recent approach");
        }
    }
}

void test_contaminated_distribution_is_rejected() {
    ShadowContactProxyV2C1H proxy;
    std::uint32_t frame = 1;
    for (int i = 0; i < 8; ++i) {
        proxy.update(sample(frame++, 70.0, 74.0, 320, 210 + i));
    }
    for (int i = 0; i < 10; ++i) {
        const auto out = proxy.update(sample(frame++, -36.0, 8.1, 322, 242));
        if (out.would_contact) fail("contaminated negative-p25 distribution produced contact");
    }
}

void test_geometry_only_never_contacts() {
    ShadowContactProxyV2C1H proxy;
    std::uint32_t frame = 1;
    for (int i = 0; i < 8; ++i) {
        proxy.update(sample(frame++, 70.0, 74.0, 320, 210 + i));
    }
    for (int i = 0; i < 10; ++i) {
        const auto out = proxy.update(sample(
            frame++, 18.0, 21.0, 321, 240,
            TargetSourceV2C1H::Geometry));
        if (out.would_contact) fail("GEOMETRY-only target produced contact");
    }
}

void test_latched_contact_releases_safely() {
    ShadowContactProxyV2C1H proxy;
    std::uint32_t frame = 1;
    bool latched = false;
    for (int i = 0; i < 7; ++i) proxy.update(sample(frame++, 72.0, 76.0, 320, 210 + i));
    for (int i = 0; i < 7; ++i) {
        const double median = 45.0 - i * 2.5;
        proxy.update(sample(frame++, median - 2.0, median, 320, 225 + i * 2));
    }
    for (int i = 0; i < 8; ++i) {
        const auto out = proxy.update(sample(frame++, 18.5, 21.0, 321, 240));
        latched = latched || out.would_contact;
    }
    if (!latched) fail("release test never reached contact latch");

    int up_events = 0;
    for (int i = 0; i < 3; ++i) {
        const auto out = proxy.update(sample(frame++, 44.0, 50.0, 321, 225));
        if (out.event == ShadowContactEventV2C1H::WouldUp) ++up_events;
    }
    if (up_events != 1) fail("lift did not produce exactly one WOULD_UP");
}

void test_geometry_identity_loss_releases_immediately() {
    ShadowContactProxyV2C1H proxy;
    std::uint32_t frame = 1;
    for (int i = 0; i < 7; ++i) proxy.update(sample(frame++, 72.0, 76.0, 320, 210 + i));
    for (int i = 0; i < 7; ++i) {
        const double median = 45.0 - i * 2.5;
        proxy.update(sample(frame++, median - 2.0, median, 320, 225 + i * 2));
    }
    bool latched = false;
    for (int i = 0; i < 8; ++i) {
        latched = proxy.update(sample(frame++, 18.5, 21.0, 321, 240)).would_contact || latched;
    }
    if (!latched) fail("identity-loss test never reached latch");

    const auto out = proxy.update(sample(
        frame++, 18.0, 21.0, 321, 240,
        TargetSourceV2C1H::Geometry));
    if (out.would_contact || out.event != ShadowContactEventV2C1H::WouldUp) {
        fail("GEOMETRY identity downgrade did not fail-safe release");
    }
}

} // namespace

int main() {
    test_physical_shape_reaches_shadow_contact_once();
    test_low_hover_without_approach_never_contacts();
    test_contaminated_distribution_is_rejected();
    test_geometry_only_never_contacts();
    test_latched_contact_releases_safely();
    test_geometry_identity_loss_releases_immediately();
    std::cout << "Phase 2C.1H shadow contact proxy self-test PASS\n";
    return 0;
}
