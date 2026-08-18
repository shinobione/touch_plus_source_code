#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cwctype>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "EtronDI_O.h"
#include "eSPAEAWBCtrl.h"
#include "com_compat.h"

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kTouchPlusVidPid[] = L"vid_1e4e&pid_0107";
constexpr wchar_t kTouchPlusFriendlyName[] = L"touch+ camera";
constexpr UINT32 kStereoWidth = 1280;
constexpr UINT32 kStereoHeight = 480;
constexpr UINT32 kEyeWidth = 640;
constexpr UINT32 kEyeHeight = 480;
constexpr wchar_t kWindowClass[] = L"TouchPlusRevivalStereoViewer";

std::vector<BYTE> g_frame(static_cast<size_t>(kStereoWidth) * kStereoHeight * 4);
bool g_have_frame = false;
bool g_running = true;
std::wstring g_status = L"Waiting for Touch+ frames...";

HDC g_back_dc = nullptr;
HBITMAP g_back_bitmap = nullptr;
HGDIOBJ g_back_old_bitmap = nullptr;
int g_back_width = 0;
int g_back_height = 0;

void check_hr(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        std::ostringstream oss;
        oss << operation << " failed (HRESULT 0x" << std::hex << std::uppercase
            << static_cast<unsigned long>(hr) << ")";
        throw std::runtime_error(oss.str());
    }
}

void check_etron(int code, const char* operation) {
    std::cout << std::left << std::setw(34) << operation;
    if (code == ESPAEAWB_RET_OK) {
        std::cout << "OK\n";
        return;
    }
    std::cout << "RET=" << code << "\n";
    std::ostringstream oss;
    oss << operation << " failed with Etron return code " << code;
    throw std::runtime_error(oss.str());
}

std::wstring lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return value;
}

std::wstring get_string(IMFActivate* activate, REFGUID key) {
    wchar_t* raw = nullptr;
    UINT32 length = 0;
    const HRESULT hr = activate->GetAllocatedString(key, &raw, &length);
    if (FAILED(hr) || raw == nullptr) {
        return L"";
    }
    std::wstring result(raw, length);
    CoTaskMemFree(raw);
    return result;
}

std::string fourcc_from_guid(const GUID& guid) {
    const DWORD value = guid.Data1;
    char chars[5] = {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
        static_cast<char>((value >> 16) & 0xff),
        static_cast<char>((value >> 24) & 0xff),
        '\0'
    };
    for (int i = 0; i < 4; ++i) {
        const unsigned char c = static_cast<unsigned char>(chars[i]);
        if (c < 32 || c > 126) {
            return "GUID";
        }
    }
    return std::string(chars);
}

void release_backbuffer() {
    if (g_back_dc != nullptr) {
        if (g_back_old_bitmap != nullptr) {
            SelectObject(g_back_dc, g_back_old_bitmap);
        }
        if (g_back_bitmap != nullptr) {
            DeleteObject(g_back_bitmap);
        }
        DeleteDC(g_back_dc);
    }

    g_back_dc = nullptr;
    g_back_bitmap = nullptr;
    g_back_old_bitmap = nullptr;
    g_back_width = 0;
    g_back_height = 0;
}

bool ensure_backbuffer(HDC reference_dc, int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (g_back_dc != nullptr && g_back_bitmap != nullptr &&
        g_back_width == width && g_back_height == height) {
        return true;
    }

    release_backbuffer();

    g_back_dc = CreateCompatibleDC(reference_dc);
    if (g_back_dc == nullptr) {
        return false;
    }

    g_back_bitmap = CreateCompatibleBitmap(reference_dc, width, height);
    if (g_back_bitmap == nullptr) {
        DeleteDC(g_back_dc);
        g_back_dc = nullptr;
        return false;
    }

    g_back_old_bitmap = SelectObject(g_back_dc, g_back_bitmap);
    if (g_back_old_bitmap == nullptr || g_back_old_bitmap == HGDI_ERROR) {
        DeleteObject(g_back_bitmap);
        DeleteDC(g_back_dc);
        g_back_bitmap = nullptr;
        g_back_dc = nullptr;
        g_back_old_bitmap = nullptr;
        return false;
    }

    g_back_width = width;
    g_back_height = height;
    return true;
}

struct ComRuntime {
    ComRuntime() { check_hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "CoInitializeEx"); }
    ~ComRuntime() { CoUninitialize(); }
};

struct MediaFoundationRuntime {
    MediaFoundationRuntime() { check_hr(MFStartup(MF_VERSION), "MFStartup"); }
    ~MediaFoundationRuntime() { MFShutdown(); }
};

struct Mode {
    ComPtr<IMFMediaType> type;
    UINT32 width = 0;
    UINT32 height = 0;
    UINT32 fps_num = 0;
    UINT32 fps_den = 1;
    GUID subtype{};
};

std::vector<Mode> enumerate_modes(IMFSourceReader* reader) {
    std::vector<Mode> modes;
    for (DWORD index = 0;; ++index) {
        ComPtr<IMFMediaType> type;
        const HRESULT hr = reader->GetNativeMediaType(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM, index, &type);
        if (hr == MF_E_NO_MORE_TYPES) {
            break;
        }
        check_hr(hr, "GetNativeMediaType");

        GUID major{};
        if (FAILED(type->GetGUID(MF_MT_MAJOR_TYPE, &major)) || major != MFMediaType_Video) {
            continue;
        }

        Mode mode;
        mode.type = type;
        MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &mode.width, &mode.height);
        MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &mode.fps_num, &mode.fps_den);
        type->GetGUID(MF_MT_SUBTYPE, &mode.subtype);
        modes.push_back(mode);
    }
    return modes;
}

const Mode* choose_stereo_mode(const std::vector<Mode>& modes) {
    const Mode* best = nullptr;
    for (const auto& mode : modes) {
        if (mode.width != kStereoWidth || mode.height != kStereoHeight) {
            continue;
        }
        if (best == nullptr) {
            best = &mode;
            continue;
        }

        const bool mode_mjpg = mode.subtype == MFVideoFormat_MJPG;
        const bool best_mjpg = best->subtype == MFVideoFormat_MJPG;
        const double fps = mode.fps_den
            ? static_cast<double>(mode.fps_num) / static_cast<double>(mode.fps_den)
            : 0.0;
        const double best_fps = best->fps_den
            ? static_cast<double>(best->fps_num) / static_cast<double>(best->fps_den)
            : 0.0;

        if ((mode_mjpg && !best_mjpg) ||
            (mode_mjpg == best_mjpg && fps > best_fps)) {
            best = &mode;
        }
    }
    return best;
}

void unlock_touchplus(bool legacy_init) {
    if (sizeof(void*) != 4) {
        throw std::runtime_error("Stereo viewer must run as Win32 because the Etron SDK is 32-bit.");
    }

    std::cout << "[1/3] Vendor unlock\n";
    void* handle = nullptr;
    if (!EtronDI_Init(&handle) || handle == nullptr) {
        throw std::runtime_error("EtronDI_Init failed");
    }

    try {
        int count = 0;
        check_etron(eSPAEAWB_EnumDevice(&count), "eSPAEAWB_EnumDevice");

        int touch_index = -1;
        for (int i = 0; i < count; ++i) {
            WCHAR name[255] = {};
            const int ret = eSPAEAWB_GetDevicename(i, name, 255);
            std::wcout << L"  [" << i << L"] " << name << L" (ret=" << ret << L")\n";
            if (touch_index < 0 && lower(name).find(kTouchPlusFriendlyName) != std::wstring::npos) {
                touch_index = i;
            }
        }

        if (touch_index < 0) {
            throw std::runtime_error("Touch+ Camera not found by the Etron control layer");
        }

        check_etron(eSPAEAWB_SelectDevice(touch_index), "eSPAEAWB_SelectDevice");
        check_etron(eSPAEAWB_SetSensorType(ESPAEAWB_SENSOR_TYPE_OV7740),
                    "eSPAEAWB_SetSensorType(OV7740)");
        check_etron(eSPAEAWB_SWUnlock(0x0107), "eSPAEAWB_SWUnlock(0x0107)");

        if (legacy_init) {
            std::cout << "Applying recovered Ractiv camera initializer...\n";
            check_etron(eSPAEAWB_DisableAE(), "eSPAEAWB_DisableAE");
            check_etron(eSPAEAWB_DisableAWB(), "eSPAEAWB_DisableAWB");

            BYTE gpio = 0;
            check_etron(eSPAEAWB_GetGPIOValue(1, &gpio), "eSPAEAWB_GetGPIOValue(1)");
            gpio = static_cast<BYTE>(gpio | 0x08);
            check_etron(eSPAEAWB_SetGPIOValue(1, gpio), "LEDs ON / GPIO");
            check_etron(eSPAEAWB_SetExposureTime(ESPAEAWB_SENSOR_MODE_BOTH, 15.0f),
                        "Exposure both = 15 ms");
            check_etron(eSPAEAWB_SetGlobalGain(ESPAEAWB_SENSOR_MODE_LEFT, 1.0f),
                        "Global gain left = 1");
            check_etron(eSPAEAWB_SetGlobalGain(ESPAEAWB_SENSOR_MODE_RIGHT, 1.0f),
                        "Global gain right = 1");
            check_etron(eSPAEAWB_SetColorGain(ESPAEAWB_SENSOR_MODE_LEFT, 2.0f, 1.0f, 2.0f),
                        "Color gain left = 2/1/2");
            check_etron(eSPAEAWB_SetColorGain(ESPAEAWB_SENSOR_MODE_RIGHT, 2.0f, 1.0f, 2.0f),
                        "Color gain right = 2/1/2");
        }
    } catch (...) {
        EtronDI_Release(&handle);
        throw;
    }

    EtronDI_Release(&handle);
    std::cout << "Vendor unlock: PASS\n\n";
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_CLOSE:
        g_running = false;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        release_backbuffer();
        g_running = false;
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE || wparam == 'Q') {
            g_running = false;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);

        const int client_width = static_cast<int>(rc.right - rc.left);
        const int client_height = static_cast<int>(rc.bottom - rc.top);
        const int status_height = 34;
        const int view_height = std::max(1, client_height - status_height);
        const int half_width = std::max(1, client_width / 2);

        if (!ensure_backbuffer(dc, client_width, client_height)) {
            FillRect(dc, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            EndPaint(hwnd, &ps);
            return 0;
        }

        RECT back_rect{0, 0, client_width, client_height};
        FillRect(g_back_dc, &back_rect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

        if (g_have_frame) {
            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = static_cast<LONG>(kStereoWidth);
            bmi.bmiHeader.biHeight = -static_cast<LONG>(kStereoHeight);
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            // The live path values latency over high-quality scaling. More
            // importantly, all drawing now lands in the off-screen backbuffer;
            // the visible window receives one final BitBlt instead of seeing
            // the intermediate black clear that caused the Phase 1A flicker.
            SetStretchBltMode(g_back_dc, COLORONCOLOR);
            StretchDIBits(g_back_dc,
                          0, 0, half_width, view_height,
                          0, 0, kEyeWidth, kEyeHeight,
                          g_frame.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
            StretchDIBits(g_back_dc,
                          half_width, 0, client_width - half_width, view_height,
                          kEyeWidth, 0, kEyeWidth, kEyeHeight,
                          g_frame.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
        }

        HPEN divider = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
        HGDIOBJ old_pen = SelectObject(g_back_dc, divider);
        MoveToEx(g_back_dc, half_width, 0, nullptr);
        LineTo(g_back_dc, half_width, view_height);
        SelectObject(g_back_dc, old_pen);
        DeleteObject(divider);

        SetBkMode(g_back_dc, TRANSPARENT);
        SetTextColor(g_back_dc, RGB(255, 255, 255));
        TextOutW(g_back_dc, 12, 10, L"LEFT", 4);
        TextOutW(g_back_dc, half_width + 12, 10, L"RIGHT", 5);

        SetTextColor(g_back_dc, RGB(180, 220, 255));
        TextOutW(g_back_dc, 12, view_height + 8,
                 g_status.c_str(), static_cast<int>(g_status.size()));
        SetTextColor(g_back_dc, RGB(190, 190, 190));
        const wchar_t* help = L"ESC/Q: quit";
        TextOutW(g_back_dc, std::max(12, client_width - 120), view_height + 8,
                 help, static_cast<int>(wcslen(help)));

        BitBlt(dc,
               0, 0, client_width, client_height,
               g_back_dc,
               0, 0,
               SRCCOPY);

        EndPaint(hwnd, &ps);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

HWND create_viewer_window(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kWindowClass;

    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("RegisterClassExW failed");
    }

    RECT desired{0, 0, 1280, 514};
    AdjustWindowRect(&desired, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExW(
        0,
        kWindowClass,
        L"TouchPlus Revival - Stereo Viewer",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        desired.right - desired.left,
        desired.bottom - desired.top,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (!hwnd) {
        throw std::runtime_error("CreateWindowExW failed");
    }
    return hwnd;
}

void pump_messages() {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            g_running = false;
            return;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    bool legacy_init = false;
    for (int i = 1; i < argc; ++i) {
        if (std::wstring(argv[i]) == L"--legacy-init") {
            legacy_init = true;
        }
    }

    try {
        std::cout << "TouchPlus Revival - Phase 1A live stereo viewer\n";
        std::cout << "Process architecture: " << (sizeof(void*) * 8) << "-bit\n";
        std::cout << "Mode: atomic unlock -> persistent 1280x480 MJPEG stream\n\n";

        unlock_touchplus(legacy_init);

        std::cout << "[2/3] Opening stereo stream\n";
        ComRuntime com;
        MediaFoundationRuntime media_foundation;

        ComPtr<IMFAttributes> enum_attributes;
        check_hr(MFCreateAttributes(&enum_attributes, 1), "MFCreateAttributes");
        check_hr(enum_attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                          MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID),
                 "Set video capture source type");

        IMFActivate** devices = nullptr;
        UINT32 device_count = 0;
        check_hr(MFEnumDeviceSources(enum_attributes.Get(), &devices, &device_count),
                 "MFEnumDeviceSources");

        ComPtr<IMFActivate> selected;
        for (UINT32 i = 0; i < device_count; ++i) {
            const std::wstring name = get_string(devices[i], MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME);
            const std::wstring link = get_string(
                devices[i], MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK);
            const bool match = lower(link).find(kTouchPlusVidPid) != std::wstring::npos ||
                               lower(name).find(kTouchPlusFriendlyName) != std::wstring::npos;
            if (match && !selected) {
                selected = devices[i];
                std::wcout << L"  [MATCH] " << name << L"\n";
            }
        }

        for (UINT32 i = 0; i < device_count; ++i) {
            devices[i]->Release();
        }
        CoTaskMemFree(devices);

        if (!selected) {
            throw std::runtime_error("Touch+ Camera not found by Media Foundation");
        }

        ComPtr<IMFMediaSource> source;
        check_hr(selected->ActivateObject(IID_PPV_ARGS(&source)),
                 "ActivateObject(IMFMediaSource)");

        ComPtr<IMFAttributes> reader_attributes;
        check_hr(MFCreateAttributes(&reader_attributes, 2),
                 "MFCreateAttributes(source reader)");
        check_hr(reader_attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE),
                 "Enable video processing");

        ComPtr<IMFSourceReader> reader;
        check_hr(MFCreateSourceReaderFromMediaSource(source.Get(),
                                                      reader_attributes.Get(),
                                                      &reader),
                 "MFCreateSourceReaderFromMediaSource");

        const auto modes = enumerate_modes(reader.Get());
        const Mode* chosen = choose_stereo_mode(modes);
        if (!chosen) {
            source->Shutdown();
            throw std::runtime_error("No native 1280x480 stereo mode exposed");
        }

        const double target_fps = chosen->fps_den
            ? static_cast<double>(chosen->fps_num) / static_cast<double>(chosen->fps_den)
            : 0.0;
        std::cout << "Selected stereo mode: " << chosen->width << "x" << chosen->height
                  << " @ " << std::fixed << std::setprecision(2) << target_fps
                  << " fps [" << fourcc_from_guid(chosen->subtype) << "]\n";

        check_hr(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                              nullptr,
                                              chosen->type.Get()),
                 "Set native Touch+ media type");

        ComPtr<IMFMediaType> rgb_type;
        check_hr(MFCreateMediaType(&rgb_type), "MFCreateMediaType(RGB32)");
        check_hr(rgb_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Set RGB major type");
        check_hr(rgb_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32), "Set RGB32 subtype");
        check_hr(MFSetAttributeSize(rgb_type.Get(), MF_MT_FRAME_SIZE, kStereoWidth, kStereoHeight),
                 "Set RGB frame size");
        check_hr(MFSetAttributeRatio(rgb_type.Get(), MF_MT_FRAME_RATE,
                                     chosen->fps_num, chosen->fps_den),
                 "Set RGB frame rate");
        check_hr(MFSetAttributeRatio(rgb_type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1),
                 "Set pixel aspect ratio");
        check_hr(rgb_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive),
                 "Set progressive mode");
        check_hr(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                              nullptr,
                                              rgb_type.Get()),
                 "Request RGB32 output");

        HWND hwnd = create_viewer_window(GetModuleHandleW(nullptr));
        std::cout << "[3/3] Live viewer running - ESC/Q to quit\n";

        const size_t required = static_cast<size_t>(kStereoWidth) * kStereoHeight * 4;
        std::uint64_t frames_total = 0;
        std::uint64_t frames_window = 0;
        auto fps_epoch = std::chrono::steady_clock::now();

        while (g_running) {
            pump_messages();
            if (!g_running) {
                break;
            }

            DWORD actual_stream = 0;
            DWORD flags = 0;
            LONGLONG timestamp = 0;
            ComPtr<IMFSample> sample;
            check_hr(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                        0,
                                        &actual_stream,
                                        &flags,
                                        &timestamp,
                                        &sample),
                     "ReadSample");

            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                throw std::runtime_error("Touch+ stream ended unexpectedly");
            }
            if (!sample) {
                continue;
            }

            ComPtr<IMFMediaBuffer> buffer;
            check_hr(sample->ConvertToContiguousBuffer(&buffer), "ConvertToContiguousBuffer");

            BYTE* data = nullptr;
            DWORD max_length = 0;
            DWORD current_length = 0;
            check_hr(buffer->Lock(&data, &max_length, &current_length), "IMFMediaBuffer::Lock");
            if (current_length >= required) {
                std::copy_n(data, required, g_frame.data());
                g_have_frame = true;
                ++frames_total;
                ++frames_window;
            }
            buffer->Unlock();

            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - fps_epoch).count();
            if (elapsed >= 1.0) {
                const double fps = static_cast<double>(frames_window) / elapsed;
                std::wostringstream status;
                status << L"1280x480 MJPEG | 640x480 + 640x480 | live "
                       << std::fixed << std::setprecision(1) << fps << L" fps | frame "
                       << frames_total;
                g_status = status.str();
                SetWindowTextW(hwnd, (L"TouchPlus Revival - Stereo Viewer - " + g_status).c_str());
                frames_window = 0;
                fps_epoch = now;
            }

            InvalidateRect(hwnd, nullptr, FALSE);
            UpdateWindow(hwnd);
        }

        source->Shutdown();
        std::cout << "Stereo viewer stopped after " << frames_total << " frames.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "\nPHASE 1A RESULT: FAIL\n" << error.what() << "\n";
        MessageBoxA(nullptr, error.what(), "TouchPlus Stereo Viewer", MB_OK | MB_ICONERROR);
        return 1;
    }
}
