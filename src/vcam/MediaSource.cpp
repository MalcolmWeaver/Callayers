#include "MediaSource.h"

#include "../shared/FrameProtocol.h"

#include <mfapi.h>
#include <mferror.h>
#include <new>
#include <vector>

extern long* CallayersServerLock();

namespace {

// PINNAME_VIDEO_CAPTURE from ksmedia.h.
const GUID kVideoCapturePin = {0xFB6C4281, 0x0353, 0x11d1, {0x90, 0x5F, 0x00, 0x00, 0xC0, 0xCC, 0x16, 0xBA}};

void AdjustServerLock(bool lock) {
    if (lock) {
        InterlockedIncrement(CallayersServerLock());
    } else {
        InterlockedDecrement(CallayersServerLock());
    }
}

HRESULT CreateVideoType(const GUID& subtype, UINT32 width, UINT32 height, IMFMediaType** type) {
    IMFMediaType* mediaType = nullptr;
    HRESULT hr = MFCreateMediaType(&mediaType);
    if (FAILED(hr)) {
        return hr;
    }
    mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    mediaType->SetGUID(MF_MT_SUBTYPE, subtype);
    mediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    mediaType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    mediaType->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE);
    MFSetAttributeSize(mediaType, MF_MT_FRAME_SIZE, width, height);
    MFSetAttributeRatio(mediaType, MF_MT_FRAME_RATE, callayers::kFps, 1);
    MFSetAttributeRatio(mediaType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    const UINT32 sampleSize = subtype == MFVideoFormat_NV12 ? width * height * 3 / 2 : width * height * 4;
    mediaType->SetUINT32(MF_MT_SAMPLE_SIZE, sampleSize);
    mediaType->SetUINT32(MF_MT_DEFAULT_STRIDE, subtype == MFVideoFormat_NV12 ? width : width * 4);
    *type = mediaType;
    return S_OK;
}

void CopyPlane(BYTE* dest, LONG destPitch, const uint8_t* src, UINT32 srcPitch, UINT32 width, UINT32 height) {
    for (UINT32 y = 0; y < height; ++y) {
        memcpy(dest + static_cast<LONG>(y) * destPitch, src + y * srcPitch, width);
    }
}

void Nv12ToBgra(const uint8_t* nv12, BYTE* dest, LONG destPitch) {
    const uint8_t* yPlane = nv12;
    const uint8_t* uvPlane = nv12 + callayers::kWidth * callayers::kHeight;
    for (UINT32 y = 0; y < callayers::kHeight; ++y) {
        BYTE* row = dest + static_cast<LONG>(y) * destPitch;
        const uint8_t* uvRow = uvPlane + (y / 2) * callayers::kWidth;
        for (UINT32 x = 0; x < callayers::kWidth; ++x) {
            const int Y = yPlane[y * callayers::kWidth + x];
            const int U = uvRow[(x & ~1u)] - 128;
            const int V = uvRow[(x & ~1u) + 1] - 128;
            const int C = Y - 16;
            int R = (298 * C + 409 * V + 128) >> 8;
            int G = (298 * C - 100 * U - 208 * V + 128) >> 8;
            int B = (298 * C + 516 * U + 128) >> 8;
            if (R < 0) R = 0;
            if (R > 255) R = 255;
            if (G < 0) G = 0;
            if (G > 255) G = 255;
            if (B < 0) B = 0;
            if (B > 255) B = 255;
            row[x * 4 + 0] = static_cast<BYTE>(B);
            row[x * 4 + 1] = static_cast<BYTE>(G);
            row[x * 4 + 2] = static_cast<BYTE>(R);
            row[x * 4 + 3] = 255;
        }
    }
}

} // namespace

CallayersSource::CallayersSource() {
    InitializeCriticalSection(&m_lock);
    m_lockReady = true;
    AdjustServerLock(true);
}

CallayersSource::~CallayersSource() {
    Shutdown();
    if (m_lockReady) {
        DeleteCriticalSection(&m_lock);
    }
    AdjustServerLock(false);
}

HRESULT CallayersSource::Initialize(IMFAttributes* activateAttributes) {
    HRESULT hr = MFCreateEventQueue(&m_events);
    if (FAILED(hr)) {
        return hr;
    }
    hr = MFCreateAttributes(&m_attributes, 8);
    if (FAILED(hr)) {
        return hr;
    }
    if (activateAttributes) {
        activateAttributes->CopyAllItems(m_attributes);
    }
    m_attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    m_attributes->SetString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, callayers::kCameraName);
    // Frame Server on current Windows builds expects a legacy sensor profile.
    // A missing profile makes IMFVirtualCamera::Start return MF_E_SHUTDOWN.
    IMFSensorProfileCollection* profiles = nullptr;
    IMFSensorProfile* legacy = nullptr;
    const GUID legacyId = { 0xB4894D81, 0x62B7, 0x4EEC, { 0x87, 0x40, 0x80, 0x65, 0x8C, 0x4A, 0x9D, 0x3E } };
    if (SUCCEEDED(MFCreateSensorProfileCollection(&profiles)) &&
        SUCCEEDED(MFCreateSensorProfile(legacyId, 0, nullptr, &legacy)) &&
        SUCCEEDED(legacy->AddProfileFilter(0, L"((RES==;FRT<=30,1;SUT==))")) &&
        SUCCEEDED(profiles->AddProfile(legacy))) {
        m_attributes->SetUnknown(MF_DEVICEMFT_SENSORPROFILE_COLLECTION, profiles);
    }
    if (legacy) {
        legacy->Release();
    }
    if (profiles) {
        profiles->Release();
    }
    m_stream = new (std::nothrow) CallayersStream(this);
    if (!m_stream) {
        return E_OUTOFMEMORY;
    }
    hr = m_stream->Initialize();
    if (FAILED(hr)) {
        return hr;
    }
    return EnsurePresentation();
}

HRESULT CallayersSource::EnsurePresentation() {
    if (m_presentation) {
        return S_OK;
    }
    IMFStreamDescriptor* descriptor = m_stream->Descriptor();
    HRESULT hr = MFCreatePresentationDescriptor(1, &descriptor, &m_presentation);
    if (FAILED(hr)) {
        return hr;
    }
    m_presentation->SelectStream(0);
    m_presentation->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    m_presentation->SetString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, callayers::kCameraName);
    return S_OK;
}

STDMETHODIMP CallayersSource::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) {
        return E_POINTER;
    }
    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_IMFMediaEventGenerator || riid == IID_IMFMediaSource || riid == IID_IMFMediaSourceEx) {
        *ppv = static_cast<IMFMediaSourceEx*>(this);
    } else if (riid == IID_IMFGetService) {
        *ppv = static_cast<IMFGetService*>(this);
    } else if (riid == __uuidof(ICallayersKsControl)) {
        *ppv = static_cast<ICallayersKsControl*>(this);
    } else if (riid == IID_IMFSampleAllocatorControl) {
        *ppv = static_cast<IMFSampleAllocatorControl*>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CallayersSource::AddRef() {
    return static_cast<ULONG>(InterlockedIncrement(&m_refs));
}

STDMETHODIMP_(ULONG) CallayersSource::Release() {
    const ULONG refs = static_cast<ULONG>(InterlockedDecrement(&m_refs));
    if (refs == 0) {
        delete this;
    }
    return refs;
}

STDMETHODIMP CallayersSource::BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) {
    IMFMediaEventQueue* queue = nullptr;
    EnterCriticalSection(&m_lock);
    if (m_shutdown || !m_events) {
        LeaveCriticalSection(&m_lock);
        return MF_E_SHUTDOWN;
    }
    queue = m_events;
    queue->AddRef();
    LeaveCriticalSection(&m_lock);
    const HRESULT hr = queue->BeginGetEvent(callback, state);
    queue->Release();
    return hr;
}

STDMETHODIMP CallayersSource::EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) {
    IMFMediaEventQueue* queue = nullptr;
    EnterCriticalSection(&m_lock);
    if (m_shutdown || !m_events) {
        LeaveCriticalSection(&m_lock);
        return MF_E_SHUTDOWN;
    }
    queue = m_events;
    queue->AddRef();
    LeaveCriticalSection(&m_lock);
    const HRESULT hr = queue->EndGetEvent(result, event);
    queue->Release();
    return hr;
}

STDMETHODIMP CallayersSource::GetEvent(DWORD flags, IMFMediaEvent** event) {
    IMFMediaEventQueue* queue = nullptr;
    EnterCriticalSection(&m_lock);
    if (m_shutdown || !m_events) {
        LeaveCriticalSection(&m_lock);
        return MF_E_SHUTDOWN;
    }
    queue = m_events;
    queue->AddRef();
    LeaveCriticalSection(&m_lock);
    const HRESULT hr = queue->GetEvent(flags, event);
    queue->Release();
    return hr;
}

STDMETHODIMP CallayersSource::QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status, const PROPVARIANT* value) {
    IMFMediaEventQueue* queue = nullptr;
    EnterCriticalSection(&m_lock);
    if (m_shutdown || !m_events) {
        LeaveCriticalSection(&m_lock);
        return MF_E_SHUTDOWN;
    }
    queue = m_events;
    queue->AddRef();
    LeaveCriticalSection(&m_lock);
    const HRESULT hr = queue->QueueEventParamVar(type, extendedType, status, value);
    queue->Release();
    return hr;
}

STDMETHODIMP CallayersSource::GetCharacteristics(DWORD* characteristics) {
    if (!characteristics) {
        return E_POINTER;
    }
    *characteristics = MFMEDIASOURCE_IS_LIVE;
    return S_OK;
}

STDMETHODIMP CallayersSource::CreatePresentationDescriptor(IMFPresentationDescriptor** descriptor) {
    if (!descriptor) {
        return E_POINTER;
    }
    EnterCriticalSection(&m_lock);
    HRESULT hr = m_shutdown ? MF_E_SHUTDOWN : EnsurePresentation();
    if (SUCCEEDED(hr)) {
        hr = m_presentation->Clone(descriptor);
    }
    LeaveCriticalSection(&m_lock);
    return hr;
}

STDMETHODIMP CallayersSource::Start(IMFPresentationDescriptor* descriptor, const GUID* timeFormat, const PROPVARIANT* startPosition) {
    if (timeFormat && *timeFormat != GUID_NULL) {
        return MF_E_UNSUPPORTED_TIME_FORMAT;
    }
    (void)startPosition;
    EnterCriticalSection(&m_lock);
    if (m_shutdown || !m_stream) {
        LeaveCriticalSection(&m_lock);
        return MF_E_SHUTDOWN;
    }
    CallayersStream* stream = m_stream;
    stream->AddRef();
    IMFMediaEventQueue* queue = m_events;
    if (queue) {
        queue->AddRef();
    }
    BOOL selected = TRUE;
    IMFMediaType* mediaType = nullptr;
    HRESULT hr = S_OK;
    if (descriptor) {
        IMFStreamDescriptor* streamDescriptor = nullptr;
        hr = descriptor->GetStreamDescriptorByIndex(0, &selected, &streamDescriptor);
        if (SUCCEEDED(hr) && selected) {
            IMFMediaTypeHandler* handler = nullptr;
            hr = streamDescriptor->GetMediaTypeHandler(&handler);
            if (SUCCEEDED(hr)) {
                hr = handler->GetCurrentMediaType(&mediaType);
                handler->Release();
            }
        }
        if (streamDescriptor) {
            streamDescriptor->Release();
        }
    }
    const bool announced = m_announced;
    if (selected) {
        m_announced = true;
    }
    LeaveCriticalSection(&m_lock);

    if (SUCCEEDED(hr) && selected && queue) {
        hr = queue->QueueEventParamUnk(announced ? MEUpdatedStream : MENewStream, GUID_NULL, S_OK, static_cast<IMFMediaStream*>(stream));
    }
    if (SUCCEEDED(hr)) {
        hr = selected ? stream->Start(mediaType, true) : stream->Stop(true);
    }
    if (mediaType) {
        mediaType->Release();
    }
    if (SUCCEEDED(hr) && queue) {
        PROPVARIANT startTime{};
        PropVariantInit(&startTime);
        startTime.vt = VT_I8;
        startTime.hVal.QuadPart = MFGetSystemTime();
        hr = queue->QueueEventParamVar(MESourceStarted, GUID_NULL, S_OK, &startTime);
        PropVariantClear(&startTime);
    }
    if (queue) {
        queue->Release();
    }
    stream->Release();
    return hr;
}

STDMETHODIMP CallayersSource::Stop() {
    EnterCriticalSection(&m_lock);
    if (m_shutdown || !m_stream) {
        LeaveCriticalSection(&m_lock);
        return MF_E_SHUTDOWN;
    }
    CallayersStream* stream = m_stream;
    stream->AddRef();
    IMFMediaEventQueue* queue = m_events;
    if (queue) {
        queue->AddRef();
    }
    LeaveCriticalSection(&m_lock);
    HRESULT hr = stream->Stop(true);
    if (SUCCEEDED(hr) && queue) {
        hr = queue->QueueEventParamVar(MESourceStopped, GUID_NULL, S_OK, nullptr);
    }
    if (queue) {
        queue->Release();
    }
    stream->Release();
    return hr;
}

STDMETHODIMP CallayersSource::Pause() {
    EnterCriticalSection(&m_lock);
    if (m_shutdown || !m_stream) {
        LeaveCriticalSection(&m_lock);
        return MF_E_SHUTDOWN;
    }
    CallayersStream* stream = m_stream;
    stream->AddRef();
    IMFMediaEventQueue* queue = m_events;
    if (queue) {
        queue->AddRef();
    }
    LeaveCriticalSection(&m_lock);
    const HRESULT hrState = stream->SetStreamState(MF_STREAM_STATE_PAUSED);
    HRESULT hr = hrState;
    if (SUCCEEDED(hr) && queue) {
        hr = queue->QueueEventParamVar(MESourcePaused, GUID_NULL, S_OK, nullptr);
    }
    if (queue) {
        queue->Release();
    }
    stream->Release();
    return hr;
}

STDMETHODIMP CallayersSource::Shutdown() {
    if (!m_lockReady) {
        return S_OK;
    }
    EnterCriticalSection(&m_lock);
    if (m_shutdown) {
        LeaveCriticalSection(&m_lock);
        return S_OK;
    }
    m_shutdown = true;
    CallayersStream* stream = m_stream;
    m_stream = nullptr;
    if (m_events) {
        m_events->Shutdown();
        m_events->Release();
        m_events = nullptr;
    }
    if (m_presentation) {
        m_presentation->Release();
        m_presentation = nullptr;
    }
    if (m_attributes) {
        m_attributes->Release();
        m_attributes = nullptr;
    }
    LeaveCriticalSection(&m_lock);
    if (stream) {
        stream->Shutdown();
        stream->Release();
    }
    return S_OK;
}

STDMETHODIMP CallayersSource::GetSourceAttributes(IMFAttributes** attributes) {
    if (!attributes) {
        return E_POINTER;
    }
    EnterCriticalSection(&m_lock);
    if (m_shutdown || !m_attributes) {
        LeaveCriticalSection(&m_lock);
        return MF_E_SHUTDOWN;
    }
    *attributes = m_attributes;
    m_attributes->AddRef();
    LeaveCriticalSection(&m_lock);
    return S_OK;
}

STDMETHODIMP CallayersSource::GetStreamAttributes(DWORD streamIdentifier, IMFAttributes** attributes) {
    if (streamIdentifier != 0) {
        return E_INVALIDARG;
    }
    EnterCriticalSection(&m_lock);
    const HRESULT hr = m_shutdown || !m_stream ? MF_E_SHUTDOWN : m_stream->Attributes(attributes);
    LeaveCriticalSection(&m_lock);
    return hr;
}

STDMETHODIMP CallayersSource::SetD3DManager(IUnknown*) {
    return S_OK;
}

STDMETHODIMP CallayersSource::GetService(REFGUID service, REFIID riid, void** object) {
    if (service == MF_MEDIASOURCE_SERVICE) {
        return QueryInterface(riid, object);
    }
    return MF_E_UNSUPPORTED_SERVICE;
}

STDMETHODIMP CallayersSource::KsProperty(PKSPROPERTY property, ULONG, void* data, ULONG dataLength, ULONG* bytesReturned) {
    if (bytesReturned) {
        *bytesReturned = 0;
    }
    if (property && (property->Flags & KSPROPERTY_TYPE_BASICSUPPORT) && data && dataLength >= sizeof(ULONG)) {
        *static_cast<ULONG*>(data) = 0;
        if (bytesReturned) {
            *bytesReturned = sizeof(ULONG);
        }
        return S_OK;
    }
    return static_cast<HRESULT>(0xC00000BBu);
}

STDMETHODIMP CallayersSource::KsMethod(PKSMETHOD, ULONG, void*, ULONG, ULONG* bytesReturned) {
    if (bytesReturned) {
        *bytesReturned = 0;
    }
    return static_cast<HRESULT>(0xC00000BBu);
}

STDMETHODIMP CallayersSource::KsEvent(PKSEVENT, ULONG, void*, ULONG, ULONG* bytesReturned) {
    if (bytesReturned) {
        *bytesReturned = 0;
    }
    return static_cast<HRESULT>(0xC00000BBu);
}

STDMETHODIMP CallayersSource::SetDefaultAllocator(DWORD, IUnknown* allocator) {
    if (!m_stream) {
        return MF_E_SHUTDOWN;
    }
    return m_stream->SetAllocator(allocator);
}

STDMETHODIMP CallayersSource::GetAllocatorUsage(DWORD outputStreamID, DWORD* inputStreamID, MFSampleAllocatorUsage* usage) {
    if (inputStreamID) {
        *inputStreamID = outputStreamID;
    }
    if (!usage) {
        return E_POINTER;
    }
    *usage = MFSampleAllocatorUsage_UsesProvidedAllocator;
    return S_OK;
}

CallayersStream::CallayersStream(CallayersSource* parent) : m_parent(parent) {
    InitializeCriticalSection(&m_lock);
    m_lockReady = true;
}

CallayersStream::~CallayersStream() {
    Shutdown();
    if (m_lockReady) {
        DeleteCriticalSection(&m_lock);
    }
}

HRESULT CallayersStream::Initialize() {
    IMFMediaType* nv12 = nullptr;
    IMFMediaType* rgb = nullptr;
    HRESULT hr = CreateVideoType(MFVideoFormat_NV12, callayers::kWidth, callayers::kHeight, &nv12);
    if (SUCCEEDED(hr)) {
        hr = CreateVideoType(MFVideoFormat_RGB32, callayers::kWidth, callayers::kHeight, &rgb);
    }
    IMFMediaType* types[2] = {nv12, rgb};
    if (SUCCEEDED(hr)) {
        hr = MFCreateStreamDescriptor(0, 2, types, &m_descriptor);
    }
    if (SUCCEEDED(hr)) {
        m_descriptor->SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, kVideoCapturePin);
        m_descriptor->SetUINT32(MF_DEVICESTREAM_STREAM_ID, 0);
        m_descriptor->SetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED, 1);
        m_descriptor->SetUINT32(MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES, MFFrameSourceTypes_Color);
        IMFMediaTypeHandler* handler = nullptr;
        hr = m_descriptor->GetMediaTypeHandler(&handler);
        if (SUCCEEDED(hr)) {
            hr = handler->SetCurrentMediaType(nv12);
            handler->Release();
        }
    }
    if (SUCCEEDED(hr)) {
        hr = MFCreateEventQueue(&m_events);
    }
    if (nv12) {
        nv12->Release();
    }
    if (rgb) {
        rgb->Release();
    }
    return hr;
}

HRESULT CallayersStream::SetAllocator(IUnknown* unknown) {
    if (!unknown) {
        return E_POINTER;
    }
    IMFVideoSampleAllocator* allocator = nullptr;
    const HRESULT hr = unknown->QueryInterface(IID_PPV_ARGS(&allocator));
    if (FAILED(hr)) {
        return hr;
    }
    EnterCriticalSection(&m_lock);
    if (m_provided) {
        m_provided->UninitializeSampleAllocator();
        m_provided->Release();
    }
    m_provided = allocator;
    HRESULT started = S_OK;
    if (m_mediaType) {
        started = m_provided->InitializeSampleAllocator(4, m_mediaType);
    }
    LeaveCriticalSection(&m_lock);
    return started;
}

HRESULT CallayersStream::Attributes(IMFAttributes** attributes) {
    if (!m_descriptor) {
        return E_UNEXPECTED;
    }
    return m_descriptor->QueryInterface(IID_PPV_ARGS(attributes));
}

HRESULT CallayersStream::Check() {
    if (m_shutdown || !m_events) {
        return MF_E_SHUTDOWN;
    }
    return S_OK;
}

HRESULT CallayersStream::ConfigureAllocator(IMFMediaType* mediaType) {
    if (m_allocator) {
        m_allocator->UninitializeSampleAllocator();
        m_allocator->Release();
        m_allocator = nullptr;
    }
    HRESULT hr = MFCreateVideoSampleAllocatorEx(IID_PPV_ARGS(&m_allocator));
    if (FAILED(hr)) {
        return hr;
    }
    return m_allocator->InitializeSampleAllocator(8, mediaType);
}

HRESULT CallayersStream::Start(IMFMediaType* mediaType, bool sendEvent) {
    EnterCriticalSection(&m_lock);
    HRESULT hr = Check();
    if (SUCCEEDED(hr)) {
        if (mediaType) {
            if (m_mediaType) {
                m_mediaType->Release();
            }
            m_mediaType = mediaType;
            m_mediaType->AddRef();
        }
        if (!m_mediaType) {
            hr = MF_E_NOT_INITIALIZED;
        }
    }
    IMFMediaEventQueue* queue = nullptr;
    if (SUCCEEDED(hr)) {
        m_state = MF_STREAM_STATE_RUNNING;
        if (sendEvent && m_events) {
            queue = m_events;
            queue->AddRef();
        }
    }
    LeaveCriticalSection(&m_lock);
    if (queue) {
        const HRESULT eventHr = queue->QueueEventParamVar(MEStreamStarted, GUID_NULL, S_OK, nullptr);
        queue->Release();
        if (FAILED(eventHr)) {
            return eventHr;
        }
    }
    return hr;
}

HRESULT CallayersStream::Stop(bool sendEvent) {
    EnterCriticalSection(&m_lock);
    HRESULT hr = Check();
    IMFMediaEventQueue* queue = nullptr;
    if (SUCCEEDED(hr)) {
        m_state = MF_STREAM_STATE_STOPPED;
        if (sendEvent && m_events) {
            queue = m_events;
            queue->AddRef();
        }
    }
    LeaveCriticalSection(&m_lock);
    if (queue) {
        const HRESULT eventHr = queue->QueueEventParamVar(MEStreamStopped, GUID_NULL, S_OK, nullptr);
        queue->Release();
        if (FAILED(eventHr)) {
            return eventHr;
        }
    }
    return hr;
}

STDMETHODIMP CallayersStream::SetStreamState(MF_STREAM_STATE state) {
    if (state == MF_STREAM_STATE_RUNNING) {
        return Start(nullptr, false);
    }
    if (state == MF_STREAM_STATE_STOPPED) {
        return Stop(false);
    }
    if (state == MF_STREAM_STATE_PAUSED) {
        EnterCriticalSection(&m_lock);
        if (m_state != MF_STREAM_STATE_RUNNING) {
            LeaveCriticalSection(&m_lock);
            return MF_E_INVALID_STATE_TRANSITION;
        }
        m_state = MF_STREAM_STATE_PAUSED;
        LeaveCriticalSection(&m_lock);
        return S_OK;
    }
    return MF_E_INVALID_STATE_TRANSITION;
}

STDMETHODIMP CallayersStream::GetStreamState(MF_STREAM_STATE* state) {
    if (!state) {
        return E_POINTER;
    }
    EnterCriticalSection(&m_lock);
    *state = m_state;
    LeaveCriticalSection(&m_lock);
    return S_OK;
}

HRESULT CallayersStream::CopyFrame(BYTE* dest, LONG pitch, DWORD, const GUID& subtype) {
    std::vector<uint8_t> nv12(callayers::kNv12Size);
    callayers::MappedFrames frames;
    const bool live = frames.OpenOrCreate() && frames.TryCopyLatest(nv12.data());
    frames.Close();
    if (!live) {
        callayers::WriteIdleNv12(nv12.data());
    }
    if (subtype == MFVideoFormat_NV12) {
        CopyPlane(dest, pitch, nv12.data(), callayers::kWidth, callayers::kWidth, callayers::kHeight);
        CopyPlane(dest + pitch * callayers::kHeight, pitch, nv12.data() + callayers::kWidth * callayers::kHeight, callayers::kWidth, callayers::kWidth, callayers::kHeight / 2);
        return S_OK;
    }
    if (subtype == MFVideoFormat_RGB32) {
        Nv12ToBgra(nv12.data(), dest, pitch);
        return S_OK;
    }
    return MF_E_INVALIDMEDIATYPE;
}

STDMETHODIMP CallayersStream::RequestSample(IUnknown* token) {
    EnterCriticalSection(&m_lock);
    if (FAILED(Check()) || m_state != MF_STREAM_STATE_RUNNING || !m_mediaType) {
        LeaveCriticalSection(&m_lock);
        return MF_E_INVALIDREQUEST;
    }
    IMFSample* sample = nullptr;
    IMFMediaBuffer* buffer = nullptr;
    GUID subtype = GUID_NULL;
    HRESULT hr = m_mediaType->GetGUID(MF_MT_SUBTYPE, &subtype);
    const DWORD bytes = subtype == MFVideoFormat_RGB32 ? callayers::kWidth * callayers::kHeight * 4 : callayers::kNv12Size;
    if (SUCCEEDED(hr) && m_provided) {
        hr = m_provided->AllocateSample(&sample);
        if (SUCCEEDED(hr)) {
            hr = sample->GetBufferByIndex(0, &buffer);
        }
    } else if (SUCCEEDED(hr)) {
        hr = MFCreateSample(&sample);
        if (SUCCEEDED(hr)) {
            hr = MFCreateMemoryBuffer(bytes, &buffer);
        }
        if (SUCCEEDED(hr)) {
            hr = sample->AddBuffer(buffer);
        }
    }
    if (SUCCEEDED(hr)) {
        IMF2DBuffer* buffer2d = nullptr;
        if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&buffer2d)))) {
            BYTE* scan = nullptr;
            LONG pitch = 0;
            hr = buffer2d->Lock2D(&scan, &pitch);
            if (SUCCEEDED(hr)) {
                const DWORD length = subtype == MFVideoFormat_NV12
                    ? static_cast<DWORD>(pitch > 0 ? pitch * callayers::kHeight * 3 / 2 : 0)
                    : static_cast<DWORD>(pitch > 0 ? pitch * callayers::kHeight : 0);
                hr = CopyFrame(scan, pitch, length, subtype);
                buffer2d->Unlock2D();
            }
            buffer2d->Release();
        } else {
            BYTE* data = nullptr;
            DWORD maxLength = 0;
            DWORD current = 0;
            hr = buffer->Lock(&data, &maxLength, &current);
            if (SUCCEEDED(hr)) {
                const LONG pitch = subtype == MFVideoFormat_NV12 ? static_cast<LONG>(callayers::kWidth) : static_cast<LONG>(callayers::kWidth * 4);
                hr = CopyFrame(data, pitch, maxLength, subtype);
                const DWORD written = subtype == MFVideoFormat_NV12 ? callayers::kNv12Size : callayers::kWidth * callayers::kHeight * 4;
                buffer->SetCurrentLength(written);
                buffer->Unlock();
            }
        }
    }
    if (SUCCEEDED(hr)) {
        const LONGLONG duration = 333333;
        sample->SetSampleTime(static_cast<LONGLONG>(m_frameIndex) * duration);
        sample->SetSampleDuration(duration);
        sample->SetUINT32(MFSampleExtension_CleanPoint, TRUE);
        if (m_frameIndex == 0) {
            sample->SetUINT32(MFSampleExtension_Discontinuity, TRUE);
        }
        ++m_frameIndex;
        if (token) {
            sample->SetUnknown(MFSampleExtension_Token, token);
        }
    }
    IMFMediaEventQueue* queue = nullptr;
    if (SUCCEEDED(hr) && m_events) {
        queue = m_events;
        queue->AddRef();
    }
    LeaveCriticalSection(&m_lock);
    if (queue) {
        hr = queue->QueueEventParamUnk(MEMediaSample, GUID_NULL, S_OK, sample);
        queue->Release();
    }
    if (buffer) {
        buffer->Release();
    }
    if (sample) {
        sample->Release();
    }
    return hr;
}

STDMETHODIMP CallayersStream::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) {
        return E_POINTER;
    }
    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_IMFMediaEventGenerator || riid == IID_IMFMediaStream || riid == IID_IMFMediaStream2) {
        *ppv = static_cast<IMFMediaStream2*>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CallayersStream::AddRef() {
    return static_cast<ULONG>(InterlockedIncrement(&m_refs));
}

STDMETHODIMP_(ULONG) CallayersStream::Release() {
    const ULONG refs = static_cast<ULONG>(InterlockedDecrement(&m_refs));
    if (refs == 0) {
        delete this;
    }
    return refs;
}

STDMETHODIMP CallayersStream::BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) {
    IMFMediaEventQueue* queue = nullptr;
    EnterCriticalSection(&m_lock);
    if (FAILED(Check())) {
        LeaveCriticalSection(&m_lock);
        return MF_E_SHUTDOWN;
    }
    queue = m_events;
    queue->AddRef();
    LeaveCriticalSection(&m_lock);
    const HRESULT hr = queue->BeginGetEvent(callback, state);
    queue->Release();
    return hr;
}

STDMETHODIMP CallayersStream::EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) {
    IMFMediaEventQueue* queue = nullptr;
    EnterCriticalSection(&m_lock);
    if (FAILED(Check())) {
        LeaveCriticalSection(&m_lock);
        return MF_E_SHUTDOWN;
    }
    queue = m_events;
    queue->AddRef();
    LeaveCriticalSection(&m_lock);
    const HRESULT hr = queue->EndGetEvent(result, event);
    queue->Release();
    return hr;
}

STDMETHODIMP CallayersStream::GetEvent(DWORD flags, IMFMediaEvent** event) {
    IMFMediaEventQueue* queue = nullptr;
    EnterCriticalSection(&m_lock);
    if (FAILED(Check())) {
        LeaveCriticalSection(&m_lock);
        return MF_E_SHUTDOWN;
    }
    queue = m_events;
    queue->AddRef();
    LeaveCriticalSection(&m_lock);
    const HRESULT hr = queue->GetEvent(flags, event);
    queue->Release();
    return hr;
}

STDMETHODIMP CallayersStream::QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status, const PROPVARIANT* value) {
    IMFMediaEventQueue* queue = nullptr;
    EnterCriticalSection(&m_lock);
    if (FAILED(Check())) {
        LeaveCriticalSection(&m_lock);
        return MF_E_SHUTDOWN;
    }
    queue = m_events;
    queue->AddRef();
    LeaveCriticalSection(&m_lock);
    const HRESULT hr = queue->QueueEventParamVar(type, extendedType, status, value);
    queue->Release();
    return hr;
}

STDMETHODIMP CallayersStream::GetMediaSource(IMFMediaSource** source) {
    if (!source) {
        return E_POINTER;
    }
    EnterCriticalSection(&m_lock);
    if (FAILED(Check()) || !m_parent) {
        LeaveCriticalSection(&m_lock);
        return MF_E_SHUTDOWN;
    }
    *source = m_parent;
    m_parent->AddRef();
    LeaveCriticalSection(&m_lock);
    return S_OK;
}

STDMETHODIMP CallayersStream::GetStreamDescriptor(IMFStreamDescriptor** descriptor) {
    if (!descriptor) {
        return E_POINTER;
    }
    EnterCriticalSection(&m_lock);
    if (FAILED(Check()) || !m_descriptor) {
        LeaveCriticalSection(&m_lock);
        return MF_E_SHUTDOWN;
    }
    *descriptor = m_descriptor;
    m_descriptor->AddRef();
    LeaveCriticalSection(&m_lock);
    return S_OK;
}

void CallayersStream::Shutdown() {
    if (!m_lockReady) {
        return;
    }
    EnterCriticalSection(&m_lock);
    m_shutdown = true;
    m_parent = nullptr;
    if (m_events) {
        m_events->Shutdown();
        m_events->Release();
        m_events = nullptr;
    }
    if (m_descriptor) {
        m_descriptor->Release();
        m_descriptor = nullptr;
    }
    if (m_mediaType) {
        m_mediaType->Release();
        m_mediaType = nullptr;
    }
    if (m_allocator) {
        m_allocator->UninitializeSampleAllocator();
        m_allocator->Release();
        m_allocator = nullptr;
    }
    if (m_provided) {
        m_provided->UninitializeSampleAllocator();
        m_provided->Release();
        m_provided = nullptr;
    }
    LeaveCriticalSection(&m_lock);
}

CallayersActivate::CallayersActivate() {
    MFCreateAttributes(&m_attributes, 4);
    AdjustServerLock(true);
}

CallayersActivate::~CallayersActivate() {
    ShutdownObject();
    if (m_attributes) {
        m_attributes->Release();
        m_attributes = nullptr;
    }
    AdjustServerLock(false);
}

STDMETHODIMP CallayersActivate::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) {
        return E_POINTER;
    }
    *ppv = nullptr;
    if (riid == IID_IUnknown || riid == IID_IMFAttributes || riid == IID_IMFActivate) {
        *ppv = static_cast<IMFActivate*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CallayersActivate::AddRef() {
    return static_cast<ULONG>(InterlockedIncrement(&m_refs));
}

STDMETHODIMP_(ULONG) CallayersActivate::Release() {
    const ULONG refs = static_cast<ULONG>(InterlockedDecrement(&m_refs));
    if (refs == 0) {
        delete this;
    }
    return refs;
}

STDMETHODIMP CallayersActivate::ActivateObject(REFIID riid, void** ppv) {
    if (!ppv) {
        return E_POINTER;
    }
    *ppv = nullptr;
    if (!m_source) {
        MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
        auto* source = new (std::nothrow) CallayersSource();
        if (!source) {
            return E_OUTOFMEMORY;
        }
        const HRESULT hr = source->Initialize(m_attributes);
        if (FAILED(hr)) {
            source->Release();
            return hr;
        }
        m_source = source;
    }
    return m_source->QueryInterface(riid, ppv);
}

STDMETHODIMP CallayersActivate::ShutdownObject() {
    if (m_source) {
        m_source->Shutdown();
        m_source->Release();
        m_source = nullptr;
    }
    return S_OK;
}

STDMETHODIMP CallayersActivate::DetachObject() {
    if (m_source) {
        m_source->Release();
        m_source = nullptr;
    }
    return S_OK;
}

#define FORWARD_ATTR(call) (m_attributes ? m_attributes->call : MF_E_NOT_INITIALIZED)

STDMETHODIMP CallayersActivate::GetItem(REFGUID key, PROPVARIANT* value) { return FORWARD_ATTR(GetItem(key, value)); }
STDMETHODIMP CallayersActivate::GetItemType(REFGUID key, MF_ATTRIBUTE_TYPE* type) { return FORWARD_ATTR(GetItemType(key, type)); }
STDMETHODIMP CallayersActivate::CompareItem(REFGUID key, REFPROPVARIANT value, BOOL* result) { return FORWARD_ATTR(CompareItem(key, value, result)); }
STDMETHODIMP CallayersActivate::Compare(IMFAttributes* theirs, MF_ATTRIBUTES_MATCH_TYPE matchType, BOOL* result) { return FORWARD_ATTR(Compare(theirs, matchType, result)); }
STDMETHODIMP CallayersActivate::GetUINT32(REFGUID key, UINT32* value) { return FORWARD_ATTR(GetUINT32(key, value)); }
STDMETHODIMP CallayersActivate::GetUINT64(REFGUID key, UINT64* value) { return FORWARD_ATTR(GetUINT64(key, value)); }
STDMETHODIMP CallayersActivate::GetDouble(REFGUID key, double* value) { return FORWARD_ATTR(GetDouble(key, value)); }
STDMETHODIMP CallayersActivate::GetGUID(REFGUID key, GUID* value) { return FORWARD_ATTR(GetGUID(key, value)); }
STDMETHODIMP CallayersActivate::GetStringLength(REFGUID key, UINT32* length) { return FORWARD_ATTR(GetStringLength(key, length)); }
STDMETHODIMP CallayersActivate::GetString(REFGUID key, LPWSTR value, UINT32 bufferLength, UINT32* written) { return FORWARD_ATTR(GetString(key, value, bufferLength, written)); }
STDMETHODIMP CallayersActivate::GetAllocatedString(REFGUID key, LPWSTR* value, UINT32* length) { return FORWARD_ATTR(GetAllocatedString(key, value, length)); }
STDMETHODIMP CallayersActivate::GetBlobSize(REFGUID key, UINT32* size) { return FORWARD_ATTR(GetBlobSize(key, size)); }
STDMETHODIMP CallayersActivate::GetBlob(REFGUID key, UINT8* buffer, UINT32 bufferSize, UINT32* written) { return FORWARD_ATTR(GetBlob(key, buffer, bufferSize, written)); }
STDMETHODIMP CallayersActivate::GetAllocatedBlob(REFGUID key, UINT8** buffer, UINT32* size) { return FORWARD_ATTR(GetAllocatedBlob(key, buffer, size)); }
STDMETHODIMP CallayersActivate::GetUnknown(REFGUID key, REFIID riid, LPVOID* object) { return FORWARD_ATTR(GetUnknown(key, riid, object)); }
STDMETHODIMP CallayersActivate::SetItem(REFGUID key, REFPROPVARIANT value) { return FORWARD_ATTR(SetItem(key, value)); }
STDMETHODIMP CallayersActivate::DeleteItem(REFGUID key) { return FORWARD_ATTR(DeleteItem(key)); }
STDMETHODIMP CallayersActivate::DeleteAllItems() { return FORWARD_ATTR(DeleteAllItems()); }
STDMETHODIMP CallayersActivate::SetUINT32(REFGUID key, UINT32 value) { return FORWARD_ATTR(SetUINT32(key, value)); }
STDMETHODIMP CallayersActivate::SetUINT64(REFGUID key, UINT64 value) { return FORWARD_ATTR(SetUINT64(key, value)); }
STDMETHODIMP CallayersActivate::SetDouble(REFGUID key, double value) { return FORWARD_ATTR(SetDouble(key, value)); }
STDMETHODIMP CallayersActivate::SetGUID(REFGUID key, REFGUID value) { return FORWARD_ATTR(SetGUID(key, value)); }
STDMETHODIMP CallayersActivate::SetString(REFGUID key, LPCWSTR value) { return FORWARD_ATTR(SetString(key, value)); }
STDMETHODIMP CallayersActivate::SetBlob(REFGUID key, const UINT8* buffer, UINT32 size) { return FORWARD_ATTR(SetBlob(key, buffer, size)); }
STDMETHODIMP CallayersActivate::SetUnknown(REFGUID key, IUnknown* unknown) { return FORWARD_ATTR(SetUnknown(key, unknown)); }
STDMETHODIMP CallayersActivate::LockStore() { return FORWARD_ATTR(LockStore()); }
STDMETHODIMP CallayersActivate::UnlockStore() { return FORWARD_ATTR(UnlockStore()); }
STDMETHODIMP CallayersActivate::GetCount(UINT32* count) { return FORWARD_ATTR(GetCount(count)); }
STDMETHODIMP CallayersActivate::GetItemByIndex(UINT32 index, GUID* key, PROPVARIANT* value) { return FORWARD_ATTR(GetItemByIndex(index, key, value)); }
STDMETHODIMP CallayersActivate::CopyAllItems(IMFAttributes* dest) { return FORWARD_ATTR(CopyAllItems(dest)); }

STDMETHODIMP CallayersClassFactory::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) {
        return E_POINTER;
    }
    if (riid == IID_IUnknown || riid == IID_IClassFactory) {
        *ppv = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CallayersClassFactory::AddRef() {
    return static_cast<ULONG>(InterlockedIncrement(&m_refs));
}

STDMETHODIMP_(ULONG) CallayersClassFactory::Release() {
    const ULONG refs = static_cast<ULONG>(InterlockedDecrement(&m_refs));
    if (refs == 0) {
        delete this;
    }
    return refs;
}

STDMETHODIMP CallayersClassFactory::CreateInstance(IUnknown* outer, REFIID riid, void** object) {
    if (outer) {
        return CLASS_E_NOAGGREGATION;
    }
    auto* activate = new (std::nothrow) CallayersActivate();
    if (!activate) {
        return E_OUTOFMEMORY;
    }
    const HRESULT hr = activate->QueryInterface(riid, object);
    activate->Release();
    return hr;
}

STDMETHODIMP CallayersClassFactory::LockServer(BOOL lock) {
    AdjustServerLock(lock == TRUE);
    return S_OK;
}
