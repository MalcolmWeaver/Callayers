#pragma once
#include "common.h"

namespace callayers {
#ifdef CALLAYERS_DEMAND_TEST_PIPE
inline constexpr wchar_t DemandPipeName[]=L"\\\\.\\pipe\\Callayers.CameraDemand.test.source";
#else
inline constexpr wchar_t DemandPipeName[]=L"\\\\.\\pipe\\Callayers.CameraDemand.v1";
#endif

// A handle is a stream lease. Windows closes it even when its owner crashes.
// No counters or expiry estimates can outlive the consuming process.
class CameraDemandClient {
    Handle connection;
public:
    void Update(bool running,const wchar_t* name=DemandPipeName){
        if(!running){connection.reset();return;}
        DWORD available=0;
        if(connection&&!PeekNamedPipe(connection.value,nullptr,0,nullptr,&available,nullptr))connection.reset();
        if(!connection)connection.reset(CreateFileW(name,GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr));
    }
};

class CameraDemandServer {
    std::wstring name;
    PSECURITY_DESCRIPTOR security=nullptr;
    Handle pending,event{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    OVERLAPPED accept{};
    bool waiting=false,connected=false;
    std::vector<std::unique_ptr<Handle>> clients;
    void Listen(bool first=false){
        SECURITY_ATTRIBUTES attributes{sizeof(attributes),security,FALSE};
        pending.reset(CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|
            (first?FILE_FLAG_FIRST_PIPE_INSTANCE:0),PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,
            PIPE_UNLIMITED_INSTANCES,64,64,0,&attributes));
        if(!pending)throw HRESULT_FROM_WIN32(GetLastError());
        ResetEvent(event.value);accept={};accept.hEvent=event.value;
        connected=ConnectNamedPipe(pending.value,&accept)!=FALSE;
        const DWORD error=connected?ERROR_SUCCESS:GetLastError();
        connected=connected||error==ERROR_PIPE_CONNECTED;
        waiting=!connected&&error==ERROR_IO_PENDING;
        if(!connected&&!waiting)throw HRESULT_FROM_WIN32(error);
    }
public:
    explicit CameraDemandServer(const wchar_t* pipeName=DemandPipeName):name(pipeName){
        Handle token;Check(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.value)?S_OK:HRESULT_FROM_WIN32(GetLastError()));
        DWORD bytes=0;GetTokenInformation(token.value,TokenUser,nullptr,0,&bytes);std::vector<BYTE> info(bytes);
        Check(GetTokenInformation(token.value,TokenUser,info.data(),bytes,&bytes)?S_OK:HRESULT_FROM_WIN32(GetLastError()));
        wchar_t* sid=nullptr;Check(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(info.data())->User.Sid,&sid)?S_OK:HRESULT_FROM_WIN32(GetLastError()));
        std::wstring sddl=L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;LS)(A;;GRGW;;;";sddl+=sid;sddl+=L")";LocalFree(sid);
        Check(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&security,nullptr)?S_OK:HRESULT_FROM_WIN32(GetLastError()));
        try{if(!event)throw HRESULT_FROM_WIN32(GetLastError());Listen(true);}catch(...){LocalFree(security);throw;}
    }
    ~CameraDemandServer(){
        if(waiting){CancelIoEx(pending.value,&accept);DWORD bytes=0;GetOverlappedResult(pending.value,&accept,&bytes,TRUE);}
        LocalFree(security);
    }
    size_t Poll(){
        if(waiting&&WaitForSingleObject(event.value,0)==WAIT_OBJECT_0){
            DWORD bytes=0;connected=GetOverlappedResult(pending.value,&accept,&bytes,FALSE)!=FALSE;waiting=false;
            if(!connected)Listen();
        }
        if(connected){
            clients.push_back(std::make_unique<Handle>(pending.value));pending.value=nullptr;connected=false;Listen();
        }
        std::erase_if(clients,[](const auto& client){DWORD bytes=0;return !PeekNamedPipe(client->value,nullptr,0,nullptr,&bytes,nullptr);});
        return clients.size();
    }
};
inline bool NeedsCameraFrames(bool previewVisible,size_t streams,ULONGLONG lastRequest=0,ULONGLONG now=GetTickCount64()){
    return previewVisible||streams!=0||(lastRequest&&now-lastRequest<1000);
}
inline bool ShouldProduceFrames(bool accessAllowed,bool previewVisible,bool hasSourceOrContent,
                                size_t streams,ULONGLONG lastRequest,ULONGLONG now=GetTickCount64()){
    return accessAllowed&&((previewVisible&&hasSourceOrContent)||streams!=0||
        (lastRequest&&now-lastRequest<1000));
}
}
