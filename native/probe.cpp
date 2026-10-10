#include "common.h"
#include <iostream>
#include <filesystem>

using namespace callayers;
namespace {
void TraceSource(){
    PSECURITY_DESCRIPTOR descriptor=nullptr;
    Check(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;LS)(A;;GRGW;;;IU)",SDDL_REVISION_1,&descriptor,nullptr)?S_OK:HRESULT_FROM_WIN32(GetLastError()));
    SECURITY_ATTRIBUTES attributes{sizeof(attributes),descriptor,FALSE};
    Handle pipe(CreateNamedPipeW(L"\\\\.\\pipe\\Callayers.Step1.Diagnostics",PIPE_ACCESS_INBOUND|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,4096,4096,0,&attributes));
    DWORD error=GetLastError();LocalFree(descriptor);if(!pipe)throw HRESULT_FROM_WIN32(error);
    auto deadline=Clock::now()+std::chrono::seconds(20);
    while(Clock::now()<deadline){
        Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));OVERLAPPED ov{};ov.hEvent=event.value;
        BOOL connected=ConnectNamedPipe(pipe.value,&ov);error=GetLastError();DWORD count=0;
        if(!connected&&error==ERROR_IO_PENDING){
            if(WaitForSingleObject(event.value,500)!=WAIT_OBJECT_0){CancelIoEx(pipe.value,&ov);GetOverlappedResult(pipe.value,&ov,&count,TRUE);DisconnectNamedPipe(pipe.value);continue;}
            connected=GetOverlappedResult(pipe.value,&ov,&count,FALSE);
        }else if(!connected&&error==ERROR_PIPE_CONNECTED)connected=TRUE;
        if(connected){
            char buffer[256]{};ResetEvent(event.value);
            BOOL read=ReadFile(pipe.value,buffer,255,&count,&ov);
            if(!read&&GetLastError()==ERROR_IO_PENDING){if(WaitForSingleObject(event.value,200)==WAIT_OBJECT_0)read=GetOverlappedResult(pipe.value,&ov,&count,FALSE);else {CancelIoEx(pipe.value,&ov);GetOverlappedResult(pipe.value,&ov,&count,TRUE);}}
            if(read)std::cout.write(buffer,count)<<std::flush;
        }
        DisconnectNamedPipe(pipe.value);
    }
}
uint64_t Hash(const BYTE* bytes,size_t length){uint64_t h=1469598103934665603ull;for(size_t i=0;i<length;i+=113){h^=bytes[i];h*=1099511628211ull;}return h;}
void Require(bool condition,const char* message){if(!condition){std::cerr<<message<<"\n";throw E_FAIL;}}
void SelfTest(){
    auto idle=Pattern(0,true),a=Pattern(1),b=Pattern(9);
    Require(Valid(a->header),"valid frame rejected");auto malformed=a->header;malformed.bytes=1;
    Require(!Valid(malformed),"invalid size accepted");
    Require(Hash(a->pixels.data(),FrameBytes)!=Hash(b->pixels.data(),FrameBytes),"pattern did not move");
    Require(idle->header.mode==0,"idle is not marked");
    std::vector<BYTE> white(4*4*4,255);auto converted=FromBgra(white.data(),16,4,4,0);
    Require(converted->pixels[Height/2*Width+Width/2]>230,"white conversion failed");
    Require(converted->pixels[0]==16,"aspect ratio letterbox failed");
    std::vector<BYTE> roundtrip;ToBgra(*converted,roundtrip);
    Require(roundtrip[(Height/2*Width+Width/2)*4]>245,"NV12 conversion failed");
    std::cout<<"PASS: protocol validation, moving/idle frames, BT.709 conversion, aspect ratio\n";
}
ComPtr<IMFMediaEvent> Event(IMFMediaEventGenerator* generator){
    auto deadline=Clock::now()+std::chrono::seconds(5);
    do{ComPtr<IMFMediaEvent> e;HRESULT hr=generator->GetEvent(MF_EVENT_FLAG_NO_WAIT,&e);
        if(SUCCEEDED(hr))return e;if(hr!=MF_E_NO_EVENTS_AVAILABLE)Check(hr);Sleep(2);
    }while(Clock::now()<deadline);
    throw HRESULT_FROM_WIN32(WAIT_TIMEOUT);
}
void SourceTest(const wchar_t* path){
    HMODULE module=LoadLibraryW(std::filesystem::absolute(path).c_str());if(!module)throw HRESULT_FROM_WIN32(GetLastError());
    using GetFactory=HRESULT(WINAPI*)(REFCLSID,REFIID,void**);
    auto factoryFunction=reinterpret_cast<GetFactory>(GetProcAddress(module,"DllGetClassObject"));Require(factoryFunction!=nullptr,"missing COM export");
    ComPtr<IClassFactory> factory;Check(factoryFunction(CameraClsid,IID_PPV_ARGS(&factory)));
    ComPtr<IMFActivate> activate;Check(factory->CreateInstance(nullptr,IID_PPV_ARGS(&activate)));
    ComPtr<IMFMediaSource> source;Check(activate->ActivateObject(IID_PPV_ARGS(&source)));
    // Frame Server may release the activation helper while retaining the source.
    // That must not implicitly shut down the caller-owned source.
    activate.Reset();
    try {
        DWORD characteristics=0;Check(source->GetCharacteristics(&characteristics));Require(characteristics&MFMEDIASOURCE_IS_LIVE,"not live");
        ComPtr<IMFPresentationDescriptor> pd;Check(source->CreatePresentationDescriptor(&pd));
        PROPVARIANT position{};Check(source->Start(pd.Get(),nullptr,&position));
        ComPtr<IMFMediaStream> stream;
        for(int i=0;i<2;++i){auto e=Event(source.Get());MediaEventType t;Check(e->GetType(&t));
            if(t==MENewStream){PROPVARIANT v{};Check(e->GetValue(&v));Check(v.punkVal->QueryInterface(IID_PPV_ARGS(&stream)));PropVariantClear(&v);}}
        Require(stream!=nullptr,"source did not announce stream");
        auto start=Event(stream.Get());MediaEventType kind;Check(start->GetType(&kind));Require(kind==MEStreamStarted,"missing stream start");
        LONGLONG previous=-1;uint64_t initial=0,latest=0;auto began=Clock::now();
        for(int i=0;i<35;++i){
            Check(stream->RequestSample(nullptr));auto e=Event(stream.Get());Check(e->GetType(&kind));Require(kind==MEMediaSample,"expected a sample");
            PROPVARIANT value{};Check(e->GetValue(&value));ComPtr<IMFSample> sample;Check(value.punkVal->QueryInterface(IID_PPV_ARGS(&sample)));PropVariantClear(&value);
            LONGLONG time=0,duration=0;Check(sample->GetSampleTime(&time));Check(sample->GetSampleDuration(&duration));
            Require(time>previous&&duration==FrameDuration,"invalid sample timing");previous=time;
            ComPtr<IMFMediaBuffer> buffer;Check(sample->ConvertToContiguousBuffer(&buffer));BYTE* data=nullptr;DWORD length=0;
            Check(buffer->Lock(&data,nullptr,&length));Require(length==FrameBytes,"wrong sample size");latest=Hash(data,length);buffer->Unlock();if(i==0)initial=latest;
        }
        double seconds=std::chrono::duration<double>(Clock::now()-began).count();Require(seconds>=0.9&&seconds<3,"sample pacing outside tolerance");
        Check(source->Stop());Require(stream->RequestSample(nullptr)==MF_E_INVALIDREQUEST,"stopped stream accepted a sample");
        // Drain stop events before testing restart.
        Event(source.Get());Event(stream.Get());
        Check(source->Start(pd.Get(),nullptr,&position));Event(source.Get());Event(source.Get());Event(stream.Get());
        Check(stream->RequestSample(nullptr));Event(stream.Get());
        Check(source->Shutdown());Require(source->GetCharacteristics(&characteristics)==MF_E_SHUTDOWN,"shutdown contract failed");
        stream.Reset();source.Reset();factory.Reset();
        using CanUnload=HRESULT(WINAPI*)();auto unload=reinterpret_cast<CanUnload>(GetProcAddress(module,"DllCanUnloadNow"));
        Require(unload&&unload()==S_OK,"COM objects leaked after shutdown");
        std::cout<<"PASS: source activation, NV12 samples, timestamps, pacing, stop/restart, shutdown; frames "<<(initial==latest?"static/idle":"moving")<<"\n";
    }catch(...){if(source)source->Shutdown();throw;}
    FreeLibrary(module);
}
class Reader final:public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,IMFSourceReaderCallback>{
public:
    Handle ready{CreateEventW(nullptr,FALSE,FALSE,nullptr)};
    HRESULT status=S_OK;DWORD flags=0;ComPtr<IMFSample> sample;
    HRESULT STDMETHODCALLTYPE OnReadSample(HRESULT hr,DWORD,DWORD f,LONGLONG,IMFSample* s)override{status=hr;flags=f;sample=s;SetEvent(ready.value);return S_OK;}
    HRESULT STDMETHODCALLTYPE OnEvent(DWORD,IMFMediaEvent*)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE OnFlush(DWORD)override{return S_OK;}
};
void ProbeDevice(const Device& device,unsigned count,const std::wstring& expectation){
    auto source=OpenCamera(device);
    try{
        auto callback=Microsoft::WRL::Make<Reader>();ComPtr<IMFAttributes> attributes;Check(MFCreateAttributes(&attributes,1));
        Check(attributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK,callback.Get()));
        ComPtr<IMFSourceReader> reader;Check(MFCreateSourceReaderFromMediaSource(source.Get(),attributes.Get(),&reader));
        auto type=VideoType();Check(reader->SetCurrentMediaType(VideoStream,nullptr,type.Get()));
        uint64_t first=0,last=0;unsigned changes=0,idleCount=0;bool liveBeforeIdle=false,liveAfterIdle=false;
        auto idle=Pattern(0,true);const auto idleHash=Hash(idle->pixels.data(),FrameBytes);
        LONGLONG prior=-1;auto began=Clock::now();
        for(unsigned i=0;i<count;){
            Check(reader->ReadSample(VideoStream,0,nullptr,nullptr,nullptr,nullptr));
            Require(WaitForSingleObject(callback->ready.value,5000)==WAIT_OBJECT_0,"camera read timeout");Check(callback->status);
            Require(!(callback->flags&(MF_SOURCE_READERF_ERROR|MF_SOURCE_READERF_ENDOFSTREAM)),"camera stopped unexpectedly");
            if(!callback->sample)continue;
            LONGLONG time;Check(callback->sample->GetSampleTime(&time));Require(time>prior,"non-monotonic timestamp");prior=time;
            ComPtr<IMFMediaBuffer> buffer;Check(callback->sample->ConvertToContiguousBuffer(&buffer));BYTE* data;DWORD length;
            Check(buffer->Lock(&data,nullptr,&length));Require(length>=FrameBytes,"short frame");auto hash=Hash(data,length);buffer->Unlock();
            if(hash==idleHash)++idleCount;
            else if(idleCount==0)liveBeforeIdle=true;
            else if(liveBeforeIdle)liveAfterIdle=true;
            if(i==0)first=hash;else if(hash!=last)++changes;last=hash;++i;callback->sample.Reset();
        }
        double seconds=std::chrono::duration<double>(Clock::now()-began).count();
        std::wcout<<device.name<<L"; "<<count<<L" frames in "<<seconds<<L"s, "<<changes<<L" changes, "<<idleCount<<L" idle; first/last "<<first<<L"/"<<last<<L"\n";
        if(expectation==L"--expect-moving")Require(changes>count/2&&idleCount<count/10,"registered output is not live");
        if(expectation==L"--expect-idle")Require(idleCount==count,"expected only idle frames");
        if(expectation==L"--expect-recovery")Require(liveBeforeIdle&&idleCount>=8&&liveAfterIdle,"expected live -> idle -> live in one reader session");
        std::cout<<"PASS: registered camera samples and expectations\n";
        reader->Flush(AllStreams);source->Shutdown();
    }catch(...){source->Shutdown();throw;}
}
}
int wmain(int argc,wchar_t** argv){
    HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(com))return 1;
    HRESULT startup=MFStartup(MF_VERSION);if(FAILED(startup)){CoUninitialize();return 1;}int result=0;
    try{
        if(argc>1&&std::wstring(argv[1])==L"--trace")TraceSource();
        else if(argc>1&&std::wstring(argv[1])==L"--self-test")SelfTest();
        else if(argc>2&&std::wstring(argv[1])==L"--source-test")SourceTest(argv[2]);
        else if(argc>1&&std::wstring(argv[1])==L"--pipe"){
            unsigned received=0,changes=0;uint64_t previous=0;DWORD lastError=0;
            for(int i=0;i<60;++i){auto f=ReadFrame();if(f){++received;if(previous!=f->header.sequence)++changes;previous=f->header.sequence;}else lastError=GetLastError();
                Sleep(33);}
            std::cout<<"Pipe: "<<received<<" frames, "<<changes<<" changes, last error "<<lastError<<"\n";
            Require(received>=55&&changes>40,"producer missing, stalled, or too slow");std::cout<<"PASS: "<<received<<" complete frames, "<<changes<<" sequence changes\n";
        }else if(argc>1&&std::wstring(argv[1])==L"--remove"){
            auto camera=VirtualCamera();Check(camera->Remove());Check(camera->Shutdown());std::cout<<"Removed current-user Callayers virtual camera\n";
        }else{
            auto devices=Cameras(true);
            for(const auto& device:devices)std::wcout<<device.name<<L"\n";
            if(argc>1&&std::wstring(argv[1])==L"--camera"){
                auto found=std::find_if(devices.begin(),devices.end(),[](const Device& d){return d.name.find(L"Callayers")!=std::wstring::npos;});
                Require(found!=devices.end(),"Callayers is not registered");ProbeDevice(*found,argc>2?std::clamp(_wtoi(argv[2]),1,3600):90,argc>3?argv[3]:L"");
            }
        }
    }catch(HRESULT hr){std::wcerr<<L"FAIL: "<<ErrorText(hr)<<L"\n";result=1;}catch(...){std::cerr<<"FAIL: unexpected exception\n";result=1;}
    MFShutdown();CoUninitialize();return result;
}
