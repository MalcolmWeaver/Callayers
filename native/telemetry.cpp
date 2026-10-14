#include "telemetry.h"
#include <winhttp.h>
#include <objbase.h>
#include <fstream>
#include <algorithm>
#include <cstdio>
#include <cctype>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace callayers {
using namespace winrt::Windows::Data::Json;
namespace {
std::string Id(){GUID guid{};if(FAILED(CoCreateGuid(&guid)))return {};guid.Data3=(guid.Data3&0xfff)|0x4000;guid.Data4[0]=(guid.Data4[0]&0x3f)|0x80;wchar_t raw[40]{};StringFromGUID2(guid,raw,40);auto id=winrt::to_string(std::wstring(raw+1,36));std::transform(id.begin(),id.end(),id.begin(),[](char c){return char(std::tolower(static_cast<unsigned char>(c)));});return id;}
std::string Timestamp(){SYSTEMTIME t{};GetSystemTime(&t);char text[32]{};sprintf_s(text,"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);return text;}
struct Internet {HINTERNET handle=nullptr;~Internet(){if(handle)WinHttpCloseHandle(handle);}};
int Deliver(const TelemetryCredentials& c,const std::string& body){
    std::wstring url=c.url;while(!url.empty() && url.back()==L'/')url.pop_back();url+=L"/events";
    URL_COMPONENTS p{};p.dwStructSize=sizeof(p);p.dwHostNameLength=p.dwUrlPathLength=p.dwExtraInfoLength=p.dwUserNameLength=p.dwPasswordLength=DWORD(-1);
    if(!WinHttpCrackUrl(url.c_str(),DWORD(url.size()),0,&p)||p.dwUserNameLength||p.dwPasswordLength||p.dwExtraInfoLength)return 400;
    std::wstring host(p.lpszHostName,p.dwHostNameLength);bool secure=p.nScheme==INTERNET_SCHEME_HTTPS;
    if(!secure && !(p.nScheme==INTERNET_SCHEME_HTTP && host==L"127.0.0.1"))return 400;
    Internet session{WinHttpOpen(L"Callayers/2.Telemetry",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0)};
    if(!session.handle)return 0;WinHttpSetTimeouts(session.handle,1000,1000,2000,6000);
    Internet connection{WinHttpConnect(session.handle,host.c_str(),p.nPort,0)};
    Internet request{connection.handle?WinHttpOpenRequest(connection.handle,L"POST",std::wstring(p.lpszUrlPath,p.dwUrlPathLength).c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,secure?WINHTTP_FLAG_SECURE:0):nullptr};
    if(!request.handle)return 0;DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;WinHttpSetOption(request.handle,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy));
    auto headers=L"Content-Type: application/json\r\nAuthorization: Bearer "+c.token+L"\r\n";
    if(!WinHttpSendRequest(request.handle,headers.c_str(),DWORD(headers.size()),const_cast<char*>(body.data()),DWORD(body.size()),DWORD(body.size()),0)||!WinHttpReceiveResponse(request.handle,nullptr))return 0;
    DWORD status=0,size=sizeof(status);if(!WinHttpQueryHeaders(request.handle,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX))return 0;return int(status);
}
}
TelemetrySender::TelemetrySender(Send send,std::filesystem::path diagnostic){
    if(!send)send=Deliver;
    if(diagnostic.empty()){wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);diagnostic=std::filesystem::path(exe).parent_path()/L"diagnostics"/L"telemetry.jsonl";}
    worker_=std::thread([this,send=std::move(send),diagnostic=std::move(diagnostic)]{work(send,diagnostic);});
}
TelemetrySender::~TelemetrySender(){{std::lock_guard lock(mutex_);stopping_=true;}wake_.notify_one();if(worker_.joinable())worker_.join();}
void TelemetrySender::account(TelemetryCredentials value,bool newSession)noexcept{try{std::lock_guard lock(mutex_);credentials_=std::move(value);if(newSession)session_=credentials_.uid.empty()?std::string{}:Id();}catch(...){} }
void TelemetrySender::record(TelemetryEvent event)noexcept{
    try{std::lock_guard lock(mutex_);if(stopping_ || session_.empty() || credentials_.token.empty() || credentials_.url.empty() || pending_.size()+inFlight_>=128)return;
        auto id=Id();if(id.empty())return;JsonObject properties;for(const auto& [key,value]:event.properties){auto name=winrt::to_hstring(key);if(auto b=std::get_if<bool>(&value))properties.SetNamedValue(name,JsonValue::CreateBooleanValue(*b));else properties.SetNamedValue(name,JsonValue::CreateStringValue(winrt::to_hstring(std::get<std::string>(value))));}
        JsonObject item;item.SetNamedValue(L"id",JsonValue::CreateStringValue(winrt::to_hstring(id)));item.SetNamedValue(L"sessionId",JsonValue::CreateStringValue(winrt::to_hstring(session_)));
        item.SetNamedValue(L"timestamp",JsonValue::CreateStringValue(winrt::to_hstring(Timestamp())));item.SetNamedValue(L"appVersion",JsonValue::CreateStringValue(L"0.2.0-telemetry.1"));
        item.SetNamedValue(L"event",JsonValue::CreateStringValue(winrt::to_hstring(event.name)));item.SetNamedValue(L"properties",properties);
        pending_.push_back({winrt::to_string(item.Stringify()),credentials_,GetTickCount64()});wake_.notify_one();
    }catch(...){} // Telemetry cannot fail an editor action.
}
void TelemetrySender::work(Send send,std::filesystem::path diagnostic){
    HRESULT apartment=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    std::error_code error;std::filesystem::create_directories(diagnostic.parent_path(),error);std::ofstream log(diagnostic,std::ios::trunc);size_t logged=0;
    auto line=[&](const std::string& value){if(logged+value.size()>1024*1024){log.close();log.open(diagnostic,std::ios::trunc);logged=0;}log<<value<<'\n';log.flush();logged+=value.size()+1;};
    for(;;){
        std::vector<Pending> batch;
        {std::unique_lock lock(mutex_);wake_.wait(lock,[this]{return stopping_ || !pending_.empty();});if(pending_.empty())break;
            if(!stopping_)wake_.wait_for(lock,std::chrono::seconds(2),[this]{return stopping_ || pending_.size()>=32;});
            while(!pending_.empty() && GetTickCount64()-pending_.front().queued>300000)pending_.pop_front();
            if(pending_.empty()){if(stopping_)break;continue;}
            const auto owner=pending_.front().credentials.uid,url=pending_.front().credentials.url;
            while(!pending_.empty() && batch.size()<32 && pending_.front().credentials.uid==owner && pending_.front().credentials.url==url){batch.push_back(std::move(pending_.front()));pending_.pop_front();}inFlight_=batch.size();
        }
        std::string body="{\"events\":[";for(size_t i=0;i<batch.size();++i){if(i)body+=',';body+=batch[i].json;line("{\"type\":\"event\",\"data\":"+batch[i].json+"}");}body+="]}";
        for(int attempt=0;attempt<3;++attempt){
            auto credentials=batch.front().credentials;
            {std::lock_guard lock(mutex_);if(credentials_.uid==credentials.uid && credentials_.url==credentials.url && !credentials_.token.empty())credentials.token=credentials_.token;}
            int status=0;try{status=send(credentials,body);}catch(...){}
            line("{\"type\":\"delivery\",\"status\":"+std::to_string(status)+",\"count\":"+std::to_string(batch.size())+",\"attempt\":"+std::to_string(attempt+1)+"}");
            if((status>=200 && status<300) || (status>=400 && status<500 && status!=401 && status!=408 && status!=429))break;
            std::unique_lock lock(mutex_);if(stopping_ || attempt==2)break;
            wake_.wait_for(lock,std::chrono::seconds(attempt==0?2:10),[this]{return stopping_;});if(stopping_)break;
        }
        {std::lock_guard lock(mutex_);inFlight_=0;if(stopping_){pending_.clear();break;}}
    }
    if(SUCCEEDED(apartment))CoUninitialize();
}
}
