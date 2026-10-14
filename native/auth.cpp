#include <winsock2.h>
#include <ws2tcpip.h>
#include "auth.h"
#include "auth_protocol.h"
#include "resource_diagnostics.h"
#include <winhttp.h>
#include <objbase.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <functional>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <stdexcept>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace callayers {
using namespace winrt::Windows::Data::Json;
namespace {
enum class FailureKind { Temporary, InvalidSession, Denied };
struct Failure {FailureKind kind;std::wstring message;};
struct Cancelled {};
struct InternetHandle {
    HINTERNET value=nullptr;
    ~InternetHandle(){if(value)WinHttpCloseHandle(value);}
};
struct SocketHandle {
    SOCKET value=INVALID_SOCKET;
    ~SocketHandle(){if(value!=INVALID_SOCKET)closesocket(value);}
};
using CancelCheck=std::function<bool()>;
void CheckCancelled(const CancelCheck& cancelled){if(cancelled())throw Cancelled{};}
std::wstring Required(const JsonObject& data,const wchar_t* name,size_t maximum=16384){
    auto value=std::wstring(data.GetNamedString(name));if(value.empty() || value.size()>maximum)throw Failure{FailureKind::Temporary,L"The sign-in service returned an unexpected response. Try again."};return value;
}
std::string Form(const std::map<std::string,std::string>& values){std::string body;for(const auto& [key,value]:values){if(!body.empty())body+='&';body+=AuthUrlEncode(key)+'='+AuthUrlEncode(value);}return body;}
std::wstring ServiceUrl() {
    DWORD size=GetEnvironmentVariableW(L"SERVICE_URL",nullptr,0);std::wstring url;
    if(size && size<=2049){url.resize(size);DWORD copied=GetEnvironmentVariableW(L"SERVICE_URL",url.data(),size);url.resize(copied);}
    if(url.empty()){
        wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
        std::ifstream file(std::filesystem::path(exe).parent_path()/L"service-url.txt",std::ios::binary);
        std::string bytes{std::istreambuf_iterator<char>(file),{}};if(bytes.size()<=2048)url=winrt::to_hstring(bytes);
    }
    while(!url.empty() && (url.back()==L'/' || iswspace(url.back())))url.pop_back();
    if(url.empty())throw Failure{FailureKind::Temporary,L"Callayers sign-in isn't configured yet. Try again after setup is complete."};return url;
}
JsonObject Request(const std::wstring& url,const std::wstring& method,const std::string& body,const std::wstring& type,const std::wstring& token,const CancelCheck& cancelled,bool account=false) {
    CheckCancelled(cancelled);URL_COMPONENTS parts{};parts.dwStructSize=sizeof(parts);parts.dwHostNameLength=DWORD(-1);parts.dwUrlPathLength=DWORD(-1);parts.dwExtraInfoLength=DWORD(-1);parts.dwUserNameLength=DWORD(-1);parts.dwPasswordLength=DWORD(-1);
    if(!WinHttpCrackUrl(url.c_str(),DWORD(url.size()),0,&parts) || parts.dwUserNameLength || parts.dwPasswordLength || url.find(L'#')!=std::wstring::npos)throw Failure{FailureKind::Temporary,L"Callayers sign-in isn't configured correctly."};
    std::wstring host(parts.lpszHostName,parts.dwHostNameLength),path(parts.lpszUrlPath,parts.dwUrlPathLength);if(path.empty())path=L"/";if(parts.dwExtraInfoLength)path.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);
    bool secure=parts.nScheme==INTERNET_SCHEME_HTTPS;
    if(!secure && !(parts.nScheme==INTERNET_SCHEME_HTTP && host==L"127.0.0.1"))throw Failure{FailureKind::Temporary,L"Callayers requires a secure sign-in service."};
    InternetHandle session{WinHttpOpen(L"Callayers/2",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0)};
    if(!session.value)throw Failure{FailureKind::Temporary,L"Couldn't connect to sign-in. Check your internet connection and Retry."};
    WinHttpSetTimeouts(session.value,4000,4000,5000,8000);
    InternetHandle connection{WinHttpConnect(session.value,host.c_str(),parts.nPort,0)};
    InternetHandle request{connection.value?WinHttpOpenRequest(connection.value,method.c_str(),path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,secure?WINHTTP_FLAG_SECURE:0):nullptr};
    if(!request.value)throw Failure{FailureKind::Temporary,L"Couldn't connect to sign-in. Check your internet connection and Retry."};
    DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;WinHttpSetOption(request.value,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy));
    std::wstring headers=L"Accept: application/json\r\n";if(!body.empty())headers+=L"Content-Type: "+type+L"\r\n";
    if(!token.empty())headers+=L"Authorization: Bearer "+token+L"\r\n";
    BOOL sent=WinHttpSendRequest(request.value,headers.c_str(),DWORD(headers.size()),body.empty()?WINHTTP_NO_REQUEST_DATA:const_cast<char*>(body.data()),DWORD(body.size()),DWORD(body.size()),0);
    if(!sent || !WinHttpReceiveResponse(request.value,nullptr)){CheckCancelled(cancelled);throw Failure{FailureKind::Temporary,L"Couldn't connect to sign-in. Check your internet connection and Retry."};}
    CheckCancelled(cancelled);DWORD status=0,statusSize=sizeof(status);WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&statusSize,WINHTTP_NO_HEADER_INDEX);
    std::string response;
    for(;;){CheckCancelled(cancelled);DWORD available=0;if(!WinHttpQueryDataAvailable(request.value,&available))throw Failure{FailureKind::Temporary,L"The sign-in connection was interrupted. Retry."};if(!available)break;
        if(response.size()+available>65536)throw Failure{FailureKind::Temporary,L"The sign-in service returned an unexpected response. Retry."};
        size_t offset=response.size();response.resize(offset+available);DWORD read=0;if(!WinHttpReadData(request.value,response.data()+offset,available,&read))throw Failure{FailureKind::Temporary,L"The sign-in connection was interrupted. Retry."};response.resize(offset+read);
    }
    JsonObject data;bool parsed=JsonObject::TryParse(winrt::to_hstring(response),data);
    if(status<200 || status>=300){
        if(account && status==401)throw Failure{FailureKind::InvalidSession,L"Your sign-in expired. Sign in with Google again."};
        if(account && status==403)throw Failure{FailureKind::Denied,L"This account can't use Callayers. Try another Google account."};
        std::wstring code;if(parsed)try {auto error=data.GetNamedValue(L"error");code=error.ValueType()==JsonValueType::String?std::wstring(error.GetString()):std::wstring(error.GetObject().GetNamedString(L"message"));}catch(...){}
        for(const wchar_t* invalid:{L"TOKEN_EXPIRED",L"USER_DISABLED",L"USER_NOT_FOUND",L"INVALID_REFRESH_TOKEN",L"INVALID_ID_TOKEN"})
            if(code==invalid || code.starts_with(std::wstring(invalid)+L" :"))throw Failure{FailureKind::InvalidSession,L"Your sign-in expired. Sign in with Google again."};
        throw Failure{FailureKind::Temporary,L"Couldn't finish sign-in. Try again."};
    }
    if(!parsed)throw Failure{FailureKind::Temporary,L"The sign-in service returned an unexpected response. Retry."};return data;
}
struct Config {std::wstring url,project,client,desktopSecret,apiKey;};
Config LoadConfig(const CancelCheck& cancelled) {
    Config config;config.url=ServiceUrl();auto data=Request(config.url+L"/client-config",L"GET",{},L"",L"",cancelled);
    if(data.GetNamedNumber(L"version")!=2)throw Failure{FailureKind::Temporary,L"Callayers sign-in isn't configured correctly."};
    config.project=Required(data,L"firebaseProjectId",128);config.client=Required(data,L"googleDesktopClientId",256);config.desktopSecret=Required(data,L"googleDesktopClientSecret",256);config.apiKey=Required(data,L"firebaseApiKey",256);return config;
}
struct Grant {StoredAuthSession session;std::wstring token,email;ULONGLONG expires=0;};
Grant Refresh(const Config& config,StoredAuthSession session,const CancelCheck& cancelled) {
    if(session.serviceUrl!=config.url || session.projectId!=config.project)throw Failure{FailureKind::InvalidSession,L"Sign in with Google to reconnect this Callayers installation."};
    auto body=Form({{"grant_type","refresh_token"},{"refresh_token",winrt::to_string(session.refreshToken)}});
    auto result=Request(L"https://securetoken.googleapis.com/v1/token?key="+std::wstring(winrt::to_hstring(AuthUrlEncode(winrt::to_string(config.apiKey)))),L"POST",body,L"application/x-www-form-urlencoded",L"",cancelled);
    Grant grant;grant.token=Required(result,L"id_token");grant.session=session;grant.session.refreshToken=Required(result,L"refresh_token");
    if(Required(result,L"user_id",128)!=session.uid)throw Failure{FailureKind::InvalidSession,L"Your saved sign-in couldn't be confirmed. Sign in again."};
    auto seconds=Required(result,L"expires_in",10);if(!std::all_of(seconds.begin(),seconds.end(),[](wchar_t c){return c>=L'0' && c<=L'9';}))throw std::runtime_error("Invalid expiry");
    ULONGLONG lifetime=std::stoull(seconds);if(lifetime<60 || lifetime>86400)throw std::runtime_error("Invalid expiry");grant.expires=GetTickCount64()+lifetime*1000;
    auto account=Request(config.url+L"/account",L"GET",{},L"",grant.token,cancelled,true);
    if(Required(account,L"uid",128)!=session.uid || !account.GetNamedBoolean(L"accessAllowed"))throw Failure{FailureKind::Denied,L"This account can't use Callayers. Try another Google account."};
    grant.email=account.GetNamedString(L"email",L"");return grant;
}
void Reply(SOCKET socket,bool success,const std::string& message) {
    std::string html="<!doctype html><meta charset=utf-8><title>Callayers</title><h1>Callayers</h1><p>"+message+"</p>";
    auto response=std::string("HTTP/1.1 ")+(success?"200 OK":"400 Bad Request")+"\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nContent-Security-Policy: default-src 'none'; frame-ancestors 'none'\r\nConnection: close\r\nContent-Length: "+std::to_string(html.size())+"\r\n\r\n"+html;
    size_t offset=0;while(offset<response.size()){int sent=send(socket,response.data()+offset,int(response.size()-offset),0);if(sent<=0)break;offset+=sent;}
}
void ReplySignedIn(SOCKET socket,const std::string& ticket) {
    // Only an expiring analytics ticket reaches the website, never OAuth codes or login tokens.
    const std::string location="https://callayers.com/signed-in"+(ticket.empty()?std::string{}:"#ticket="+ticket);
    const std::string response="HTTP/1.1 303 See Other\r\nLocation: "+location+"\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nContent-Security-Policy: default-src 'none'; frame-ancestors 'none'\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
    size_t offset=0;while(offset<response.size()){int sent=send(socket,response.data()+offset,int(response.size()-offset),0);if(sent<=0)break;offset+=sent;}
}
std::string ReadCallback(SOCKET socket,const std::string& host,const CancelCheck& cancelled) {
    std::string request;ULONGLONG start=GetTickCount64();
    while(request.find("\r\n\r\n")==std::string::npos && request.size()<8192 && GetTickCount64()-start<2000){
        CheckCancelled(cancelled);char buffer[1024];int got=recv(socket,buffer,sizeof(buffer),0);if(got>0)request.append(buffer,got);else if(got==0)break;else if(WSAGetLastError()!=WSAETIMEDOUT)return {};
    }
    auto line=request.find("\r\n");if(line==std::string::npos || !request.starts_with("GET ") || request.find("\r\n\r\n")==std::string::npos)return {};
    auto end=request.find(" HTTP/1.1",4);if(end==std::string::npos || end!=line-9)return {};
    bool matched=false;size_t at=line+2;
    while(at<request.size()){auto next=request.find("\r\n",at);if(next==std::string::npos || next==at)break;auto field=request.substr(at,next-at);auto colon=field.find(':');
        if(colon!=std::string::npos){auto key=field.substr(0,colon);std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return char(std::tolower(c));});if(key=="host"){auto value=field.substr(colon+1);while(!value.empty() && value.front()==' ')value.erase(value.begin());if(matched || value!=host)return {};matched=true;}}
        at=next+2;
    }
    return matched?request.substr(4,end-4):std::string{};
}
void Interactive(const Config& config,const CancelCheck& cancelled,const std::function<void(std::wstring)>& openBrowser,const std::function<void(Grant)>& accepted) {
    SocketHandle listener{socket(AF_INET,SOCK_STREAM,IPPROTO_TCP)};if(listener.value==INVALID_SOCKET)throw Failure{FailureKind::Temporary,L"Couldn't prepare Google sign-in. Try again."};
    BOOL exclusive=TRUE;setsockopt(listener.value,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&exclusive),sizeof(exclusive));
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(bind(listener.value,reinterpret_cast<sockaddr*>(&address),sizeof(address)) || listen(listener.value,4))throw Failure{FailureKind::Temporary,L"Couldn't prepare Google sign-in. Try again."};
    int length=sizeof(address);getsockname(listener.value,reinterpret_cast<sockaddr*>(&address),&length);
    std::string host="127.0.0.1:"+std::to_string(ntohs(address.sin_port)),redirect="http://"+host+"/callback";
    auto state=AuthRandomToken(32),verifier=AuthRandomToken(48);
    auto query=Form({{"client_id",winrt::to_string(config.client)},{"redirect_uri",redirect},{"response_type","code"},{"scope","openid email profile"},{"state",state},{"code_challenge",AuthPkceChallenge(verifier)},{"code_challenge_method","S256"},{"prompt","select_account"}});
    openBrowser(std::wstring(winrt::to_hstring("https://accounts.google.com/o/oauth2/v2/auth?"+query)));
    ULONGLONG deadline=GetTickCount64()+300000;
    while(GetTickCount64()<deadline){
        CheckCancelled(cancelled);fd_set read{};FD_ZERO(&read);FD_SET(listener.value,&read);timeval wait{0,100000};int ready=select(0,&read,nullptr,nullptr,&wait);
        if(ready<0)throw Failure{FailureKind::Temporary,L"Google sign-in was interrupted. Try again."};if(!ready)continue;
        SocketHandle peer{accept(listener.value,nullptr,nullptr)};if(peer.value==INVALID_SOCKET)continue;DWORD receive=250,sendTimeout=2000;
        setsockopt(peer.value,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&receive),sizeof(receive));setsockopt(peer.value,SOL_SOCKET,SO_SNDTIMEO,reinterpret_cast<const char*>(&sendTimeout),sizeof(sendTimeout));
        auto callback=ParseAuthCallback(ReadCallback(peer.value,host,cancelled),state);
        if(callback.kind==CallbackKind::Ignore){Reply(peer.value,false,"This is not the expected sign-in callback. Return to Google to finish signing in.");continue;}
        // Close the listener as soon as the matching callback is consumed.
        closesocket(listener.value);listener.value=INVALID_SOCKET;
        if(callback.kind==CallbackKind::Cancelled){Reply(peer.value,false,"Sign-in was cancelled. Return to Callayers to try again.");throw Failure{FailureKind::Temporary,L"Google sign-in was cancelled. Try again."};}
        try {
            auto google=Request(L"https://oauth2.googleapis.com/token",L"POST",Form({{"client_id",winrt::to_string(config.client)},{"client_secret",winrt::to_string(config.desktopSecret)},{"code",callback.code},{"redirect_uri",redirect},{"grant_type","authorization_code"},{"code_verifier",verifier}}),L"application/x-www-form-urlencoded",L"",cancelled);
            JsonObject payload;payload.SetNamedValue(L"requestUri",JsonValue::CreateStringValue(winrt::to_hstring(redirect)));payload.SetNamedValue(L"postBody",JsonValue::CreateStringValue(winrt::to_hstring(Form({{"id_token",winrt::to_string(Required(google,L"id_token"))},{"providerId","google.com"}}))));payload.SetNamedValue(L"returnSecureToken",JsonValue::CreateBooleanValue(true));
            auto firebase=Request(L"https://identitytoolkit.googleapis.com/v1/accounts:signInWithIdp?key="+std::wstring(winrt::to_hstring(AuthUrlEncode(winrt::to_string(config.apiKey)))),L"POST",winrt::to_string(payload.Stringify()),L"application/json",L"",cancelled);
            StoredAuthSession session{config.url,config.project,Required(firebase,L"localId",128),Required(firebase,L"refreshToken")};
            auto grant=Refresh(config,session,cancelled);CheckCancelled(cancelled);
            auto attributionToken=grant.token;
            accepted(std::move(grant));
            std::string ticket;
            try {
                auto attribution=Request(config.url+L"/attribution/ticket",L"POST",{},L"",attributionToken,cancelled);
                auto value=winrt::to_string(Required(attribution,L"ticket",43));
                if(value.size()==43 && std::all_of(value.begin(),value.end(),[](unsigned char c){return std::isalnum(c)||c=='-'||c=='_';}))ticket=std::move(value);
            }catch(...) { /* Analytics never changes the accepted sign-in result. */ }
            SecureZeroMemory(attributionToken.data(),attributionToken.size()*sizeof(wchar_t));
            ReplySignedIn(peer.value,ticket);return;
        }catch(...){Reply(peer.value,false,"Couldn't finish signing in. Return to Callayers and Retry.");throw;}
    }
    throw Failure{FailureKind::Temporary,L"Google sign-in timed out. Try again."};
}
}
Auth::Auth(){worker_=std::thread([this]{work();});}
Auth::~Auth(){stopping_=true;++generation_;wake_.notify_one();worker_.join();}
void Auth::enqueue(Action action) {
    std::lock_guard lock(mutex_);if(busy_ || (snapshot_.allowed && action==Action::SignIn))return;
    unsigned generation=++generation_;busy_=true;pending_=Job{action,generation};
    if(!snapshot_.allowed){snapshot_.phase=action==Action::SignIn?AuthPhase::SigningIn:AuthPhase::Checking;snapshot_.message=action==Action::SignIn?L"Opening Google sign-in…":L"Checking your sign-in…";++snapshot_.revision;}
    wake_.notify_one();
}
void Auth::restore(){enqueue(Action::Restore);}
void Auth::signIn(){enqueue(Action::SignIn);}
void Auth::retry(){bool save=false,forget=false,allowed=false;{std::lock_guard lock(mutex_);allowed=snapshot_.allowed;save=allowed && snapshot_.rememberFailed;forget=snapshot_.forgetFailed;}if(forget)signOut();else enqueue(save?Action::Save:allowed?Action::Renew:Action::Restore);}
void Auth::cancel(){std::lock_guard lock(mutex_);if(snapshot_.allowed)return;++generation_;pending_.reset();browserUrl_.clear();busy_=false;snapshot_.phase=AuthPhase::SignedOut;snapshot_.message=L"Sign-in cancelled. Try again.";++snapshot_.revision;}
void Auth::signOut(){std::lock_guard lock(mutex_);gate_=false;if(revoke_)revoke_();++generation_;pending_.reset();browserUrl_.clear();busy_=false;idToken_.clear();session_={};renewAt_=expiresAt_=0;bool forgot=ClearAuthSession();snapshot_={AuthPhase::SignedOut,false,false,snapshot_.revision+1,forgot?L"Sign in to use Callayers.":L"Signed out. Couldn't remove your saved sign-in. Retry.",L""};snapshot_.forgetFailed=snapshot_.retryAvailable=!forgot;}
void Auth::tick(){bool renew=false;{std::lock_guard lock(mutex_);renew=snapshot_.allowed && !busy_ && renewAt_ && GetTickCount64()>=renewAt_;}if(renew)enqueue(Action::Renew);}
AuthSnapshot Auth::snapshot(){std::lock_guard lock(mutex_);return snapshot_;}
std::wstring Auth::takeBrowserUrl(){std::lock_guard lock(mutex_);auto url=std::move(browserUrl_);browserUrl_.clear();return url;}
std::wstring Auth::idToken(){std::lock_guard lock(mutex_);return snapshot_.allowed && GetTickCount64()<expiresAt_?idToken_:L"";}
#ifdef CALLAYERS_TELEMETRY
TelemetryCredentials Auth::telemetryCredentials(){std::lock_guard lock(mutex_);if(!snapshot_.allowed)return {};return {session_.serviceUrl,session_.uid,GetTickCount64()<expiresAt_?idToken_:L""};}
#endif
void Auth::onRevoke(std::function<void()> callback){std::lock_guard lock(mutex_);revoke_=std::move(callback);}
void Auth::work() {
    HRESULT apartment=CoInitializeEx(nullptr,COINIT_MULTITHREADED);WSADATA sockets{};int socketsReady=WSAStartup(MAKEWORD(2,2),&sockets);Config config;
    while(!stopping_){
        Job job{};{std::unique_lock lock(mutex_);wake_.wait(lock,[this]{return stopping_ || pending_.has_value();});if(stopping_)break;job=*pending_;pending_.reset();}
        CancelCheck cancelled=[this,generation=job.generation]{return stopping_ || generation_!=generation;};
        bool restoreAvailable=false;
        try {
            CheckCancelled(cancelled);if(FAILED(apartment) || socketsReady)throw std::runtime_error("Sign-in runtime unavailable");
            if(job.action==Action::Save){std::lock_guard lock(mutex_);CheckCancelled(cancelled);try{SaveAuthSession(session_);snapshot_.rememberFailed=false;snapshot_.message=L"";}catch(...){snapshot_.rememberFailed=true;snapshot_.message=L"Couldn't remember your sign-in. Retry.";}busy_=false;++snapshot_.revision;continue;}
            StoredAuthSession session;
            if(job.action==Action::Restore){session=LoadAuthSession();if(session.refreshToken.empty()){std::lock_guard lock(mutex_);CheckCancelled(cancelled);busy_=false;snapshot_.phase=AuthPhase::SignedOut;snapshot_.message=L"Sign in to use Callayers.";snapshot_.retryAvailable=false;++snapshot_.revision;continue;}restoreAvailable=true;}
            else if(job.action==Action::Renew){std::lock_guard lock(mutex_);session=session_;}
            if(job.action!=Action::Renew || config.url.empty())config=LoadConfig(cancelled);
            auto accepted=[this,&cancelled](Grant grant){
                std::lock_guard lock(mutex_);CheckCancelled(cancelled);session_=std::move(grant.session);idToken_=std::move(grant.token);expiresAt_=grant.expires;renewAt_=grant.expires-60000;
                bool remembered=true;try{SaveAuthSession(session_);}catch(...){remembered=false;}
                snapshot_={AuthPhase::SignedIn,true,!remembered,snapshot_.revision+1,remembered?L"":L"Couldn't remember your sign-in. Retry.",std::move(grant.email)};gate_=true;busy_=false;
            };
            if(job.action==Action::SignIn)Interactive(config,cancelled,[this,&cancelled](std::wstring url){std::lock_guard lock(mutex_);CheckCancelled(cancelled);browserUrl_=std::move(url);snapshot_.message=L"Finish signing in with Google in your browser.";++snapshot_.revision;},accepted);
            else accepted(Refresh(config,session,cancelled));
        }catch(const Cancelled&){}
        catch(const Failure& failure){
            std::lock_guard lock(mutex_);if(cancelled())continue;busy_=false;browserUrl_.clear();
            if(snapshot_.allowed && failure.kind==FailureKind::Temporary){renewAt_=GetTickCount64()+30000;snapshot_.message=L"Account connection unavailable. Retrying…";}
            else {gate_=false;if(revoke_)revoke_();if(failure.kind==FailureKind::InvalidSession)ClearAuthSession();snapshot_.allowed=false;snapshot_.phase=AuthPhase::SignedOut;snapshot_.message=failure.message;snapshot_.retryAvailable=restoreAvailable && failure.kind==FailureKind::Temporary;idToken_.clear();}
            ++snapshot_.revision;Resources().event("auth_request_failed");
        }catch(...){std::lock_guard lock(mutex_);if(cancelled())continue;busy_=false;browserUrl_.clear();if(snapshot_.allowed){renewAt_=GetTickCount64()+30000;snapshot_.message=L"Account connection unavailable. Retrying…";}else{snapshot_.phase=AuthPhase::SignedOut;snapshot_.message=L"Couldn't confirm your sign-in. Sign in with Google to try again.";}++snapshot_.revision;Resources().event("auth_operation_failed");}
    }
    if(!socketsReady)WSACleanup();if(SUCCEEDED(apartment))CoUninitialize();
}
}
