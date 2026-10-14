#include "generation.h"
#include <winhttp.h>
#include <objbase.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <functional>
namespace callayers {
using namespace winrt::Windows::Data::Json;
namespace {
struct Internet {HINTERNET handle=nullptr;~Internet(){if(handle)WinHttpCloseHandle(handle);}};
struct HttpResult {DWORD status;JsonObject json;};
HttpResult request(const std::wstring& token,const std::string& body,std::atomic<void*>& active,const std::function<bool()>& cancelled){
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);std::wstring url;
    wchar_t configured[2049]{};if(GetEnvironmentVariableW(L"SERVICE_URL",configured,2049))url=configured;
    else{std::ifstream file(std::filesystem::path(exe).parent_path()/L"service-url.txt");std::string text{std::istreambuf_iterator<char>(file),{}};if(text.size()<=2048)url=winrt::to_hstring(text);}
    while(!url.empty() && (url.back()==L'/'||iswspace(url.back())))url.pop_back();url+=L"/generate";
    URL_COMPONENTS parts{};parts.dwStructSize=sizeof(parts);parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwUserNameLength=parts.dwPasswordLength=DWORD(-1);
    if(!WinHttpCrackUrl(url.c_str(),DWORD(url.size()),0,&parts)||parts.dwUserNameLength||parts.dwPasswordLength||parts.nScheme!=INTERNET_SCHEME_HTTPS)throw std::runtime_error("Invalid service");
    Internet session{WinHttpOpen(L"Callayers/2",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0)};
    if(!session.handle)throw std::runtime_error("Cannot connect");WinHttpSetTimeouts(session.handle,5000,5000,10000,85000);
    Internet connection{WinHttpConnect(session.handle,std::wstring(parts.lpszHostName,parts.dwHostNameLength).c_str(),parts.nPort,0)};
    Internet req{connection.handle?WinHttpOpenRequest(connection.handle,L"POST",std::wstring(parts.lpszUrlPath,parts.dwUrlPathLength).c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE):nullptr};
    if(!req.handle)throw std::runtime_error("Cannot connect");DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;WinHttpSetOption(req.handle,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy));
    const auto requestHandle=req.handle;req.handle=nullptr;active=requestHandle;
    struct Active {std::atomic<void*>& shared;HINTERNET handle;~Active(){if(shared.exchange(nullptr)==handle)WinHttpCloseHandle(handle);}} guard{active,requestHandle};
    if(cancelled())throw std::runtime_error("Cancelled");
    auto headers=L"Content-Type: application/json\r\nAuthorization: Bearer "+token+L"\r\n";
    if(!WinHttpSendRequest(requestHandle,headers.c_str(),DWORD(headers.size()),const_cast<char*>(body.data()),DWORD(body.size()),DWORD(body.size()),0)||!WinHttpReceiveResponse(requestHandle,nullptr))throw std::runtime_error("Request lost");
    DWORD status=0,size=sizeof(status);WinHttpQueryHeaders(requestHandle,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX);
    std::string response;char chunk[8192];DWORD count=0;
    while(WinHttpReadData(requestHandle,chunk,sizeof(chunk),&count)&&count){if(response.size()+count>524288)throw std::runtime_error("Response too large");response.append(chunk,count);}
    return {status,JsonObject::Parse(winrt::to_hstring(response))};
}
}
Generation::~Generation(){cancel();if(worker_.joinable())worker_.join();}
void Generation::cancel(){++version_;auto handle=activeRequest_.exchange(nullptr);if(handle)WinHttpCloseHandle(handle);std::lock_guard lock(mutex_);state_.busy=false;state_.result.reset();state_.message=L"Cancelled. Processing may already have used a generation.";}
GenerationSnapshot Generation::snapshot(){std::lock_guard lock(mutex_);return state_;}
std::optional<GeneratedCard> Generation::takeResult(){std::lock_guard lock(mutex_);auto result=std::move(state_.result);state_.result.reset();return result;}
int Generation::takeAccessError(){std::lock_guard lock(mutex_);int result=state_.accessError;state_.accessError=0;return result;}
void Generation::start(const std::wstring& prompt,const std::wstring& token){
    if(working_){std::lock_guard lock(mutex_);state_.message=L"The cancelled request is still finishing. Try Create again shortly.";return;}
    if(worker_.joinable())worker_.join();if(prompt.empty()||winrt::to_string(prompt).size()>12000||token.empty()){std::lock_guard lock(mutex_);state_.message=L"Enter a prompt and sign in to create a card.";return;}
    const auto version=++version_;{std::lock_guard lock(mutex_);state_.busy=true;state_.message=L"Creating your card…";state_.result.reset();}working_=true;
    worker_=std::thread([this,version,prompt,token]{winrt::init_apartment();GenerationSnapshot result;
        try{GUID guid{};if(FAILED(CoCreateGuid(&guid)))throw std::runtime_error("Cannot identify request");guid.Data3=(guid.Data3&0x0fff)|0x4000;guid.Data4[0]=(guid.Data4[0]&0x3f)|0x80;wchar_t raw[40]{};StringFromGUID2(guid,raw,40);
            std::wstring id(raw+1,36);std::transform(id.begin(),id.end(),id.begin(),::towlower);
            JsonObject input;input.SetNamedValue(L"requestId",JsonValue::CreateStringValue(id));input.SetNamedValue(L"prompt",JsonValue::CreateStringValue(prompt));
            auto response=request(token,winrt::to_string(input.Stringify()),activeRequest_,[this,version]{return version_!=version;});
            if(response.json.HasKey(L"allowance"))result.remaining=int(response.json.GetNamedObject(L"allowance").GetNamedNumber(L"remaining"));
            if(response.status==200){auto data=response.json.GetNamedObject(L"card");GeneratedCard card;card.id=id;card.name=data.GetNamedString(L"name");card.html=data.GetNamedString(L"html");card.width=data.GetNamedNumber(L"width");card.height=data.GetNamedNumber(L"height");card.w=card.width;card.h=card.height;card.x=std::min(48.0,1280-card.w);card.y=std::min(48.0,720-card.h);
                if(response.json.GetNamedString(L"requestId")!=id||!ValidGeneratedCard(card))throw std::runtime_error("Invalid generated card");result.result=std::move(card);result.message=L"Card created. It will appear in your saved cards.";
            }else{auto error=response.json.GetNamedString(L"error",L"");result.accessError=response.status==403?2:response.status==401?1:0;
                result.message=error==L"daily_limit_reached"?L"You've reached today's generation limit. It resets at midnight UTC.":error==L"website_tools_not_ready"?L"Website reading is coming next. Paste the content to create this card now.":response.status==401||response.status==403?L"Sign in again to create a card.":L"Couldn't create this card. Create again starts a new generation and may use another allowance unit.";}
        }catch(...){result.message=L"The response couldn't be received or validated. It will not be retried automatically. Create again starts a new generation and may use another allowance unit.";}
        {std::lock_guard lock(mutex_);if(version_==version){if(result.remaining<0)result.remaining=state_.remaining;state_=std::move(result);}}working_=false;winrt::uninit_apartment();
    });
}
}
