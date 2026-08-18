#include <windows.h>
#include <iomanip>
#include <iostream>

#include "eSPAEAWBCtrl.h"

namespace {

int legacy_swunlock_and_init(WORD app_id) {
    const int unlock = eSPAEAWB_SWUnlock(app_id);
    if (unlock != ESPAEAWB_RET_OK) {
        return unlock;
    }

    std::cout << "\n  Applying recovered Ractiv sensor initializer atomically...\n";

    auto run = [](const char* name, int code) -> int {
        std::cout << "  " << std::left << std::setw(32) << name;
        if (code == ESPAEAWB_RET_OK) {
            std::cout << "OK\n";
        } else {
            std::cout << "RET=" << code << "\n";
        }
        return code;
    };

    int first_error = ESPAEAWB_RET_OK;
    auto keep = [&](const char* name, int code) {
        run(name, code);
        if (first_error == ESPAEAWB_RET_OK && code != ESPAEAWB_RET_OK) {
            first_error = code;
        }
    };

    keep("DisableAE", eSPAEAWB_DisableAE());
    keep("DisableAWB", eSPAEAWB_DisableAWB());

    BYTE gpio = 0;
    const int gpio_read = eSPAEAWB_GetGPIOValue(1, &gpio);
    keep("GetGPIOValue(1)", gpio_read);
    if (gpio_read == ESPAEAWB_RET_OK) {
        gpio = static_cast<BYTE>(gpio | 0x08);
        keep("LEDs ON / SetGPIOValue", eSPAEAWB_SetGPIOValue(1, gpio));
    }

    keep("Exposure both = 15 ms",
         eSPAEAWB_SetExposureTime(ESPAEAWB_SENSOR_MODE_BOTH, 15.0f));
    keep("Global gain left = 1",
         eSPAEAWB_SetGlobalGain(ESPAEAWB_SENSOR_MODE_LEFT, 1.0f));
    keep("Global gain right = 1",
         eSPAEAWB_SetGlobalGain(ESPAEAWB_SENSOR_MODE_RIGHT, 1.0f));
    keep("Color gain left = 2/1/2",
         eSPAEAWB_SetColorGain(ESPAEAWB_SENSOR_MODE_LEFT, 2.0f, 1.0f, 2.0f));
    keep("Color gain right = 2/1/2",
         eSPAEAWB_SetColorGain(ESPAEAWB_SENSOR_MODE_RIGHT, 2.0f, 1.0f, 2.0f));

    if (first_error == ESPAEAWB_RET_OK) {
        std::cout << "  Legacy sensor init: PASS\n\n";
    } else {
        std::cout << "  Legacy sensor init: FAIL (first RET=" << first_error << ")\n\n";
    }
    return first_error;
}

} // namespace

// Reuse the already-validated legacy DirectShow timing probe, but intercept its
// SWUnlock call so the historical sensor initializer is applied immediately
// afterwards, before the DirectShow graph is created. This gives us an atomic
// A/B against the minimal-unlock probe without a second process in between.
#define eSPAEAWB_SWUnlock(app_id) legacy_swunlock_and_init(app_id)
#include "directshow_timing_probe.cpp"
