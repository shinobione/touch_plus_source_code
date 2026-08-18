#pragma once

#include <objbase.h>

// The recovered Etron Win32 SDK may initialize COM on the calling thread before
// Phase 0C reaches Media Foundation. If it chose a different apartment model,
// a later CoInitializeEx(..., COINIT_MULTITHREADED) returns RPC_E_CHANGED_MODE.
// That does not mean COM is unavailable; it means this thread is already
// initialized in another apartment. Reuse that apartment for this short-lived
// diagnostic process instead of treating the result as fatal.
namespace touchplus_com_compat {
inline thread_local bool owns_com_initialization = false;

inline HRESULT initialize(LPVOID reserved, DWORD flags) {
    const HRESULT hr = ::CoInitializeEx(reserved, flags);
    if (hr == RPC_E_CHANGED_MODE) {
        // Etron already initialized this thread using another apartment model.
        // Report non-failure to the existing RAII wrapper, but do NOT later call
        // CoUninitialize for an initialization count we did not acquire.
        owns_com_initialization = false;
        return S_FALSE;
    }

    if (SUCCEEDED(hr)) {
        // Both S_OK and S_FALSE from the real CoInitializeEx require a matching
        // CoUninitialize call.
        owns_com_initialization = true;
    }
    return hr;
}

inline void uninitialize() {
    if (owns_com_initialization) {
        owns_com_initialization = false;
        ::CoUninitialize();
    }
}
}  // namespace touchplus_com_compat

// atomic_probe.cpp already owns the RAII call sites. Redirect just those calls
// without changing the rest of the recovered/vendor stack.
#define CoInitializeEx touchplus_com_compat::initialize
#define CoUninitialize touchplus_com_compat::uninitialize
