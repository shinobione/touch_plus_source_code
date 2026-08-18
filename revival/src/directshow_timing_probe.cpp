#include <windows.h>
#include <dshow.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "EtronDI_O.h"
#include "eSPAEAWBCtrl.h"

#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kTouchPlusFriendlyName[] = L"touch+ camera";
constexpr LONG kStereoWidth = 1280;
constexpr LONG kStereoHeight = 480;
constexpr double kMeasureSeconds = 6.0;

// qedit.h disappeared from modern Windows SDKs, but the legacy Sample Grabber
// COM filter still has stable published interfaces/CLSIDs. Declare only the
// tiny subset Ractiv's CameraDS path used so this probe builds on current MSVC.
MIDL_INTERFACE("0579154A-2B53-4994-B0D0-E773148EFF85")
ISampleGrabberCBLocal : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE SampleCB(double sample_time, IMediaSample* sample) = 0;
    virtual HRESULT STDMETHODCALLTYPE BufferCB(double sample_time, BYTE* buffer, long buffer_len) = 0;
};

MIDL_INTERFACE("6B652FFF-11FE-4FCE-92AD-0266B5D7C78F")
ISampleGrabberLocal : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE SetOneShot(BOOL one_shot) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMediaType(const AM_MEDIA_TYPE* type) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType(AM_MEDIA_TYPE* type) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetBufferSamples(BOOL buffer_them) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer(long* size, long* buffer) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSample(IMediaSample** sample) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCallback(ISampleGrabberCBLocal* callback, long which_method) = 0;
};

const CLSID kClsidSampleGrabber = {
    0xC1F400A0, 0x3F08, 0x11D3, {0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37}
};
const CLSID kClsidNullRenderer = {
    0xC1F400A4, 0x3F08, 0x11D3, {0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37}
};

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

void free_media_type(AM_MEDIA_TYPE& mt) {
    if (mt.cbFormat != 0) {
        CoTaskMemFree(mt.pbFormat);
        mt.cbFormat = 0;
        mt.pbFormat = nullptr;
    }
    if (mt.pUnk != nullptr) {
        mt.pUnk->Release();
        mt.pUnk = nullptr;
    }
}

void delete_media_type(AM_MEDIA_TYPE* mt) {
    if (!mt) return;
    free_media_type(*mt);
    CoTaskMemFree(mt);
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
        if (c < 32 || c > 126) return "GUID";
    }
    return std::string(chars);
}

struct ComRuntime {
    bool owned = false;
    ComRuntime() {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(hr)) {
            owned = true;
            return;
        }
        if (hr != RPC_E_CHANGED_MODE) {
            check_hr(hr, "CoInitializeEx(STA)");
        }
    }
    ~ComRuntime() {
        if (owned) CoUninitialize();
    }
};

void unlock_touchplus() {
    std::cout << "[1/4] Etron vendor unlock\n";
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
        if (touch_index < 0) throw std::runtime_error("Touch+ Camera not found by Etron");

        check_etron(eSPAEAWB_SelectDevice(touch_index), "eSPAEAWB_SelectDevice");
        check_etron(eSPAEAWB_SetSensorType(ESPAEAWB_SENSOR_TYPE_OV7740),
                    "eSPAEAWB_SetSensorType(OV7740)");
        check_etron(eSPAEAWB_SWUnlock(0x0107), "eSPAEAWB_SWUnlock(0x0107)");
    } catch (...) {
        EtronDI_Release(&handle);
        throw;
    }

    EtronDI_Release(&handle);
    std::cout << "Vendor unlock: PASS\n\n";
}

ComPtr<IBaseFilter> find_touchplus_filter() {
    ComPtr<ICreateDevEnum> dev_enum;
    check_hr(CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&dev_enum)),
             "Create SystemDeviceEnum");

    ComPtr<IEnumMoniker> enum_moniker;
    const HRESULT enum_hr = dev_enum->CreateClassEnumerator(
        CLSID_VideoInputDeviceCategory, &enum_moniker, 0);
    if (enum_hr == S_FALSE || !enum_moniker) {
        throw std::runtime_error("No DirectShow video devices found");
    }
    check_hr(enum_hr, "CreateClassEnumerator(VideoInput)");

    IMoniker* raw_moniker = nullptr;
    while (enum_moniker->Next(1, &raw_moniker, nullptr) == S_OK) {
        ComPtr<IMoniker> moniker;
        moniker.Attach(raw_moniker);
        raw_moniker = nullptr;

        ComPtr<IPropertyBag> bag;
        if (FAILED(moniker->BindToStorage(nullptr, nullptr, IID_PPV_ARGS(&bag)))) {
            continue;
        }

        VARIANT value;
        VariantInit(&value);
        std::wstring name;
        if (SUCCEEDED(bag->Read(L"FriendlyName", &value, nullptr)) && value.vt == VT_BSTR) {
            name = value.bstrVal;
        }
        VariantClear(&value);

        std::wcout << L"  DirectShow device: " << name << L"\n";
        if (lower(name).find(kTouchPlusFriendlyName) == std::wstring::npos) {
            continue;
        }

        ComPtr<IBaseFilter> filter;
        check_hr(moniker->BindToObject(nullptr, nullptr, IID_PPV_ARGS(&filter)),
                 "Bind Touch+ DirectShow filter");
        return filter;
    }

    throw std::runtime_error("Touch+ Camera not found by DirectShow");
}

ComPtr<IPin> first_pin(IBaseFilter* filter, PIN_DIRECTION wanted) {
    ComPtr<IEnumPins> pins;
    check_hr(filter->EnumPins(&pins), "EnumPins");

    IPin* raw = nullptr;
    while (pins->Next(1, &raw, nullptr) == S_OK) {
        PIN_DIRECTION direction{};
        const HRESULT hr = raw->QueryDirection(&direction);
        if (SUCCEEDED(hr) && direction == wanted) {
            ComPtr<IPin> result;
            result.Attach(raw);
            return result;
        }
        raw->Release();
        raw = nullptr;
    }
    throw std::runtime_error("Required DirectShow pin not found");
}

void print_video_info(const char* label, const AM_MEDIA_TYPE* mt) {
    std::cout << label;
    if (!mt || mt->formattype != FORMAT_VideoInfo || !mt->pbFormat) {
        std::cout << " non-VideoInfo format\n";
        return;
    }
    const auto* vih = reinterpret_cast<const VIDEOINFOHEADER*>(mt->pbFormat);
    const double fps = vih->AvgTimePerFrame > 0
        ? 10000000.0 / static_cast<double>(vih->AvgTimePerFrame)
        : 0.0;
    std::cout << ' ' << vih->bmiHeader.biWidth << 'x' << std::abs(vih->bmiHeader.biHeight)
              << " @ " << std::fixed << std::setprecision(2) << fps
              << " fps [" << fourcc_from_guid(mt->subtype) << "]"
              << " AvgTimePerFrame=" << vih->AvgTimePerFrame << "\n";
}

struct TimingSnapshot {
    std::vector<double> wall_seconds;
    std::vector<double> sample_seconds;
};

class TimingCallback final : public ISampleGrabberCBLocal {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ISampleGrabberCBLocal)) {
            *out = static_cast<ISampleGrabberCBLocal*>(this);
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }

    STDMETHODIMP SampleCB(double, IMediaSample*) override { return S_OK; }

    STDMETHODIMP BufferCB(double sample_time, BYTE*, long) override {
        const double wall = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        std::lock_guard<std::mutex> lock(mutex_);
        wall_seconds_.push_back(wall);
        sample_seconds_.push_back(sample_time);
        return S_OK;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        wall_seconds_.clear();
        sample_seconds_.clear();
    }

    TimingSnapshot snapshot() {
        std::lock_guard<std::mutex> lock(mutex_);
        return {wall_seconds_, sample_seconds_};
    }

private:
    std::mutex mutex_;
    std::vector<double> wall_seconds_;
    std::vector<double> sample_seconds_;
};

double percentile(std::vector<double> values, double p) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double pos = p * static_cast<double>(values.size() - 1);
    const size_t lo = static_cast<size_t>(std::floor(pos));
    const size_t hi = static_cast<size_t>(std::ceil(pos));
    if (lo == hi) return values[lo];
    const double t = pos - static_cast<double>(lo);
    return values[lo] * (1.0 - t) + values[hi] * t;
}

std::vector<double> deltas_ms(const std::vector<double>& values) {
    std::vector<double> result;
    if (values.size() < 2) return result;
    result.reserve(values.size() - 1);
    for (size_t i = 1; i < values.size(); ++i) {
        const double delta = (values[i] - values[i - 1]) * 1000.0;
        if (delta > 0.0 && delta < 1000.0) result.push_back(delta);
    }
    return result;
}

} // namespace

int wmain() {
    try {
        std::cout << "TouchPlus Revival - legacy DirectShow timing probe\n";
        std::cout << "Replays Ractiv CameraDS negotiation: MJPG 1280x480, AvgTimePerFrame=1e7/60.\n\n";

        unlock_touchplus();
        ComRuntime com;

        std::cout << "[2/4] Building legacy DirectShow graph\n";
        ComPtr<IGraphBuilder> graph;
        check_hr(CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&graph)),
                 "Create FilterGraph");

        ComPtr<IMediaControl> media_control;
        check_hr(graph.As(&media_control), "Query IMediaControl");

        ComPtr<IBaseFilter> camera = find_touchplus_filter();
        check_hr(graph->AddFilter(camera.Get(), L"Touch+ Camera"), "Add Touch+ filter");

        ComPtr<IBaseFilter> grabber_filter;
        check_hr(CoCreateInstance(kClsidSampleGrabber, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&grabber_filter)),
                 "Create legacy SampleGrabber");
        check_hr(graph->AddFilter(grabber_filter.Get(), L"Grabber"), "Add SampleGrabber");

        ComPtr<ISampleGrabberLocal> grabber;
        check_hr(grabber_filter.As(&grabber), "Query ISampleGrabber");

        AM_MEDIA_TYPE grab_type{};
        grab_type.majortype = MEDIATYPE_Video;
        grab_type.subtype = MEDIASUBTYPE_MJPG;
        grab_type.formattype = FORMAT_VideoInfo;
        check_hr(grabber->SetMediaType(&grab_type), "SampleGrabber SetMediaType(MJPG)");

        ComPtr<IBaseFilter> null_renderer;
        check_hr(CoCreateInstance(kClsidNullRenderer, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&null_renderer)),
                 "Create NullRenderer");
        check_hr(graph->AddFilter(null_renderer.Get(), L"NullRenderer"), "Add NullRenderer");

        ComPtr<IPin> camera_out = first_pin(camera.Get(), PINDIR_OUTPUT);
        ComPtr<IPin> grabber_in = first_pin(grabber_filter.Get(), PINDIR_INPUT);
        ComPtr<IPin> grabber_out = first_pin(grabber_filter.Get(), PINDIR_OUTPUT);
        ComPtr<IPin> null_in = first_pin(null_renderer.Get(), PINDIR_INPUT);

        std::cout << "[3/4] Forcing historical IAMStreamConfig format\n";
        ComPtr<IAMStreamConfig> stream_config;
        check_hr(camera_out.As(&stream_config), "Query IAMStreamConfig");

        AM_MEDIA_TYPE* current = nullptr;
        check_hr(stream_config->GetFormat(&current), "IAMStreamConfig::GetFormat(before)");
        print_video_info("Before SetFormat:", current);

        if (!current || current->formattype != FORMAT_VideoInfo || !current->pbFormat) {
            delete_media_type(current);
            throw std::runtime_error("Touch+ DirectShow pin did not expose FORMAT_VideoInfo");
        }

        auto* vih = reinterpret_cast<VIDEOINFOHEADER*>(current->pbFormat);
        current->subtype = MEDIASUBTYPE_MJPG;
        vih->bmiHeader.biWidth = kStereoWidth;
        vih->bmiHeader.biHeight = kStereoHeight;
        vih->AvgTimePerFrame = 10000000LL / 60LL;
        check_hr(stream_config->SetFormat(current), "IAMStreamConfig::SetFormat(1280x480 MJPG @60)");
        delete_media_type(current);
        current = nullptr;

        AM_MEDIA_TYPE* negotiated = nullptr;
        check_hr(stream_config->GetFormat(&negotiated), "IAMStreamConfig::GetFormat(after)");
        print_video_info("After SetFormat :", negotiated);
        double negotiated_fps = 0.0;
        if (negotiated && negotiated->formattype == FORMAT_VideoInfo && negotiated->pbFormat) {
            const auto* n = reinterpret_cast<const VIDEOINFOHEADER*>(negotiated->pbFormat);
            if (n->AvgTimePerFrame > 0) {
                negotiated_fps = 10000000.0 / static_cast<double>(n->AvgTimePerFrame);
            }
        }
        delete_media_type(negotiated);

        check_hr(graph->Connect(camera_out.Get(), grabber_in.Get()), "Connect camera -> grabber");
        check_hr(graph->Connect(grabber_out.Get(), null_in.Get()), "Connect grabber -> null renderer");

        TimingCallback callback;
        check_hr(grabber->SetBufferSamples(FALSE), "SetBufferSamples(FALSE)");
        check_hr(grabber->SetOneShot(FALSE), "SetOneShot(FALSE)");
        check_hr(grabber->SetCallback(&callback, 1), "SetCallback(BufferCB)");

        check_hr(media_control->Run(), "IMediaControl::Run");
        Sleep(750);
        callback.reset();

        std::cout << "[4/4] Measuring 6 seconds of RAW DirectShow callbacks...\n";
        Sleep(static_cast<DWORD>(kMeasureSeconds * 1000.0));
        check_hr(media_control->Stop(), "IMediaControl::Stop");
        grabber->SetCallback(nullptr, 0);

        const TimingSnapshot data = callback.snapshot();
        const auto wall_ms = deltas_ms(data.wall_seconds);
        const auto sample_ms = deltas_ms(data.sample_seconds);

        const double wall_elapsed = data.wall_seconds.size() >= 2
            ? data.wall_seconds.back() - data.wall_seconds.front()
            : 0.0;
        const double wall_rate = wall_elapsed > 0.0
            ? static_cast<double>(data.wall_seconds.size() - 1) / wall_elapsed
            : 0.0;
        const double wall_median = percentile(wall_ms, 0.50);
        const double sample_median = percentile(sample_ms, 0.50);
        const double sample_hz = sample_median > 0.0 ? 1000.0 / sample_median : 0.0;

        std::cout << "\n======= DIRECTSHOW TIMING RESULT =======\n";
        std::cout << "Negotiated pin rate  : " << std::fixed << std::setprecision(2)
                  << negotiated_fps << " fps\n";
        std::cout << "Measured wall rate   : " << wall_rate << " callbacks/s\n";
        std::cout << "Wall median interval : " << wall_median << " ms\n";
        std::cout << "SampleTime median    : " << sample_median << " ms => "
                  << sample_hz << " Hz\n";
        std::cout << "Measured callbacks   : " << data.wall_seconds.size() << "\n";

        if (wall_rate >= 50.0 || sample_hz >= 50.0) {
            std::cout << "VERDICT: DIRECTSHOW_60_CLASS - legacy negotiation restores ~60 fps.\n";
        } else if (wall_rate >= 24.0 && wall_rate <= 36.0) {
            std::cout << "VERDICT: DIRECTSHOW_30_CLASS - even Ractiv's legacy path is ~30 fps.\n";
        } else {
            std::cout << "VERDICT: DIRECTSHOW_OTHER - inspect values / graph negotiation.\n";
        }
        std::cout << "========================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nDIRECTSHOW TIMING PROBE FAIL: " << e.what() << "\n";
        std::cerr << "If Create legacy SampleGrabber returns 0x80040154, this Windows build no longer registers Qedit SampleGrabber.\n";
        return 1;
    }
}
