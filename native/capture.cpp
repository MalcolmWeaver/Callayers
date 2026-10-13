#include "capture.h"
#include <mferror.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>

using Microsoft::WRL::ComPtr;
namespace callayers {
static constexpr DWORD VideoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
static constexpr DWORD AllStreams = static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS);
std::vector<CameraDevice> EnumerateCameras() {
    std::vector<CameraDevice> devices;
    ComPtr<IMFAttributes> attributes;
    if (FAILED(MFCreateAttributes(&attributes, 1))) return devices;
    attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** list = nullptr; UINT count = 0;
    if (FAILED(MFEnumDeviceSources(attributes.Get(), &list, &count))) return devices;
    for (UINT i = 0; i < count; ++i) {
        wchar_t *name = nullptr, *id = nullptr; UINT length = 0;
        list[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &length);
        list[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &id, &length);
        if (name && id && std::wstring(name).find(L"Callayers") == std::wstring::npos) devices.push_back({name, id});
        CoTaskMemFree(name); CoTaskMemFree(id); list[i]->Release();
    }
    CoTaskMemFree(list);
    return devices;
}

class ReaderCallback final : public IMFSourceReaderCallback {
    std::atomic<ULONG> refs_{1};
    std::mutex readerMutex_;
    ComPtr<IMFSourceReader> reader_;
    std::shared_ptr<CaptureState> state_;
    UINT width_, height_;
    LONG stride_;
public:
    ReaderCallback(std::shared_ptr<CaptureState> state, UINT w, UINT h, LONG stride)
        : state_(std::move(state)), width_(w), height_(h), stride_(stride) {}
    void configure(UINT w, UINT h, LONG stride) { width_ = w; height_ = h; stride_ = stride; }
    void attach(IMFSourceReader* reader) { std::lock_guard lock(readerMutex_); reader_ = reader; }
    void detach() { std::lock_guard lock(readerMutex_); reader_.Reset(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id == IID_IUnknown || id == __uuidof(IMFSourceReaderCallback)) *out = static_cast<IMFSourceReaderCallback*>(this);
        if (!*out) return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE OnEvent(DWORD, IMFMediaEvent*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnFlush(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnReadSample(HRESULT hr, DWORD, DWORD flags, LONGLONG, IMFSample* sample) override {
        if (!state_->running) return S_OK;
        if (FAILED(hr) || (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED))) {
            state_->error = FAILED(hr) ? hr : MF_E_HW_MFT_FAILED_START_STREAMING;
            state_->running = false;
            return S_OK;
        }
        if (sample) {
            ComPtr<IMFMediaBuffer> buffer;
            if (SUCCEEDED(hr = sample->ConvertToContiguousBuffer(&buffer))) {
                ComPtr<IMF2DBuffer> twoD;
                BYTE* bytes = nullptr; LONG stride = stride_; DWORD size = 0;
                bool locked2D = SUCCEEDED(buffer.As(&twoD)) && SUCCEEDED(twoD->Lock2D(&bytes, &stride));
                if (locked2D || SUCCEEDED(hr = buffer->Lock(&bytes, nullptr, &size))) {
                    if (locked2D || size >= size_t(std::abs(stride)) * height_) {
                        auto image = std::make_shared<CameraImage>();
                        image->width = width_; image->height = height_; image->tick = GetTickCount64();
                        image->pixels.resize(size_t(width_) * height_ * 4);
                        if (!locked2D && stride < 0) bytes += size_t(-stride) * (height_ - 1);
                        for (UINT y = 0; y < height_; ++y)
                            memcpy(image->pixels.data() + size_t(y) * width_ * 4, bytes + ptrdiff_t(y) * stride, size_t(width_) * 4);
                        std::lock_guard lock(state_->mutex);
                        state_->latest = std::move(image);
                    } else hr = E_FAIL;
                    if (locked2D) twoD->Unlock2D(); else buffer->Unlock();
                }
            }
            if (FAILED(hr)) { state_->error = hr; state_->running = false; return S_OK; }
        }
        std::lock_guard lock(readerMutex_);
        if (state_->running && reader_) {
            hr = reader_->ReadSample(VideoStream, 0, nullptr, nullptr, nullptr, nullptr);
            if (FAILED(hr)) { state_->error = hr; state_->running = false; }
        }
        return S_OK;
    }
};

HRESULT Capture::start(const std::wstring& identity) {
    stop();
    state = std::make_shared<CaptureState>();
    ComPtr<IMFAttributes> sourceAttrs;
    HRESULT hr = MFCreateAttributes(&sourceAttrs, 2);
    if (FAILED(hr)) return hr;
    sourceAttrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    sourceAttrs->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, identity.c_str());
    if (FAILED(hr = MFCreateDeviceSource(sourceAttrs.Get(), &source_))) return hr;
    auto cb = new ReaderCallback(state, 0, 0, 0);
    callback_.Attach(cb);
    ComPtr<IMFAttributes> attrs;
    if (FAILED(hr = MFCreateAttributes(&attrs, 3))) { stop(); return hr; }
    attrs->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, cb);
    attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    if (FAILED(hr = MFCreateSourceReaderFromMediaSource(source_.Get(), attrs.Get(), &reader_))) { stop(); return hr; }
    reader_->SetStreamSelection(AllStreams, FALSE);
    reader_->SetStreamSelection(VideoStream, TRUE);
    // Prefer native 720p/30. Keep a usable native fallback rather than pretending
    // every USB camera accepts an arbitrary source-reader output size.
    ComPtr<IMFMediaType> best;
    double bestScore = std::numeric_limits<double>::max();
    for (DWORD index = 0; ; ++index) {
        ComPtr<IMFMediaType> type;
        if (FAILED(reader_->GetNativeMediaType(VideoStream, index, &type))) break;
        UINT32 w = 0, h = 0, n = 0, d = 1;
        if (FAILED(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &h))) continue;
        MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &n, &d);
        double fps = d ? double(n) / d : 0;
        double score = std::abs(double(w) - 1280) + std::abs(double(h) - 720) + 100 * std::abs(fps - 30);
        if (score < bestScore) { bestScore = score; best = type; }
    }
    if (best) reader_->SetCurrentMediaType(VideoStream, nullptr, best.Get());
    ComPtr<IMFMediaType> rgb;
    if (FAILED(hr = MFCreateMediaType(&rgb))) { stop(); return hr; }
    rgb->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    rgb->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (FAILED(hr = reader_->SetCurrentMediaType(VideoStream, nullptr, rgb.Get()))) { stop(); return hr; }
    ComPtr<IMFMediaType> actual;
    reader_->GetCurrentMediaType(VideoStream, &actual);
    UINT32 width = 0, height = 0, rawStride = 0;
    if (!actual || FAILED(MFGetAttributeSize(actual.Get(), MF_MT_FRAME_SIZE, &width, &height)) || !width || !height) { stop(); return E_FAIL; }
    LONG stride = LONG(width * 4);
    if (SUCCEEDED(actual->GetUINT32(MF_MT_DEFAULT_STRIDE, &rawStride))) stride = LONG(rawStride);
    else MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, width, &stride);
    cb->configure(width, height, stride);
    cb->attach(reader_.Get());
    state->running = true;
    hr = reader_->ReadSample(VideoStream, 0, nullptr, nullptr, nullptr, nullptr);
    if (FAILED(hr)) { stop(); return hr; }
    return S_OK;
}
void Capture::stop() {
    state->running = false;
    if (callback_) static_cast<ReaderCallback*>(callback_.Get())->detach();
    if (reader_) reader_->Flush(AllStreams);
    if (source_) source_->Shutdown();
    reader_.Reset(); source_.Reset(); callback_.Reset();
    std::lock_guard lock(state->mutex); state->latest.reset();
}
}
