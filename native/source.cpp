#include "common.h"
#include "camera_demand.h"
#include <ks.h>
#include <ksmedia.h>
#include <ksproxy.h>
#include <condition_variable>
#include <deque>

using namespace callayers;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::Make;
using Microsoft::WRL::ChainInterfaces;
namespace {
std::atomic<long> objects=0;
struct Counted { Counted(){++objects;} ~Counted(){--objects;} };
void Trace(const char* action,const void* object,HRESULT hr=S_OK){
    char text[256];sprintf_s(text,"pid=%lu %s object=%p hr=%08X\n",GetCurrentProcessId(),action,object,static_cast<unsigned>(hr));
    Handle pipe(CreateFileW(L"\\\\.\\pipe\\Callayers.Step1.Diagnostics",GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr));
    if(pipe)Transfer(pipe.value,true,text,static_cast<DWORD>(strlen(text)),nullptr,20);
}

// Event queues are retained through shutdown so concurrent event calls never race a reset.
#define EVENT_METHODS \
 HRESULT STDMETHODCALLTYPE GetEvent(DWORD f,IMFMediaEvent** e) override {return events->GetEvent(f,e);} \
 HRESULT STDMETHODCALLTYPE BeginGetEvent(IMFAsyncCallback* c,IUnknown* s) override {return events->BeginGetEvent(c,s);} \
 HRESULT STDMETHODCALLTYPE EndGetEvent(IMFAsyncResult* r,IMFMediaEvent** e) override {return events->EndGetEvent(r,e);} \
 HRESULT STDMETHODCALLTYPE QueueEvent(MediaEventType t,REFGUID g,HRESULT h,const PROPVARIANT* v) override {return events->QueueEventParamVar(t,g,h,v);}

class Stream final : public RuntimeClass<RuntimeClassFlags<ClassicCom>,ChainInterfaces<IMFMediaStream2,IMFMediaStream,IMFMediaEventGenerator>>, public Counted {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<ComPtr<IUnknown>> requests;
    ComPtr<IMFMediaSource> source;
    MF_STREAM_STATE state=MF_STREAM_STATE_STOPPED;
    bool closed=false;
    uint64_t generation=0;
    std::thread worker;
    std::thread demandWorker;
    CameraDemandClient demand;
    Handle stop{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
public:
    ComPtr<IMFMediaEventQueue> events;
    ComPtr<IMFStreamDescriptor> descriptor;
    ComPtr<IMFAttributes> attributes;
    HRESULT Initialize(IMFMediaSource* owner) {
        try {
            if (!stop) return HRESULT_FROM_WIN32(GetLastError());
            source=owner; Check(MFCreateEventQueue(&events));
            auto type=VideoType(); IMFMediaType* types[]={type.Get()};
            Check(MFCreateStreamDescriptor(0,1,types,&descriptor));
            ComPtr<IMFMediaTypeHandler> handler; Check(descriptor->GetMediaTypeHandler(&handler));
            Check(handler->SetCurrentMediaType(type.Get()));
            Check(MFCreateAttributes(&attributes,4));
            Check(attributes->SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY,PINNAME_VIDEO_CAPTURE));
            Check(attributes->SetUINT32(MF_DEVICESTREAM_STREAM_ID,0));
            Check(attributes->SetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED,TRUE));
            Check(attributes->SetUINT32(MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES,MFFrameSourceTypes_Color));
            worker=std::thread([this]{Run();});
            demandWorker=std::thread([this]{
                while(WaitForSingleObject(stop.value,100)==WAIT_TIMEOUT){
                    std::lock_guard lock(mutex);demand.Update(!closed&&state==MF_STREAM_STATE_RUNNING);
                }
            });
            return S_OK;
        } catch(HRESULT hr){source.Reset();return hr;} catch(...){source.Reset();return E_OUTOFMEMORY;}
    }
    ~Stream(){ Close(); }
    void Close() {
        {std::lock_guard lock(mutex); if(closed)return;closed=true;demand.Update(false);requests.clear();source.Reset();}
        SetEvent(stop.value); wake.notify_all();
        if(worker.joinable()) worker.join();
        if(demandWorker.joinable()) demandWorker.join();
        if(events)events->Shutdown();
    }
    void SetRunning(bool running) {
        std::lock_guard lock(mutex);
        state=running?MF_STREAM_STATE_RUNNING:MF_STREAM_STATE_STOPPED;
        demand.Update(running);
        ++generation; requests.clear(); wake.notify_all();
    }
    EVENT_METHODS
    HRESULT STDMETHODCALLTYPE GetMediaSource(IMFMediaSource** out) override {
        if(!out)return E_POINTER;
        std::lock_guard lock(mutex);if(closed){*out=nullptr;return MF_E_SHUTDOWN;}return source.CopyTo(out);
    }
    HRESULT STDMETHODCALLTYPE GetStreamDescriptor(IMFStreamDescriptor** out) override {return descriptor.CopyTo(out);}
    HRESULT STDMETHODCALLTYPE RequestSample(IUnknown* token) override {
        std::lock_guard lock(mutex);
        if(closed)return MF_E_SHUTDOWN;
        if(state!=MF_STREAM_STATE_RUNNING)return MF_E_INVALIDREQUEST;
        if(requests.size()>=8)return MF_E_NOTACCEPTING;
        requests.emplace_back(token);wake.notify_one();return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetStreamState(MF_STREAM_STATE next) override {
        std::lock_guard lock(mutex);if(closed)return MF_E_SHUTDOWN;
        if(next==MF_STREAM_STATE_PAUSED)return MF_E_INVALID_STATE_TRANSITION;
        state=next;demand.Update(next==MF_STREAM_STATE_RUNNING); ++generation;requests.clear();wake.notify_all();return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetStreamState(MF_STREAM_STATE* out) override {
        if(!out)return E_POINTER;std::lock_guard lock(mutex);if(closed)return MF_E_SHUTDOWN;*out=state;return S_OK;
    }
private:
    void Run() {
        CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        auto due=Clock::now(); auto idle=Pattern(0,true);
        while(true) {
            ComPtr<IUnknown> token;uint64_t revision;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock,[&]{return closed || (state==MF_STREAM_STATE_RUNNING&&!requests.empty());});
                if(closed)break;
                revision=generation;
                if(wake.wait_until(lock,due,[&]{return closed||generation!=revision;}))continue;
                token=requests.front();requests.pop_front();
            }
            auto frame=ReadFrame(stop.value); if(!frame)frame=idle;
            HRESULT hr=S_OK;
            try {
                ComPtr<IMFSample> sample;ComPtr<IMFMediaBuffer> buffer;
                Check(MFCreateSample(&sample));
                Check(MFCreate2DMediaBuffer(Width,Height,MFVideoFormat_NV12.Data1,FALSE,&buffer));
                ComPtr<IMF2DBuffer> twoD;Check(buffer.As(&twoD));
                Check(twoD->ContiguousCopyFrom(frame->pixels.data(),FrameBytes));
                Check(sample->AddBuffer(buffer.Get()));
                Check(sample->SetSampleTime(MFGetSystemTime()));
                Check(sample->SetSampleDuration(FrameDuration));
                Check(sample->SetUINT32(MFSampleExtension_CleanPoint,TRUE));
                if(token)Check(sample->SetUnknown(MFSampleExtension_Token,token.Get()));
                std::lock_guard lock(mutex);
                if(!closed&&generation==revision)Check(events->QueueEventParamUnk(MEMediaSample,GUID_NULL,S_OK,sample.Get()));
            } catch(HRESULT h){hr=h;}catch(...){hr=E_OUTOFMEMORY;}
            if(FAILED(hr))events->QueueEventParamVar(MEError,GUID_NULL,hr,nullptr);
            due=std::max(due+std::chrono::nanoseconds(1'000'000'000/Fps),Clock::now());
        }
        CoUninitialize();
    }
};

class Source final : public RuntimeClass<RuntimeClassFlags<ClassicCom>,ChainInterfaces<IMFMediaSourceEx,IMFMediaSource,IMFMediaEventGenerator>,IMFGetService,IKsControl,IMFSampleAllocatorControl>, public Counted {
    std::recursive_mutex mutex;
    std::atomic<bool> closed=false;
    bool started=false;
    ComPtr<Stream> stream;
    ComPtr<IMFPresentationDescriptor> presentation;
    ComPtr<IMFAttributes> attributes;
public:
    ComPtr<IMFMediaEventQueue> events;
    HRESULT Initialize() {
        Trace("Source.Initialize",this);
        try {
            Check(MFCreateEventQueue(&events));
            Check(MFCreateAttributes(&attributes,4));
            Check(attributes->SetUINT32(MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES,MFFrameSourceTypes_Color));
            stream=Make<Stream>();if(!stream)return E_OUTOFMEMORY;
            Check(stream->Initialize(this));
            IMFStreamDescriptor* descriptors[]={stream->descriptor.Get()};
            Check(MFCreatePresentationDescriptor(1,descriptors,&presentation));
            Check(presentation->SelectStream(0));
            return S_OK;
        }catch(HRESULT hr){Shutdown();return hr;}catch(...){Shutdown();return E_OUTOFMEMORY;}
    }
    EVENT_METHODS
    HRESULT STDMETHODCALLTYPE GetCharacteristics(DWORD* out) override {
        Trace("Source.GetCharacteristics",this,closed?MF_E_SHUTDOWN:S_OK);
        if(!out)return E_POINTER;std::lock_guard lock(mutex);if(closed)return MF_E_SHUTDOWN;*out=MFMEDIASOURCE_IS_LIVE;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreatePresentationDescriptor(IMFPresentationDescriptor** out) override {
        Trace("Source.CreatePresentationDescriptor",this,closed?MF_E_SHUTDOWN:S_OK);
        if(!out)return E_POINTER;std::lock_guard lock(mutex);if(closed)return MF_E_SHUTDOWN;return presentation->Clone(out);
    }
    HRESULT STDMETHODCALLTYPE Start(IMFPresentationDescriptor* pd,const GUID* timeFormat,const PROPVARIANT* position) override {
        Trace("Source.Start",this,closed?MF_E_SHUTDOWN:S_OK);
        if(!pd||!position)return E_INVALIDARG;
        std::lock_guard lock(mutex);if(closed)return MF_E_SHUTDOWN;
        if(timeFormat&&*timeFormat!=GUID_NULL)return MF_E_UNSUPPORTED_TIME_FORMAT;
        if(position->vt!=VT_EMPTY && (position->vt!=VT_I8||position->hVal.QuadPart!=0))return MF_E_INVALIDREQUEST;
        DWORD count=0;if(FAILED(pd->GetStreamDescriptorCount(&count))||count!=1)return E_INVALIDARG;
        BOOL selected=FALSE;ComPtr<IMFStreamDescriptor> sd;
        HRESULT hr=pd->GetStreamDescriptorByIndex(0,&selected,&sd);if(FAILED(hr))return hr;
        if(!selected)return MF_E_INVALIDREQUEST;
        ComPtr<IMFMediaTypeHandler> handler;ComPtr<IMFMediaType> type;GUID subtype{};UINT32 w=0,h=0,n=0,d=0;DWORD id=~0u;
        if(FAILED(sd->GetStreamIdentifier(&id))||id!=0)return MF_E_INVALIDSTREAMNUMBER;
        if(FAILED(sd->GetMediaTypeHandler(&handler))||FAILED(handler->GetCurrentMediaType(&type))||
           FAILED(type->GetGUID(MF_MT_SUBTYPE,&subtype))||subtype!=MFVideoFormat_NV12||
           FAILED(MFGetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,&w,&h))||w!=Width||h!=Height||
           FAILED(MFGetAttributeRatio(type.Get(),MF_MT_FRAME_RATE,&n,&d))||n!=Fps||d!=1)return MF_E_INVALIDMEDIATYPE;
        PROPVARIANT start{};start.vt=VT_I8;start.hVal.QuadPart=MFGetSystemTime();
        hr=events->QueueEventParamUnk(started?MEUpdatedStream:MENewStream,GUID_NULL,S_OK,stream.Get());
        if(FAILED(hr))return hr;
        stream->SetRunning(true);started=true;
        hr=stream->QueueEvent(MEStreamStarted,GUID_NULL,S_OK,&start);if(FAILED(hr))return hr;
        return QueueEvent(MESourceStarted,GUID_NULL,S_OK,&start);
    }
    HRESULT STDMETHODCALLTYPE Stop() override {
        std::lock_guard lock(mutex);if(closed)return MF_E_SHUTDOWN;
        stream->SetRunning(false);
        stream->QueueEvent(MEStreamStopped,GUID_NULL,S_OK,nullptr);
        return QueueEvent(MESourceStopped,GUID_NULL,S_OK,nullptr);
    }
    HRESULT STDMETHODCALLTYPE Pause() override {std::lock_guard lock(mutex);return closed?MF_E_SHUTDOWN:MF_E_INVALID_STATE_TRANSITION;}
    HRESULT STDMETHODCALLTYPE Shutdown() override {
        Trace("Source.Shutdown",this);
        std::lock_guard lock(mutex);if(closed)return MF_E_SHUTDOWN;closed=true;
        if(stream)stream->Close();if(events)events->Shutdown();return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSourceAttributes(IMFAttributes** out) override {
        std::lock_guard lock(mutex);if(closed)return MF_E_SHUTDOWN;return attributes.CopyTo(out);
    }
    HRESULT STDMETHODCALLTYPE GetStreamAttributes(DWORD id,IMFAttributes** out) override {
        std::lock_guard lock(mutex);if(closed)return MF_E_SHUTDOWN;if(id!=0)return MF_E_INVALIDSTREAMNUMBER;return stream->attributes.CopyTo(out);
    }
    HRESULT STDMETHODCALLTYPE SetD3DManager(IUnknown*) override {std::lock_guard lock(mutex);return closed?MF_E_SHUTDOWN:S_OK;}
    HRESULT STDMETHODCALLTYPE SetDefaultAllocator(DWORD id,IUnknown*) override {return id==0?S_OK:MF_E_INVALIDSTREAMNUMBER;}
    HRESULT STDMETHODCALLTYPE GetAllocatorUsage(DWORD id,DWORD* input,MFSampleAllocatorUsage* usage) override {
        if(!input||!usage)return E_POINTER;if(id!=0)return MF_E_INVALIDSTREAMNUMBER;*input=0;*usage=MFSampleAllocatorUsage_UsesCustomAllocator;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetService(REFGUID,REFIID,void** out) override {if(!out)return E_POINTER;*out=nullptr;return MF_E_UNSUPPORTED_SERVICE;}
    HRESULT STDMETHODCALLTYPE KsProperty(PKSPROPERTY,ULONG,void*,ULONG,ULONG* returned) override {if(!returned)return E_POINTER;*returned=0;return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);}
    HRESULT STDMETHODCALLTYPE KsMethod(PKSMETHOD,ULONG,void*,ULONG,ULONG* returned) override {if(!returned)return E_POINTER;*returned=0;return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);}
    HRESULT STDMETHODCALLTYPE KsEvent(PKSEVENT,ULONG,void*,ULONG,ULONG* returned) override {if(!returned)return E_POINTER;*returned=0;return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);}
};
class Activate final : public RuntimeClass<RuntimeClassFlags<ClassicCom>,ChainInterfaces<IMFActivate,IMFAttributes>>,public Counted {
    ComPtr<IMFAttributes> store;
    ComPtr<Source> source;
    std::mutex mutex;
public:
    HRESULT Initialize(){return MFCreateAttributes(&store,8);}
    // Releasing the activation helper only releases its reference. The caller
    // owns the activated source and shuts it down explicitly when finished.
    ~Activate() = default;
    HRESULT STDMETHODCALLTYPE ActivateObject(REFIID iid,void** out)override{
        Trace("Activate.ActivateObject",this);
        if(!out)return E_POINTER;*out=nullptr;std::lock_guard lock(mutex);
        const bool creating=!source;
        if(creating){
            source=Make<Source>();if(!source)return E_OUTOFMEMORY;
            HRESULT hr=source->Initialize();if(FAILED(hr)){source.Reset();return hr;}
            ComPtr<IMFAttributes> attrs;hr=source->GetSourceAttributes(&attrs);
            if(SUCCEEDED(hr))hr=store->CopyAllItems(attrs.Get());
            if(FAILED(hr)){source->Shutdown();source.Reset();return hr;}
        }
        HRESULT hr=source->QueryInterface(iid,out);
        if(FAILED(hr)&&creating){source->Shutdown();source.Reset();}
        return hr;
    }
    HRESULT STDMETHODCALLTYPE ShutdownObject()override{Trace("Activate.ShutdownObject",this);std::lock_guard lock(mutex);if(source){source->Shutdown();source.Reset();}return S_OK;}
    HRESULT STDMETHODCALLTYPE DetachObject()override{Trace("Activate.DetachObject",this);std::lock_guard lock(mutex);source.Reset();return S_OK;}
    HRESULT STDMETHODCALLTYPE GetItem(REFGUID k,PROPVARIANT* v)override{return store->GetItem(k,v);}
    HRESULT STDMETHODCALLTYPE GetItemType(REFGUID k,MF_ATTRIBUTE_TYPE* v)override{return store->GetItemType(k,v);}
    HRESULT STDMETHODCALLTYPE CompareItem(REFGUID k,REFPROPVARIANT v,BOOL* r)override{return store->CompareItem(k,v,r);}
    HRESULT STDMETHODCALLTYPE Compare(IMFAttributes* a,MF_ATTRIBUTES_MATCH_TYPE t,BOOL* r)override{return store->Compare(a,t,r);}
    HRESULT STDMETHODCALLTYPE GetUINT32(REFGUID k,UINT32* v)override{return store->GetUINT32(k,v);}
    HRESULT STDMETHODCALLTYPE GetUINT64(REFGUID k,UINT64* v)override{return store->GetUINT64(k,v);}
    HRESULT STDMETHODCALLTYPE GetDouble(REFGUID k,double* v)override{return store->GetDouble(k,v);}
    HRESULT STDMETHODCALLTYPE GetGUID(REFGUID k,GUID* v)override{return store->GetGUID(k,v);}
    HRESULT STDMETHODCALLTYPE GetStringLength(REFGUID k,UINT32* n)override{return store->GetStringLength(k,n);}
    HRESULT STDMETHODCALLTYPE GetString(REFGUID k,LPWSTR v,UINT32 n,UINT32* used)override{return store->GetString(k,v,n,used);}
    HRESULT STDMETHODCALLTYPE GetAllocatedString(REFGUID k,LPWSTR* v,UINT32* n)override{return store->GetAllocatedString(k,v,n);}
    HRESULT STDMETHODCALLTYPE GetBlobSize(REFGUID k,UINT32* n)override{return store->GetBlobSize(k,n);}
    HRESULT STDMETHODCALLTYPE GetBlob(REFGUID k,UINT8* b,UINT32 n,UINT32* used)override{return store->GetBlob(k,b,n,used);}
    HRESULT STDMETHODCALLTYPE GetAllocatedBlob(REFGUID k,UINT8** b,UINT32* n)override{return store->GetAllocatedBlob(k,b,n);}
    HRESULT STDMETHODCALLTYPE GetUnknown(REFGUID k,REFIID iid,void** out)override{return store->GetUnknown(k,iid,out);}
    HRESULT STDMETHODCALLTYPE SetItem(REFGUID k,REFPROPVARIANT v)override{return store->SetItem(k,v);}
    HRESULT STDMETHODCALLTYPE DeleteItem(REFGUID k)override{return store->DeleteItem(k);}
    HRESULT STDMETHODCALLTYPE DeleteAllItems()override{return store->DeleteAllItems();}
    HRESULT STDMETHODCALLTYPE SetUINT32(REFGUID k,UINT32 v)override{return store->SetUINT32(k,v);}
    HRESULT STDMETHODCALLTYPE SetUINT64(REFGUID k,UINT64 v)override{return store->SetUINT64(k,v);}
    HRESULT STDMETHODCALLTYPE SetDouble(REFGUID k,double v)override{return store->SetDouble(k,v);}
    HRESULT STDMETHODCALLTYPE SetGUID(REFGUID k,REFGUID v)override{return store->SetGUID(k,v);}
    HRESULT STDMETHODCALLTYPE SetString(REFGUID k,LPCWSTR v)override{return store->SetString(k,v);}
    HRESULT STDMETHODCALLTYPE SetBlob(REFGUID k,const UINT8* b,UINT32 n)override{return store->SetBlob(k,b,n);}
    HRESULT STDMETHODCALLTYPE SetUnknown(REFGUID k,IUnknown* v)override{return store->SetUnknown(k,v);}
    HRESULT STDMETHODCALLTYPE LockStore()override{return store->LockStore();}
    HRESULT STDMETHODCALLTYPE UnlockStore()override{return store->UnlockStore();}
    HRESULT STDMETHODCALLTYPE GetCount(UINT32* n)override{return store->GetCount(n);}
    HRESULT STDMETHODCALLTYPE GetItemByIndex(UINT32 n,GUID* k,PROPVARIANT* v)override{return store->GetItemByIndex(n,k,v);}
    HRESULT STDMETHODCALLTYPE CopyAllItems(IMFAttributes* out)override{return store->CopyAllItems(out);}
};
class Factory final : public RuntimeClass<RuntimeClassFlags<ClassicCom>,IClassFactory>,public Counted {
public:
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer,REFIID iid,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;if(outer)return CLASS_E_NOAGGREGATION;
        auto activate=Make<Activate>();if(!activate)return E_OUTOFMEMORY;
        HRESULT hr=activate->Initialize();if(FAILED(hr))return hr;
        return activate->QueryInterface(iid,out);
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {if(lock)++objects;else --objects;return S_OK;}
};
}
extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid,REFIID iid,void** out) {
    if(!out)return E_POINTER;*out=nullptr;if(clsid!=CameraClsid)return CLASS_E_CLASSNOTAVAILABLE;
    auto factory=Make<Factory>();return factory?factory->QueryInterface(iid,out):E_OUTOFMEMORY;
}
extern "C" HRESULT __stdcall DllCanUnloadNow(){return objects==0?S_OK:S_FALSE;}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(module);return TRUE;}
