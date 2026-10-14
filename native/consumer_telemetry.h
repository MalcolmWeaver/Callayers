#pragma once
#include <windows.h>
#include <cstdint>
#include <cwctype>
#include <string>

namespace callayers {
enum class ConsumerApp : LONG { Unknown, Brave, Chrome, Edge, Firefox, Zoom, Teams, Discord, Skype, Obs, Opera, WindowsCamera };
inline const char* ConsumerLabel(ConsumerApp app) {
    switch(app) {
    case ConsumerApp::Brave:return "brave";case ConsumerApp::Chrome:return "chrome";
    case ConsumerApp::Edge:return "edge";case ConsumerApp::Firefox:return "firefox";
    case ConsumerApp::Zoom:return "zoom";case ConsumerApp::Teams:return "teams";
    case ConsumerApp::Discord:return "discord";case ConsumerApp::Skype:return "skype";
    case ConsumerApp::Obs:return "obs";case ConsumerApp::Opera:return "opera";
    case ConsumerApp::WindowsCamera:return "windows_camera";default:return "unknown";
    }
}
inline ConsumerApp NormalizeConsumer(std::wstring name) {
    auto slash=name.find_last_of(L"\\/");if(slash!=std::wstring::npos)name.erase(0,slash+1);
    for(auto& c:name)c=static_cast<wchar_t>(std::towlower(c));
    if(name==L"brave.exe")return ConsumerApp::Brave;if(name==L"chrome.exe")return ConsumerApp::Chrome;
    if(name==L"msedge.exe")return ConsumerApp::Edge;if(name==L"firefox.exe")return ConsumerApp::Firefox;
    if(name==L"zoom.exe")return ConsumerApp::Zoom;if(name==L"teams.exe"||name==L"ms-teams.exe")return ConsumerApp::Teams;
    if(name==L"discord.exe")return ConsumerApp::Discord;if(name==L"skype.exe")return ConsumerApp::Skype;
    if(name==L"obs64.exe"||name==L"obs32.exe")return ConsumerApp::Obs;if(name==L"opera.exe")return ConsumerApp::Opera;
    if(name==L"windowscamera.exe")return ConsumerApp::WindowsCamera;return ConsumerApp::Unknown;
}
struct ConsumerSnapshot {
    bool connected=false,output=false;
    bool valid=true;
    ConsumerApp app=ConsumerApp::Unknown;
    uint64_t connection=0,scene=0;
};
// A separate, versioned mapping preserves the accepted video bridge layout.
// Only normalized labels and timing/scene counters cross this boundary.
struct alignas(8) ConsumerShared {
    volatile LONG sequence,app;
    volatile LONG64 counter,connection,connectedTick,outputTick,outputSession,outputScene;
    volatile LONG64 publishedSession,publishedTick,publishedScene;
};
inline LONG64 ConsumerRead64(volatile LONG64* p){return InterlockedCompareExchange64(p,0,0);}
class ConsumerTelemetry {
    HANDLE mapping_=nullptr;ConsumerShared* data_=nullptr;
public:
    explicit ConsumerTelemetry(const wchar_t* name=L"Local\\Callayers.Foundation.v2.ConsumerTelemetry.1") {
        mapping_=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(ConsumerShared),name);
        if(mapping_)data_=static_cast<ConsumerShared*>(MapViewOfFile(mapping_,FILE_MAP_ALL_ACCESS,0,0,sizeof(ConsumerShared)));
    }
    ~ConsumerTelemetry(){if(data_)UnmapViewOfFile(data_);if(mapping_)CloseHandle(mapping_);}
    ConsumerTelemetry(const ConsumerTelemetry&)=delete;
    void started(ConsumerApp app) {
        if(!data_)return;
        InterlockedIncrement(&data_->sequence);InterlockedExchange(&data_->app,LONG(app));
        InterlockedExchange64(&data_->connection,InterlockedIncrement64(&data_->counter));
        InterlockedExchange64(&data_->outputTick,0);InterlockedExchange64(&data_->outputScene,0);
        InterlockedExchange64(&data_->connectedTick,GetTickCount64());InterlockedIncrement(&data_->sequence);
    }
    void heartbeat(){if(data_)InterlockedExchange64(&data_->connectedTick,GetTickCount64());}
    void delivered(bool live,LONG64 session,uint64_t scene,HRESULT received=S_OK) {
        if(!data_)return;InterlockedIncrement(&data_->sequence);
        InterlockedExchange64(&data_->outputSession,session);InterlockedExchange64(&data_->outputScene,scene);
        // IMemInputPin::Receive uses S_FALSE for a rejected sample, not success.
        // https://learn.microsoft.com/windows/win32/api/strmif/nf-strmif-imeminputpin-receive
        InterlockedExchange64(&data_->outputTick,live && received==S_OK?GetTickCount64():0);InterlockedIncrement(&data_->sequence);
    }
    void stopped() {
        if(!data_)return;InterlockedIncrement(&data_->sequence);
        InterlockedExchange64(&data_->connectedTick,0);InterlockedExchange64(&data_->outputTick,0);
        InterlockedExchange64(&data_->connection,0);InterlockedIncrement(&data_->sequence);
    }
    // Publication metadata is protected by the existing frame mutex.
    void published(LONG64 session,LONG64 tick,uint64_t scene){if(data_){InterlockedExchange64(&data_->publishedSession,session);InterlockedExchange64(&data_->publishedTick,tick);InterlockedExchange64(&data_->publishedScene,scene);}}
    uint64_t publishedScene(LONG64 session,LONG64 tick)const{return data_ && ConsumerRead64(&data_->publishedSession)==session && ConsumerRead64(&data_->publishedTick)==tick?ConsumerRead64(&data_->publishedScene):0;}
    ConsumerSnapshot snapshot(LONG64 session,ULONGLONG now=GetTickCount64())const {
        if(!data_)return {};
        for(int attempt=0;attempt<4;++attempt){
            LONG before=InterlockedCompareExchange(&data_->sequence,0,0);if(before&1)continue;
            auto connected=ConsumerRead64(&data_->connectedTick),output=ConsumerRead64(&data_->outputTick);
            auto deliveredSession=ConsumerRead64(&data_->outputSession);
            ConsumerSnapshot value;value.app=ConsumerApp(InterlockedCompareExchange(&data_->app,0,0));
            value.connection=ConsumerRead64(&data_->connection);value.scene=ConsumerRead64(&data_->outputScene);
            if(before!=InterlockedCompareExchange(&data_->sequence,0,0))continue;
            value.connected=connected>0 && now>=ULONGLONG(connected) && now-ULONGLONG(connected)<1000;
            value.output=value.connected && output>0 && now>=ULONGLONG(output) && now-ULONGLONG(output)<1000 && deliveredSession==session;
            if(!value.connected)return {};return value;
        }
        ConsumerSnapshot unavailable;unavailable.valid=false;return unavailable;
    }
};
}
