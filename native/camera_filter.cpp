#include <windows.h>
#include <dshow.h>
#include <ks.h>
#include <ksmedia.h>
#include <dvdmedia.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <wrl.h>
#include "frame_bridge.h"

using Microsoft::WRL::ComPtr;
using namespace callayers;
// Separate from the previous Media Foundation camera identity.
static const CLSID CLSID_Callayers = {0x49c2dca8,0xf030,0x4c75,{0x9b,0x0e,0xc5,0x1e,0x33,0xc1,0xa2,0x91}};
static constexpr wchar_t ClassId[] = L"{49C2DCA8-F030-4C75-9B0E-C51E33C1A291}";
static std::atomic<long> objects{0}, serverLocks{0};
static HMODULE moduleHandle;
static void FreeType(AM_MEDIA_TYPE& mt) {
    CoTaskMemFree(mt.pbFormat); if (mt.pUnk) mt.pUnk->Release(); mt = {};
}
static HRESULT CopyType(AM_MEDIA_TYPE& dst, const AM_MEDIA_TYPE& src) {
    dst = src; dst.pbFormat = nullptr; dst.pUnk = nullptr;
    if (src.cbFormat) {
        if (!src.pbFormat) return E_INVALIDARG;
        dst.pbFormat = static_cast<BYTE*>(CoTaskMemAlloc(src.cbFormat));
        if (!dst.pbFormat) return E_OUTOFMEMORY;
        memcpy(dst.pbFormat, src.pbFormat, src.cbFormat);
    }
    dst.pUnk = src.pUnk; if (dst.pUnk) dst.pUnk->AddRef(); return S_OK;
}
static HRESULT MakeType(int index, AM_MEDIA_TYPE& mt) {
    mt = {}; mt.majortype = MEDIATYPE_Video; mt.subtype = index ? MEDIASUBTYPE_RGB32 : MEDIASUBTYPE_RGB24;
    mt.bFixedSizeSamples = TRUE; mt.bTemporalCompression = FALSE; mt.formattype = FORMAT_VideoInfo;
    mt.cbFormat = sizeof(VIDEOINFOHEADER); mt.pbFormat = static_cast<BYTE*>(CoTaskMemAlloc(mt.cbFormat));
    if (!mt.pbFormat) return E_OUTOFMEMORY;
    ZeroMemory(mt.pbFormat, mt.cbFormat);
    auto info = reinterpret_cast<VIDEOINFOHEADER*>(mt.pbFormat);
    info->AvgTimePerFrame = 10000000 / Fps;
    info->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info->bmiHeader.biWidth = Width; info->bmiHeader.biHeight = Height;
    info->bmiHeader.biPlanes = 1; info->bmiHeader.biBitCount = index ? 32 : 24;
    info->bmiHeader.biCompression = BI_RGB;
    info->bmiHeader.biSizeImage = Width * Height * (index ? 4 : 3);
    info->dwBitRate = info->bmiHeader.biSizeImage * 8 * Fps;
    mt.lSampleSize = info->bmiHeader.biSizeImage;
    return S_OK;
}
static bool AcceptType(const AM_MEDIA_TYPE* mt) {
    if (!mt || mt->majortype != MEDIATYPE_Video || mt->formattype != FORMAT_VideoInfo ||
        (mt->subtype != MEDIASUBTYPE_RGB24 && mt->subtype != MEDIASUBTYPE_RGB32) ||
        mt->cbFormat < sizeof(VIDEOINFOHEADER) || !mt->pbFormat) return false;
    auto info = reinterpret_cast<const VIDEOINFOHEADER*>(mt->pbFormat);
    return info->bmiHeader.biWidth == Width && info->bmiHeader.biHeight == Height &&
        info->bmiHeader.biPlanes == 1 && info->bmiHeader.biCompression == BI_RGB &&
        info->bmiHeader.biBitCount == (mt->subtype == MEDIASUBTYPE_RGB32 ? 32 : 24) &&
        (info->AvgTimePerFrame == 0 || info->AvgTimePerFrame == 10000000 / Fps);
}
class Filter;
class OutputPin final : public IPin, public IAMStreamConfig, public IKsPropertySet, public IAMPushSource {
    Filter* owner_;
    ComPtr<IPin> connected_;
    ComPtr<IMemInputPin> input_;
    ComPtr<IMemAllocator> allocator_;
    AM_MEDIA_TYPE type_{};
    std::thread worker_;
    HANDLE stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    FrameBridge bridge_;
    std::atomic<REFERENCE_TIME> offset_{0}, maxOffset_{0};
    bool formatSet_ = false;
    void deliver();
public:
    explicit OutputPin(Filter* owner) : owner_(owner) { MakeType(0, type_); }
    ~OutputPin() { stop(); FreeType(type_); CloseHandle(stopEvent_); }
    HRESULT start();
    void stop();
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE Connect(IPin* receiver, const AM_MEDIA_TYPE* requested) override;
    HRESULT STDMETHODCALLTYPE ReceiveConnection(IPin*, const AM_MEDIA_TYPE*) override { return VFW_E_INVALID_DIRECTION; }
    HRESULT STDMETHODCALLTYPE Disconnect() override;
    HRESULT STDMETHODCALLTYPE ConnectedTo(IPin** pin) override {
        if (!pin) return E_POINTER; *pin = connected_.Get(); if (!*pin) return VFW_E_NOT_CONNECTED; (*pin)->AddRef(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ConnectionMediaType(AM_MEDIA_TYPE* mt) override {
        if (!mt) return E_POINTER; if (!connected_) return VFW_E_NOT_CONNECTED; return CopyType(*mt, type_);
    }
    HRESULT STDMETHODCALLTYPE QueryPinInfo(PIN_INFO* info) override;
    HRESULT STDMETHODCALLTYPE QueryDirection(PIN_DIRECTION* direction) override { if (!direction) return E_POINTER; *direction = PINDIR_OUTPUT; return S_OK; }
    HRESULT STDMETHODCALLTYPE QueryId(LPWSTR* id) override {
        if (!id) return E_POINTER; *id = static_cast<wchar_t*>(CoTaskMemAlloc(16 * sizeof(wchar_t))); if (!*id) return E_OUTOFMEMORY;
        wcscpy_s(*id, 16, L"Capture"); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE QueryAccept(const AM_MEDIA_TYPE* mt) override { return AcceptType(mt) ? S_OK : S_FALSE; }
    HRESULT STDMETHODCALLTYPE EnumMediaTypes(IEnumMediaTypes** types) override;
    HRESULT STDMETHODCALLTYPE QueryInternalConnections(IPin**, ULONG*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EndOfStream() override { return E_UNEXPECTED; }
    HRESULT STDMETHODCALLTYPE BeginFlush() override { return E_UNEXPECTED; }
    HRESULT STDMETHODCALLTYPE EndFlush() override { return E_UNEXPECTED; }
    HRESULT STDMETHODCALLTYPE NewSegment(REFERENCE_TIME, REFERENCE_TIME, double) override { return E_UNEXPECTED; }
    HRESULT STDMETHODCALLTYPE SetFormat(AM_MEDIA_TYPE* mt) override;
    HRESULT STDMETHODCALLTYPE GetFormat(AM_MEDIA_TYPE** mt) override {
        if (!mt) return E_POINTER; *mt = static_cast<AM_MEDIA_TYPE*>(CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE)));
        if (!*mt) return E_OUTOFMEMORY; HRESULT hr = CopyType(**mt, type_); if (FAILED(hr)) { CoTaskMemFree(*mt); *mt = nullptr; } return hr;
    }
    HRESULT STDMETHODCALLTYPE GetNumberOfCapabilities(int* count, int* size) override {
        if (!count || !size) return E_POINTER; *count = 2; *size = sizeof(VIDEO_STREAM_CONFIG_CAPS); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetStreamCaps(int index, AM_MEDIA_TYPE** mt, BYTE* caps) override {
        if (!mt || !caps) return E_POINTER; *mt = nullptr; if (index < 0 || index >= 2) return S_FALSE;
        *mt = static_cast<AM_MEDIA_TYPE*>(CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE))); if (!*mt) return E_OUTOFMEMORY;
        HRESULT hr = MakeType(index, **mt); if (FAILED(hr)) { CoTaskMemFree(*mt); *mt = nullptr; return hr; }
        auto c = reinterpret_cast<VIDEO_STREAM_CONFIG_CAPS*>(caps); *c = {};
        c->guid = FORMAT_VideoInfo; c->InputSize = c->MinCroppingSize = c->MaxCroppingSize = c->MinOutputSize = c->MaxOutputSize = {Width, Height};
        c->CropGranularityX = c->CropGranularityY = c->OutputGranularityX = c->OutputGranularityY = 1;
        c->MinFrameInterval = c->MaxFrameInterval = 10000000 / Fps;
        c->MinBitsPerSecond = c->MaxBitsPerSecond = Width * Height * (index ? 32 : 24) * Fps;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Set(REFGUID, DWORD, LPVOID, DWORD, LPVOID, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Get(REFGUID set, DWORD property, LPVOID, DWORD, LPVOID data, DWORD size, DWORD* returned) override {
        if (set != AMPROPSETID_Pin) return E_PROP_SET_UNSUPPORTED;
        if (property != AMPROPERTY_PIN_CATEGORY) return E_PROP_ID_UNSUPPORTED;
        if (returned) *returned = sizeof(GUID); if (!data) return S_OK; if (size < sizeof(GUID)) return E_UNEXPECTED;
        *static_cast<GUID*>(data) = PIN_CATEGORY_CAPTURE; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE QuerySupported(REFGUID set, DWORD property, DWORD* flags) override {
        if (!flags) return E_POINTER; *flags = 0;
        if (set != AMPROPSETID_Pin) return E_PROP_SET_UNSUPPORTED;
        if (property != AMPROPERTY_PIN_CATEGORY) return E_PROP_ID_UNSUPPORTED;
        *flags = KSPROPERTY_SUPPORT_GET; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetLatency(REFERENCE_TIME* latency) override { if (!latency) return E_POINTER; *latency = 10000000 / Fps; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPushSourceFlags(ULONG* flags) override { if (!flags) return E_POINTER; *flags = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetPushSourceFlags(ULONG) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetStreamOffset(REFERENCE_TIME offset) override { offset_ = offset; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetStreamOffset(REFERENCE_TIME* offset) override { if (!offset) return E_POINTER; *offset = offset_; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetMaxStreamOffset(REFERENCE_TIME* offset) override { if (!offset) return E_POINTER; *offset = maxOffset_; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetMaxStreamOffset(REFERENCE_TIME offset) override { maxOffset_ = offset; return S_OK; }
};
class Filter final : public IBaseFilter, public IAMFilterMiscFlags {
    std::atomic<ULONG> refs_{1};
    std::mutex clockMutex_;
    ComPtr<IReferenceClock> clock_;
    IFilterGraph* graph_ = nullptr; // Graph owns filter; this is deliberately weak.
    std::wstring name_ = L"Callayers";
public:
    std::atomic<FILTER_STATE> state{State_Stopped};
    std::atomic<REFERENCE_TIME> startTime{0};
    OutputPin pin{this};
    Filter() { ++objects; }
    ~Filter() { pin.stop(); --objects; }
    REFERENCE_TIME streamTime() {
        std::lock_guard lock(clockMutex_); REFERENCE_TIME time = 0;
        if (clock_ && SUCCEEDED(clock_->GetTime(&time))) return std::max<REFERENCE_TIME>(0, time - startTime.load());
        return -1;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (id == IID_IUnknown || id == IID_IPersist || id == IID_IMediaFilter || id == IID_IBaseFilter) *out = static_cast<IBaseFilter*>(this);
        else if (id == IID_IAMFilterMiscFlags) *out = static_cast<IAMFilterMiscFlags*>(this);
        if (!*out) return E_NOINTERFACE; AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* id) override { if (!id) return E_POINTER; *id = CLSID_Callayers; return S_OK; }
    HRESULT STDMETHODCALLTYPE Stop() override { state = State_Stopped; pin.stop(); return S_OK; }
    HRESULT STDMETHODCALLTYPE Pause() override { state = State_Paused; pin.stop(); return S_OK; }
    HRESULT STDMETHODCALLTYPE Run(REFERENCE_TIME start) override {
        startTime = start; state = State_Running; HRESULT hr = pin.start(); if (FAILED(hr)) state = State_Stopped; return hr;
    }
    HRESULT STDMETHODCALLTYPE GetState(DWORD, FILTER_STATE* result) override { if (!result) return E_POINTER; *result = state; return state == State_Paused ? VFW_S_CANT_CUE : S_OK; }
    HRESULT STDMETHODCALLTYPE SetSyncSource(IReferenceClock* clock) override { std::lock_guard lock(clockMutex_); clock_ = clock; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetSyncSource(IReferenceClock** clock) override { if (!clock) return E_POINTER; std::lock_guard lock(clockMutex_); return clock_.CopyTo(clock); }
    HRESULT STDMETHODCALLTYPE EnumPins(IEnumPins** pins) override;
    HRESULT STDMETHODCALLTYPE FindPin(LPCWSTR id, IPin** result) override {
        if (!result) return E_POINTER; *result = nullptr; if (!id || wcscmp(id, L"Capture")) return VFW_E_NOT_FOUND;
        *result = &pin; pin.AddRef(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE QueryFilterInfo(FILTER_INFO* info) override {
        if (!info) return E_POINTER; *info = {}; wcsncpy_s(info->achName, name_.c_str(), _TRUNCATE);
        info->pGraph = graph_; if (graph_) graph_->AddRef(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE JoinFilterGraph(IFilterGraph* graph, LPCWSTR name) override { graph_ = graph; name_ = name ? name : L"Callayers"; return S_OK; }
    HRESULT STDMETHODCALLTYPE QueryVendorInfo(LPWSTR* info) override { if (info) *info = nullptr; return E_NOTIMPL; }
    ULONG STDMETHODCALLTYPE GetMiscFlags() override { return AM_FILTER_MISC_FLAGS_IS_SOURCE; }
};

class PinEnum final : public IEnumPins {
    std::atomic<ULONG> refs_{1}; IPin* pin_; ULONG position_;
public:
    explicit PinEnum(IPin* pin, ULONG position = 0) : pin_(pin), position_(position) { pin_->AddRef(); }
    ~PinEnum() { pin_->Release(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER; *out = nullptr; if (id != IID_IUnknown && id != IID_IEnumPins) return E_NOINTERFACE;
        *out = static_cast<IEnumPins*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE Next(ULONG count, IPin** pins, ULONG* fetched) override {
        if (!pins || (!fetched && count != 1)) return E_POINTER; ULONG n = 0;
        if (count && position_ == 0) { pins[n++] = pin_; pin_->AddRef(); position_ = 1; }
        if (fetched) *fetched = n; return n == count ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG count) override { ULONG available = 1 - position_; position_ += std::min(count, available); return count <= available ? S_OK : S_FALSE; }
    HRESULT STDMETHODCALLTYPE Reset() override { position_ = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE Clone(IEnumPins** result) override { if (!result) return E_POINTER; *result = new PinEnum(pin_, position_); return S_OK; }
};
class TypeEnum final : public IEnumMediaTypes {
    std::atomic<ULONG> refs_{1}; IPin* pin_; ULONG position_; int preferred_;
public:
    TypeEnum(IPin* pin, int preferred, ULONG position = 0) : pin_(pin), position_(position), preferred_(preferred) { pin_->AddRef(); }
    ~TypeEnum() { pin_->Release(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER; *out = nullptr; if (id != IID_IUnknown && id != IID_IEnumMediaTypes) return E_NOINTERFACE;
        *out = static_cast<IEnumMediaTypes*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE Next(ULONG count, AM_MEDIA_TYPE** types, ULONG* fetched) override {
        if (!types || (!fetched && count != 1)) return E_POINTER; ULONG n = 0;
        while (n < count && position_ < 2) {
            types[n] = static_cast<AM_MEDIA_TYPE*>(CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE)));
            if (!types[n]) { if (fetched) *fetched = n; return E_OUTOFMEMORY; }
            HRESULT hr = MakeType((preferred_ + int(position_)) % 2, *types[n]);
            if (FAILED(hr)) { CoTaskMemFree(types[n]); if (fetched) *fetched = n; return hr; }
            ++n; ++position_;
        }
        if (fetched) *fetched = n; return n == count ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG count) override { ULONG available = 2 - position_; position_ += std::min(count, available); return count <= available ? S_OK : S_FALSE; }
    HRESULT STDMETHODCALLTYPE Reset() override { position_ = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE Clone(IEnumMediaTypes** result) override { if (!result) return E_POINTER; *result = new TypeEnum(pin_, preferred_, position_); return S_OK; }
};
HRESULT Filter::EnumPins(IEnumPins** pins) { if (!pins) return E_POINTER; *pins = new PinEnum(&pin); return S_OK; }
ULONG OutputPin::AddRef() { return owner_->AddRef(); }
ULONG OutputPin::Release() { return owner_->Release(); }
HRESULT OutputPin::QueryInterface(REFIID id, void** out) {
    if (!out) return E_POINTER; *out = nullptr;
    if (id == IID_IUnknown || id == IID_IPin) *out = static_cast<IPin*>(this);
    else if (id == IID_IAMStreamConfig) *out = static_cast<IAMStreamConfig*>(this);
    else if (id == IID_IKsPropertySet) *out = static_cast<IKsPropertySet*>(this);
    else if (id == IID_IAMPushSource || id == IID_IAMLatency) *out = static_cast<IAMPushSource*>(this);
    if (!*out) return E_NOINTERFACE; AddRef(); return S_OK;
}
HRESULT OutputPin::QueryPinInfo(PIN_INFO* info) {
    if (!info) return E_POINTER; *info = {}; info->pFilter = owner_; owner_->AddRef(); info->dir = PINDIR_OUTPUT;
    wcscpy_s(info->achName, L"Capture"); return S_OK;
}
HRESULT OutputPin::EnumMediaTypes(IEnumMediaTypes** types) {
    if (!types) return E_POINTER; *types = new TypeEnum(this, type_.subtype == MEDIASUBTYPE_RGB32 ? 1 : 0); return S_OK;
}
HRESULT OutputPin::SetFormat(AM_MEDIA_TYPE* mt) {
    if (owner_->state != State_Stopped) return VFW_E_NOT_STOPPED;
    if (connected_) return VFW_E_ALREADY_CONNECTED;
    if (!AcceptType(mt)) return VFW_E_TYPE_NOT_ACCEPTED;
    AM_MEDIA_TYPE copy{}; HRESULT hr = MakeType(mt->subtype == MEDIASUBTYPE_RGB32 ? 1 : 0, copy); if (FAILED(hr)) return hr;
    FreeType(type_); type_ = copy; formatSet_ = true; return S_OK;
}
HRESULT OutputPin::Connect(IPin* receiver, const AM_MEDIA_TYPE* requested) {
    if (!receiver) return E_POINTER; if (connected_) return VFW_E_ALREADY_CONNECTED;
    if (owner_->state != State_Stopped) return VFW_E_NOT_STOPPED;
    if (requested && requested->majortype != GUID_NULL && requested->majortype != MEDIATYPE_Video) return VFW_E_TYPE_NOT_ACCEPTED;
    ComPtr<IMemInputPin> input; HRESULT hr = receiver->QueryInterface(IID_PPV_ARGS(&input)); if (FAILED(hr)) return hr;
    for (int attempt = 0; attempt < 2; ++attempt) {
        AM_MEDIA_TYPE candidate{};
        int preferred = type_.subtype == MEDIASUBTYPE_RGB32 ? 1 : 0;
        if (requested && requested->formattype != GUID_NULL) {
            if (attempt || !AcceptType(requested)) break;
            hr = MakeType(requested->subtype == MEDIASUBTYPE_RGB32 ? 1 : 0, candidate);
        } else {
            if (formatSet_ && attempt) break;
            hr = MakeType((preferred + attempt) % 2, candidate);
            if (requested && requested->subtype != GUID_NULL && candidate.subtype != requested->subtype) { FreeType(candidate); continue; }
        }
        if (FAILED(hr)) return hr;
        hr = receiver->ReceiveConnection(this, &candidate);
        if (FAILED(hr)) { FreeType(candidate); continue; }
        ComPtr<IMemAllocator> allocator;
        if (FAILED(input->GetAllocator(&allocator))) hr = CoCreateInstance(CLSID_MemoryAllocator, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&allocator));
        else hr = S_OK;
        ALLOCATOR_PROPERTIES requirements{}; input->GetAllocatorRequirements(&requirements);
        ALLOCATOR_PROPERTIES wanted{std::max<LONG>(2, requirements.cBuffers), std::max<LONG>(LONG(candidate.lSampleSize), requirements.cbBuffer), std::max<LONG>(1, requirements.cbAlign), std::max<LONG>(0, requirements.cbPrefix)}, actual{};
        if (SUCCEEDED(hr)) hr = allocator->SetProperties(&wanted, &actual);
        if (SUCCEEDED(hr) && (actual.cbBuffer < wanted.cbBuffer || actual.cBuffers < wanted.cBuffers)) hr = E_FAIL;
        if (SUCCEEDED(hr)) hr = input->NotifyAllocator(allocator.Get(), FALSE);
        if (FAILED(hr)) { receiver->Disconnect(); FreeType(candidate); continue; }
        connected_ = receiver; input_ = input; allocator_ = allocator; FreeType(type_); type_ = candidate; return S_OK;
    }
    return VFW_E_NO_ACCEPTABLE_TYPES;
}
HRESULT OutputPin::Disconnect() {
    if (owner_->state != State_Stopped) return VFW_E_NOT_STOPPED;
    if (!connected_) return S_FALSE; stop(); allocator_.Reset(); input_.Reset(); connected_.Reset(); return S_OK;
}
HRESULT OutputPin::start() {
    if (!connected_ || worker_.joinable()) return S_OK;
    if (!stopEvent_ || !bridge_.valid()) return E_FAIL;
    HRESULT hr = allocator_->Commit(); if (FAILED(hr)) return hr;
    ResetEvent(stopEvent_); worker_ = std::thread([this] { deliver(); }); return S_OK;
}
void OutputPin::stop() {
    SetEvent(stopEvent_);
    if (worker_.joinable()) {
        if (connected_) connected_->BeginFlush();
        if (allocator_) allocator_->Decommit();
        worker_.join();
        if (connected_) connected_->EndFlush();
        bridge_.consumerStopped();
    }
}
void OutputPin::deliver() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::vector<uint8_t> live, neutral; Neutral(neutral);
    const int bpp = type_.subtype == MEDIASUBTYPE_RGB32 ? 4 : 3;
    auto began = std::chrono::steady_clock::now(), next = began;
    REFERENCE_TIME previous = -1;
    bool discontinuity = true;
    while (WaitForSingleObject(stopEvent_, 0) != WAIT_OBJECT_0 && owner_->state == State_Running) {
        bridge_.heartbeat();
        ComPtr<IMediaSample> sample;
        HRESULT hr = allocator_->GetBuffer(&sample, nullptr, nullptr, AM_GBF_NOWAIT);
        if (SUCCEEDED(hr)) {
            BYTE* output = nullptr; sample->GetPointer(&output);
            const auto& picture = bridge_.read(live) ? live : neutral;
            if (output && sample->GetSize() >= Width * Height * bpp) {
                for (int y = 0; y < Height; ++y) {
                    const auto src = picture.data() + size_t(Height - y - 1) * Width * 4;
                    auto dst = output + size_t(y) * Width * bpp;
                    if (bpp == 4) memcpy(dst, src, Width * 4);
                    else for (int x = 0; x < Width; ++x) { dst[x*3] = src[x*4]; dst[x*3+1] = src[x*4+1]; dst[x*3+2] = src[x*4+2]; }
                }
                REFERENCE_TIME stamp = owner_->streamTime();
                if (stamp < 0) stamp = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - began).count() / 100;
                stamp = std::max(stamp + offset_, previous + 1); previous = stamp;
                REFERENCE_TIME end = stamp + 10000000 / Fps;
                sample->SetTime(&stamp, &end); sample->SetMediaTime(nullptr, nullptr);
                sample->SetActualDataLength(Width * Height * bpp); sample->SetSyncPoint(TRUE);
                sample->SetDiscontinuity(discontinuity); sample->SetPreroll(FALSE);
                hr = input_->Receive(sample.Get()); discontinuity = FAILED(hr);
                if (FAILED(hr)) break;
            }
        } else discontinuity = true;
        next += std::chrono::nanoseconds(1000000000 / Fps);
        auto now = std::chrono::steady_clock::now(); if (next < now) next = now;
        auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(next - now).count();
        if (WaitForSingleObject(stopEvent_, DWORD(std::max<int64_t>(1, delay))) == WAIT_OBJECT_0) break;
    }
    bridge_.consumerStopped(); CoUninitialize();
}
class Factory final : public IClassFactory {
    std::atomic<ULONG> refs_{1};
public:
    Factory() { ++objects; } ~Factory() { --objects; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER; *out = nullptr; if (id != IID_IUnknown && id != IID_IClassFactory) return E_NOINTERFACE;
        *out = static_cast<IClassFactory*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID id, void** out) override {
        if (!out) return E_POINTER; *out = nullptr; if (outer) return CLASS_E_NOAGGREGATION;
        auto filter = new Filter; HRESULT hr = filter->QueryInterface(id, out); filter->Release(); return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override { if (lock) ++serverLocks; else --serverLocks; return S_OK; }
};
extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID iid, void** out) {
    if (!out) return E_POINTER; *out = nullptr; if (clsid != CLSID_Callayers) return CLASS_E_CLASSNOTAVAILABLE;
    auto factory = new Factory; HRESULT hr = factory->QueryInterface(iid, out); factory->Release(); return hr;
}
extern "C" HRESULT __stdcall DllCanUnloadNow() { return !objects && !serverLocks ? S_OK : S_FALSE; }
static HRESULT RegistryString(const std::wstring& key, const wchar_t* name, const std::wstring& value) {
    HKEY handle = nullptr;
    LSTATUS result = RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &handle, nullptr);
    if (result != ERROR_SUCCESS) return HRESULT_FROM_WIN32(result);
    result = RegSetValueExW(handle, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), DWORD((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(handle); return HRESULT_FROM_WIN32(result);
}
extern "C" HRESULT __stdcall DllRegisterServer() {
    wchar_t path[32768]; if (!GetModuleFileNameW(moduleHandle, path, 32768)) return HRESULT_FROM_WIN32(GetLastError());
    std::wstring cls = std::wstring(L"Software\\Classes\\CLSID\\") + ClassId;
    HRESULT hr = RegistryString(cls, nullptr, L"Callayers"); if (FAILED(hr)) return hr;
    hr = RegistryString(cls + L"\\InprocServer32", nullptr, path); if (FAILED(hr)) return hr;
    hr = RegistryString(cls + L"\\InprocServer32", L"ThreadingModel", L"Both"); if (FAILED(hr)) return hr;
    std::wstring category = std::wstring(L"Software\\Classes\\CLSID\\{860BB310-5D01-11D0-BD3B-00A0C911CE86}\\Instance\\") + ClassId;
    hr = RegistryString(category, L"CLSID", ClassId); if (FAILED(hr)) return hr;
    return RegistryString(category, L"FriendlyName", L"Callayers");
}
extern "C" HRESULT __stdcall DllUnregisterServer() {
    std::wstring cls = std::wstring(L"Software\\Classes\\CLSID\\") + ClassId;
    std::wstring category = std::wstring(L"Software\\Classes\\CLSID\\{860BB310-5D01-11D0-BD3B-00A0C911CE86}\\Instance\\") + ClassId;
    LSTATUS a = RegDeleteTreeW(HKEY_CURRENT_USER, category.c_str()), b = RegDeleteTreeW(HKEY_CURRENT_USER, cls.c_str());
    if (a != ERROR_SUCCESS && a != ERROR_FILE_NOT_FOUND) return HRESULT_FROM_WIN32(a);
    return b == ERROR_FILE_NOT_FOUND ? S_OK : HRESULT_FROM_WIN32(b);
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { moduleHandle = instance; DisableThreadLibraryCalls(instance); } return TRUE;
}
