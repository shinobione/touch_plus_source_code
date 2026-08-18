#include <windows.h>
#include <cfgmgr32.h>
#include <setupapi.h>

#include <algorithm>
#include <cwctype>
#include <iostream>
#include <string>
#include <vector>

#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "setupapi.lib")

namespace {

constexpr wchar_t kTouchPlusVidPid[] = L"vid_1e4e&pid_0107";
constexpr wchar_t kDescriptorFailure[] = L"device_descriptor_failure";
constexpr wchar_t kUsbUnknown[] = L"usb\\unknown";
constexpr wchar_t kZeroVidPid[] = L"vid_0000&pid_0002";
constexpr ULONG kCode43 = 43;

std::wstring lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return value;
}

std::wstring get_instance_id(HDEVINFO devices, SP_DEVINFO_DATA& data) {
    DWORD required = 0;
    SetupDiGetDeviceInstanceIdW(devices, &data, nullptr, 0, &required);
    if (required == 0) {
        return L"";
    }

    std::vector<wchar_t> buffer(required + 1, L'\0');
    if (!SetupDiGetDeviceInstanceIdW(devices, &data, buffer.data(),
                                     static_cast<DWORD>(buffer.size()), nullptr)) {
        return L"";
    }
    return buffer.data();
}

std::wstring get_property(HDEVINFO devices, SP_DEVINFO_DATA& data, DWORD property) {
    DWORD type = 0;
    DWORD required = 0;
    SetupDiGetDeviceRegistryPropertyW(devices, &data, property, &type, nullptr, 0, &required);
    if (required == 0) {
        return L"";
    }

    std::vector<BYTE> raw(required + sizeof(wchar_t) * 2, 0);
    if (!SetupDiGetDeviceRegistryPropertyW(devices, &data, property, &type,
                                           raw.data(), static_cast<DWORD>(raw.size()), nullptr)) {
        return L"";
    }

    const auto* text = reinterpret_cast<const wchar_t*>(raw.data());
    if (type == REG_MULTI_SZ) {
        std::wstring joined;
        const wchar_t* current = text;
        while (*current != L'\0') {
            if (!joined.empty()) {
                joined += L" | ";
            }
            joined += current;
            current += std::wcslen(current) + 1;
        }
        return joined;
    }

    return text;
}

std::wstring get_parent_instance_id(DEVINST devinst) {
    DEVINST parent = 0;
    if (CM_Get_Parent(&parent, devinst, 0) != CR_SUCCESS) {
        return L"";
    }

    ULONG length = 0;
    if (CM_Get_Device_ID_Size(&length, parent, 0) != CR_SUCCESS) {
        return L"";
    }

    std::vector<wchar_t> buffer(length + 1, L'\0');
    if (CM_Get_Device_IDW(parent, buffer.data(), static_cast<ULONG>(buffer.size()), 0) != CR_SUCCESS) {
        return L"";
    }
    return buffer.data();
}

struct ProblemState {
    bool available = false;
    ULONG status = 0;
    ULONG code = 0;
};

ProblemState get_problem_state(DEVINST devinst) {
    ProblemState state;
    if (CM_Get_DevNode_Status(&state.status, &state.code, devinst, 0) == CR_SUCCESS) {
        state.available = true;
    }
    return state;
}

bool contains(const std::wstring& haystack, const wchar_t* needle) {
    return lower(haystack).find(needle) != std::wstring::npos;
}

bool looks_like_descriptor_failure(const std::wstring& instance,
                                   const std::wstring& hardware_ids,
                                   const ProblemState& problem) {
    const std::wstring combined = lower(instance + L" " + hardware_ids);
    return combined.find(kDescriptorFailure) != std::wstring::npos ||
           combined.find(kUsbUnknown) != std::wstring::npos ||
           combined.find(kZeroVidPid) != std::wstring::npos ||
           (problem.available && problem.code == kCode43 &&
            combined.find(L"usb\\") != std::wstring::npos);
}

void print_field(const wchar_t* label, const std::wstring& value) {
    if (!value.empty()) {
        std::wcout << L"    " << label << L": " << value << L"\n";
    }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    const bool show_all = argc > 1 && std::wstring(argv[1]) == L"--all";

    std::wcout << L"TouchPlus Revival - USB/PnP preflight\n";
    std::wcout << L"Looking below the camera stack for USB enumeration failures.\n\n";

    HDEVINFO devices = SetupDiGetClassDevsW(nullptr, L"USB", nullptr,
                                             DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (devices == INVALID_HANDLE_VALUE) {
        std::wcerr << L"SetupDiGetClassDevsW failed. Win32 error: " << GetLastError() << L"\n";
        return 1;
    }

    unsigned total = 0;
    unsigned interesting = 0;
    unsigned descriptor_failures = 0;
    unsigned touchplus_matches = 0;

    for (DWORD index = 0;; ++index) {
        SP_DEVINFO_DATA data{};
        data.cbSize = sizeof(data);
        if (!SetupDiEnumDeviceInfo(devices, index, &data)) {
            if (GetLastError() == ERROR_NO_MORE_ITEMS) {
                break;
            }
            std::wcerr << L"SetupDiEnumDeviceInfo failed at index " << index
                       << L". Win32 error: " << GetLastError() << L"\n";
            SetupDiDestroyDeviceInfoList(devices);
            return 1;
        }

        ++total;
        const std::wstring instance = get_instance_id(devices, data);
        const std::wstring hardware_ids = get_property(devices, data, SPDRP_HARDWAREID);
        const std::wstring description = get_property(devices, data, SPDRP_DEVICEDESC);
        const std::wstring friendly = get_property(devices, data, SPDRP_FRIENDLYNAME);
        const std::wstring location = get_property(devices, data, SPDRP_LOCATION_INFORMATION);
        const std::wstring service = get_property(devices, data, SPDRP_SERVICE);
        const std::wstring parent = get_parent_instance_id(data.DevInst);
        const ProblemState problem = get_problem_state(data.DevInst);

        const std::wstring identity = instance + L" " + hardware_ids + L" " + description + L" " + friendly;
        const bool is_touchplus = contains(identity, kTouchPlusVidPid);
        const bool descriptor_failure = looks_like_descriptor_failure(instance, hardware_ids, problem);
        const bool has_problem = problem.available && problem.code != 0;

        if (is_touchplus) {
            ++touchplus_matches;
        }
        if (descriptor_failure) {
            ++descriptor_failures;
        }

        if (!show_all && !is_touchplus && !descriptor_failure && !has_problem) {
            continue;
        }

        ++interesting;
        if (is_touchplus) {
            std::wcout << L"[TOUCH+]";
        } else if (descriptor_failure) {
            std::wcout << L"[USB-FAIL]";
        } else {
            std::wcout << L"[PROBLEM]";
        }
        std::wcout << L" USB devnode #" << index << L"\n";

        print_field(L"Description", !friendly.empty() ? friendly : description);
        print_field(L"Instance", instance);
        print_field(L"Hardware IDs", hardware_ids);
        print_field(L"Location", location);
        print_field(L"Service", service);
        print_field(L"Parent", parent);
        if (problem.available) {
            std::wcout << L"    Problem code: " << problem.code;
            if (problem.code == kCode43) {
                std::wcout << L" (Code 43)";
            }
            std::wcout << L"\n";
        }
        std::wcout << L"\n";
    }

    SetupDiDestroyDeviceInfoList(devices);

    std::wcout << L"USB devnodes present:        " << total << L"\n";
    std::wcout << L"Interesting/problem nodes:  " << interesting << L"\n";
    std::wcout << L"Touch+ VID/PID matches:      " << touchplus_matches << L"\n";
    std::wcout << L"Descriptor-failure suspects:" << L" " << descriptor_failures << L"\n\n";

    if (touchplus_matches > 0) {
        std::wcout << L"RESULT: Touch+ identity is visible to Windows at the PnP layer.\n";
        std::wcout << L"Next step: investigate why/if it does not reach the UVC camera layer.\n";
        return 0;
    }

    if (descriptor_failures > 0) {
        std::wcout << L"RESULT: Windows sees at least one USB device failing before a usable VID/PID is available.\n";
        std::wcout << L"CORRELATION TEST: run this once with Touch+ unplugged, then again immediately after plugging it in.\n";
        std::wcout << L"If the [USB-FAIL] entry appears/disappears with the Touch+, we have identified the sensor.\n";
        return 2;
    }

    std::wcout << L"RESULT: no Touch+ identity and no current USB descriptor-failure node found.\n";
    std::wcout << L"Use --all to print every present USB devnode.\n";
    return 3;
}
