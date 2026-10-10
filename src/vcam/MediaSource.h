#pragma once

#include <mfidl.h>
#include <ks.h>

// {28F54685-06FD-11D2-B27A-00A0C9223196}
struct __declspec(uuid("28F54685-06FD-11D2-B27A-00A0C9223196")) ICallayersKsControl : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE KsProperty(PKSPROPERTY property, ULONG propertyLength, void* data, ULONG dataLength, ULONG* bytesReturned) = 0;
    virtual HRESULT STDMETHODCALLTYPE KsMethod(PKSMETHOD method, ULONG methodLength, void* data, ULONG dataLength, ULONG* bytesReturned) = 0;
    virtual HRESULT STDMETHODCALLTYPE KsEvent(PKSEVENT event, ULONG eventLength, void* data, ULONG dataLength, ULONG* bytesReturned) = 0;
};

class CallayersStream;

class CallayersSource final : public IMFMediaSourceEx, public IMFGetService, public ICallayersKsControl, public IMFSampleAllocatorControl {
public:
    CallayersSource();
    ~CallayersSource();

    HRESULT Initialize(IMFAttributes* activateAttributes);

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) override;
    STDMETHODIMP EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) override;
    STDMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** event) override;
    STDMETHODIMP QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status, const PROPVARIANT* value) override;

    STDMETHODIMP GetCharacteristics(DWORD* characteristics) override;
    STDMETHODIMP CreatePresentationDescriptor(IMFPresentationDescriptor** descriptor) override;
    STDMETHODIMP Start(IMFPresentationDescriptor* descriptor, const GUID* timeFormat, const PROPVARIANT* startPosition) override;
    STDMETHODIMP Stop() override;
    STDMETHODIMP Pause() override;
    STDMETHODIMP Shutdown() override;

    STDMETHODIMP GetSourceAttributes(IMFAttributes** attributes) override;
    STDMETHODIMP GetStreamAttributes(DWORD streamIdentifier, IMFAttributes** attributes) override;
    STDMETHODIMP SetD3DManager(IUnknown* manager) override;

    STDMETHODIMP GetService(REFGUID service, REFIID riid, void** object) override;

    STDMETHODIMP KsProperty(PKSPROPERTY property, ULONG propertyLength, void* data, ULONG dataLength, ULONG* bytesReturned) override;
    STDMETHODIMP KsMethod(PKSMETHOD method, ULONG methodLength, void* data, ULONG dataLength, ULONG* bytesReturned) override;
    STDMETHODIMP KsEvent(PKSEVENT event, ULONG eventLength, void* data, ULONG dataLength, ULONG* bytesReturned) override;

    STDMETHODIMP SetDefaultAllocator(DWORD outputStreamID, IUnknown* allocator) override;
    STDMETHODIMP GetAllocatorUsage(DWORD outputStreamID, DWORD* inputStreamID, MFSampleAllocatorUsage* usage) override;

private:
    HRESULT EnsurePresentation();

    long m_refs = 1;
    CRITICAL_SECTION m_lock{};
    bool m_lockReady = false;
    bool m_shutdown = false;
    IMFMediaEventQueue* m_events = nullptr;
    IMFAttributes* m_attributes = nullptr;
    IMFPresentationDescriptor* m_presentation = nullptr;
    CallayersStream* m_stream = nullptr;
    bool m_announced = false;
};

class CallayersStream final : public IMFMediaStream2 {
public:
    explicit CallayersStream(CallayersSource* parent);
    ~CallayersStream();

    HRESULT Initialize();
    HRESULT SetAllocator(IUnknown* allocator);
    HRESULT Start(IMFMediaType* mediaType, bool sendEvent);
    HRESULT Stop(bool sendEvent);

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) override;
    STDMETHODIMP EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) override;
    STDMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** event) override;
    STDMETHODIMP QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status, const PROPVARIANT* value) override;

    STDMETHODIMP GetMediaSource(IMFMediaSource** source) override;
    STDMETHODIMP GetStreamDescriptor(IMFStreamDescriptor** descriptor) override;
    STDMETHODIMP RequestSample(IUnknown* token) override;

    STDMETHODIMP SetStreamState(MF_STREAM_STATE state) override;
    STDMETHODIMP GetStreamState(MF_STREAM_STATE* state) override;

    IMFStreamDescriptor* Descriptor() const { return m_descriptor; }
    HRESULT Attributes(IMFAttributes** attributes);

    void Shutdown();

private:
    HRESULT Check();
    HRESULT ConfigureAllocator(IMFMediaType* mediaType);
    HRESULT CopyFrame(BYTE* dest, LONG pitch, DWORD destBytes, const GUID& subtype);

    long m_refs = 1;
    CRITICAL_SECTION m_lock{};
    bool m_lockReady = false;
    bool m_shutdown = false;
    CallayersSource* m_parent = nullptr;
    IMFMediaEventQueue* m_events = nullptr;
    IMFStreamDescriptor* m_descriptor = nullptr;
    IMFMediaType* m_mediaType = nullptr;
    IMFVideoSampleAllocatorEx* m_allocator = nullptr;
    IMFVideoSampleAllocator* m_provided = nullptr;
    MF_STREAM_STATE m_state = MF_STREAM_STATE_STOPPED;
    UINT64 m_frameIndex = 0;
};

class CallayersActivate final : public IMFActivate {
public:
    CallayersActivate();
    virtual ~CallayersActivate();

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP ActivateObject(REFIID riid, void** ppv) override;
    STDMETHODIMP ShutdownObject() override;
    STDMETHODIMP DetachObject() override;

    STDMETHODIMP GetItem(REFGUID key, PROPVARIANT* value) override;
    STDMETHODIMP GetItemType(REFGUID key, MF_ATTRIBUTE_TYPE* type) override;
    STDMETHODIMP CompareItem(REFGUID key, REFPROPVARIANT value, BOOL* result) override;
    STDMETHODIMP Compare(IMFAttributes* theirs, MF_ATTRIBUTES_MATCH_TYPE matchType, BOOL* result) override;
    STDMETHODIMP GetUINT32(REFGUID key, UINT32* value) override;
    STDMETHODIMP GetUINT64(REFGUID key, UINT64* value) override;
    STDMETHODIMP GetDouble(REFGUID key, double* value) override;
    STDMETHODIMP GetGUID(REFGUID key, GUID* value) override;
    STDMETHODIMP GetStringLength(REFGUID key, UINT32* length) override;
    STDMETHODIMP GetString(REFGUID key, LPWSTR value, UINT32 bufferLength, UINT32* written) override;
    STDMETHODIMP GetAllocatedString(REFGUID key, LPWSTR* value, UINT32* length) override;
    STDMETHODIMP GetBlobSize(REFGUID key, UINT32* size) override;
    STDMETHODIMP GetBlob(REFGUID key, UINT8* buffer, UINT32 bufferSize, UINT32* written) override;
    STDMETHODIMP GetAllocatedBlob(REFGUID key, UINT8** buffer, UINT32* size) override;
    STDMETHODIMP GetUnknown(REFGUID key, REFIID riid, LPVOID* object) override;
    STDMETHODIMP SetItem(REFGUID key, REFPROPVARIANT value) override;
    STDMETHODIMP DeleteItem(REFGUID key) override;
    STDMETHODIMP DeleteAllItems() override;
    STDMETHODIMP SetUINT32(REFGUID key, UINT32 value) override;
    STDMETHODIMP SetUINT64(REFGUID key, UINT64 value) override;
    STDMETHODIMP SetDouble(REFGUID key, double value) override;
    STDMETHODIMP SetGUID(REFGUID key, REFGUID value) override;
    STDMETHODIMP SetString(REFGUID key, LPCWSTR value) override;
    STDMETHODIMP SetBlob(REFGUID key, const UINT8* buffer, UINT32 size) override;
    STDMETHODIMP SetUnknown(REFGUID key, IUnknown* unknown) override;
    STDMETHODIMP LockStore() override;
    STDMETHODIMP UnlockStore() override;
    STDMETHODIMP GetCount(UINT32* count) override;
    STDMETHODIMP GetItemByIndex(UINT32 index, GUID* key, PROPVARIANT* value) override;
    STDMETHODIMP CopyAllItems(IMFAttributes* dest) override;

private:
    long m_refs = 1;
    IMFAttributes* m_attributes = nullptr;
    CallayersSource* m_source = nullptr;
};

class CallayersClassFactory final : public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override;
    STDMETHODIMP LockServer(BOOL lock) override;

private:
    long m_refs = 1;
};
