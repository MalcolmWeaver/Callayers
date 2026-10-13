#include "login_return.h"
#include "cloud_client.h"
#include "heartbeat.h"
#include <winhttp.h>
#include <wincred.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <shellapi.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <fstream>

#ifndef CALLAYERS_SERVICE_URL
#define CALLAYERS_SERVICE_URL L""
#endif
namespace callayers {
using namespace winrt::Windows::Data::Json;
namespace {
std::wstring Environment(const wchar_t* name){DWORD size=GetEnvironmentVariableW(name,nullptr,0);if(!size)return {};std::wstring value(size,L'\0');auto n=GetEnvironmentVariableW(name,value.data(),size);if(n>=size)return {};value.resize(n);return value;}
std::wstring Base(){static auto base=[] {std::wstring value=CALLAYERS_SERVICE_URL;if(value.empty())value=Environment(L"CALLAYERS_API_URL");while(value.ends_with(L'/'))value.pop_back();return value;}();return base;}
std::string Hex(const BYTE* bytes,size_t size){constexpr char digits[]="0123456789abcdef";std::string out;for(size_t i=0;i<size;++i){out+=digits[bytes[i]>>4];out+=digits[bytes[i]&15];}return out;}
std::string RandomSecret(){BYTE bytes[32];if(BCryptGenRandom(nullptr,bytes,sizeof(bytes),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("Could not start a secure sign-in.");return Hex(bytes,sizeof(bytes));}
std::string Sha(const std::string& text){BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;BYTE digest[32];
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("Could not initialize sign-in.");
    auto status=BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0);
    if(status>=0)status=BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(text.data())),static_cast<ULONG>(text.size()),0);
    if(status>=0)status=BCryptFinishHash(hash,digest,sizeof(digest),0);
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);if(status<0)throw std::runtime_error("Could not initialize sign-in.");return Hex(digest,sizeof(digest));
}
std::wstring CredentialName(){return L"Callayers/FirebaseRefresh/"+std::wstring(winrt::to_hstring(Sha(winrt::to_string(Base()))));}
std::filesystem::path DataPath(const wchar_t* name){wchar_t path[MAX_PATH];Check(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,0,path));auto dir=std::filesystem::path(path)/L"Callayers"/L"cloud"/winrt::to_hstring(Sha(winrt::to_string(Base()))).c_str();std::filesystem::create_directories(dir);return dir/name;}
std::mutex credentialMutex;
std::string ReadCredential(){std::lock_guard lock(credentialMutex);PCREDENTIALW c=nullptr;if(!CredReadW(CredentialName().c_str(),CRED_TYPE_GENERIC,0,&c))return {};std::string result(reinterpret_cast<char*>(c->CredentialBlob),c->CredentialBlobSize);CredFree(c);return result;}
void SaveCredential(const std::string& token){std::lock_guard lock(credentialMutex);auto name=CredentialName();CREDENTIALW c{};c.Type=CRED_TYPE_GENERIC;c.TargetName=name.data();c.CredentialBlobSize=static_cast<DWORD>(token.size());c.CredentialBlob=reinterpret_cast<BYTE*>(const_cast<char*>(token.data()));c.Persist=CRED_PERSIST_LOCAL_MACHINE;Check(CredWriteW(&c,0)?S_OK:HRESULT_FROM_WIN32(GetLastError()));}
struct Internet {HINTERNET h;explicit Internet(HINTERNET value):h(value){if(!h)throw std::runtime_error("Could not connect to Callayers.");}~Internet(){WinHttpCloseHandle(h);}};
struct Response {DWORD status;JsonObject body;};
Response WireRequest(const std::wstring& base,const wchar_t* method,const std::wstring& path,const std::string& body,const std::string& token={},bool generation=false,const wchar_t* contentType=L"application/json"){
    URL_COMPONENTS url{};url.dwStructSize=sizeof(url);url.dwHostNameLength=DWORD(-1);url.dwUrlPathLength=DWORD(-1);url.dwExtraInfoLength=DWORD(-1);url.dwUserNameLength=DWORD(-1);url.dwPasswordLength=DWORD(-1);
    if(!WinHttpCrackUrl(base.c_str(),0,0,&url)||url.nScheme!=INTERNET_SCHEME_HTTPS||url.dwUserNameLength||url.dwPasswordLength||url.dwExtraInfoLength||url.dwUrlPathLength>1)throw std::runtime_error("Configure a valid HTTPS Callayers service address.");
    Internet session(WinHttpOpen(L"Callayers/0.5",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    WinHttpSetTimeouts(session.h,5000,5000,10000,generation?140000:10000);
    std::wstring host(url.lpszHostName,url.dwHostNameLength);
    Internet connection(WinHttpConnect(session.h,host.c_str(),url.nPort,0));
    Internet request(WinHttpOpenRequest(connection.h,method,path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
    DWORD redirects=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;WinHttpSetOption(request.h,WINHTTP_OPTION_REDIRECT_POLICY,&redirects,sizeof(redirects));
    std::wstring headers=L"Content-Type: "+std::wstring(contentType)+L"\r\n";if(!token.empty())headers+=L"Authorization: Bearer "+std::wstring(winrt::to_hstring(token))+L"\r\n";
    auto sent=WinHttpSendRequest(request.h,headers.c_str(),static_cast<DWORD>(headers.size()),body.empty()?WINHTTP_NO_REQUEST_DATA:const_cast<char*>(body.data()),static_cast<DWORD>(body.size()),static_cast<DWORD>(body.size()),0);
    SecureZeroMemory(headers.data(),headers.size()*sizeof(wchar_t));
    if(!sent||!WinHttpReceiveResponse(request.h,nullptr))throw std::runtime_error("Could not reach Callayers. Check your connection and try again.");
    DWORD status=0,size=sizeof(status);WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX);
    std::string bytes;char buffer[16384];DWORD read=0;
    do{if(!WinHttpReadData(request.h,buffer,sizeof(buffer),&read))throw std::runtime_error("The Callayers connection was interrupted.");if(bytes.size()+read>2*1024*1024)throw std::runtime_error("The service response was too large.");bytes.append(buffer,read);}while(read);
    return {status,bytes.empty()?JsonObject{}:JsonObject::Parse(winrt::to_hstring(bytes))};
}
// Serialize refreshes across account, AI, telemetry, and administrator workers.
// The key identifies the in-process login; rotated credentials are persisted
// without allowing an old request to restore a signed-out account.
std::mutex refreshMutex;
struct FirebaseTokens { std::string refreshValue, accessToken; ULONGLONG accessUntil=0; };
std::map<std::string,FirebaseTokens> firebaseTokens;
std::string FormEncode(const std::string& value){
    std::string out;constexpr char hex[]="0123456789ABCDEF";
    for(unsigned char c:value){if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')out+=char(c);else{out+='%';out+=hex[c>>4];out+=hex[c&15];}}return out;
}
Response Request(const wchar_t* method,const std::wstring& path,const std::string& body,const std::string& credential={},bool generation=false){
    std::string token;
    if(!credential.empty()){
        std::lock_guard lock(refreshMutex);
        auto& cached=firebaseTokens[credential];
        auto& refreshValue=cached.refreshValue;auto& accessToken=cached.accessToken;auto& accessUntil=cached.accessUntil;
        if(refreshValue.empty())refreshValue=credential;
        if(accessToken.empty()||GetTickCount64()>=accessUntil){
            auto config=WireRequest(Base(),L"GET",L"/v1/client-config",{});
            if(config.status>=400)return config;
            const auto apiKey=winrt::to_string(config.body.GetNamedString(L"apiKey"));
            auto renewed=WireRequest(L"https://securetoken.googleapis.com",L"POST",L"/v1/token?key="+std::wstring(winrt::to_hstring(FormEncode(apiKey))),
                "grant_type=refresh_token&refresh_token="+FormEncode(refreshValue),{},false,L"application/x-www-form-urlencoded");
            if(renewed.status>=400){
                JsonObject error;error.SetNamedValue(L"message",JsonValue::CreateStringValue(renewed.status==400||renewed.status==401||renewed.status==403?
                    L"Sign in with Google again.":L"Could not renew sign-in. Please try again."));
                return {renewed.status==400||renewed.status==401||renewed.status==403?DWORD(401):DWORD(503),error};
            }
            const auto next=winrt::to_string(renewed.body.GetNamedString(L"refresh_token"));
            const auto fresh=winrt::to_string(renewed.body.GetNamedString(L"id_token"));
            const auto seconds=std::stoul(winrt::to_string(renewed.body.GetNamedString(L"expires_in")));
            if(next.empty()||fresh.empty()||seconds<=60)throw std::runtime_error("Could not renew sign-in.");
            {
                std::lock_guard credentialLock(credentialMutex);
                PCREDENTIALW saved=nullptr;
                if(CredReadW(CredentialName().c_str(),CRED_TYPE_GENERIC,0,&saved)){
                    const bool same=std::string(reinterpret_cast<char*>(saved->CredentialBlob),saved->CredentialBlobSize)==refreshValue;CredFree(saved);
                    if(same){auto name=CredentialName();CREDENTIALW c{};c.Type=CRED_TYPE_GENERIC;c.TargetName=name.data();c.CredentialBlobSize=static_cast<DWORD>(next.size());c.CredentialBlob=reinterpret_cast<BYTE*>(const_cast<char*>(next.data()));c.Persist=CRED_PERSIST_LOCAL_MACHINE;Check(CredWriteW(&c,0)?S_OK:HRESULT_FROM_WIN32(GetLastError()));}
                }
            }
            refreshValue=next;accessToken=fresh;accessUntil=GetTickCount64()+static_cast<ULONGLONG>(seconds-60)*1000;
        }
        token=accessToken;
    }
    return WireRequest(Base(),method,path,body,token,generation);
}
void Require(const Response& response){if(response.status>=400)throw std::runtime_error(winrt::to_string(response.body.GetNamedString(L"message",L"Callayers is temporarily unavailable.")));}
struct Entry {std::wstring uid;std::string json;};
struct State {
    std::mutex mutex;std::condition_variable cv;bool stop=false,started=false;unsigned epoch=0;
    Handle finished{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    CloudAccount account;std::string token;std::string install,session;std::deque<Entry> queue;
    std::map<std::wstring,std::pair<std::wstring,HRESULT>> failures;
    bool observed=false,preview=false,consuming=false,returnToApp=false;std::atomic<bool> setupSent=false;ULONGLONG startedMs=GetTickCount64();
    HeartbeatSchedule heartbeat;
    uint64_t queueRevision=0;
    std::set<std::wstring> stage,output;
};
// Background workers own this state; no worker captures HWNDs or UI objects.
std::shared_ptr<State> Shared(){static auto state=std::make_shared<State>();return state;}
void AccountResponse(const std::shared_ptr<State>& state,const JsonObject& body,unsigned epoch){
    std::lock_guard lock(state->mutex);if(state->epoch!=epoch)return;
    auto& a=state->account;a.signedIn=true;a.administrator=body.GetNamedBoolean(L"administrator",false);a.email=body.GetNamedString(L"email");a.uid=body.GetNamedString(L"uid");
    auto setup=body.GetNamedObject(L"setup",JsonObject{});a.setupJson=setup.Stringify();a.setupComplete=setup.GetNamedBoolean(L"completed",false);
    a.message=body.GetNamedBoolean(L"paid")?L"Paid access":L"AI trial";
    {
        a.message+=L" · $"+std::to_wstring(body.GetNamedNumber(L"remainingUsd",0)).substr(0,4)+L" AI allowance remaining";
        auto end=body.GetNamedValue(L"trialEndsAt");if(!body.GetNamedBoolean(L"paid")&&end.ValueType()==JsonValueType::Number){
            auto now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            auto days=static_cast<int>(std::ceil((end.GetNumber()-double(now))/86400000.));a.message=days<=0?L"Trial ended — contact us for paid access":a.message+L" · "+std::to_wstring(days)+L" day(s) left";
        }
    }
    if(!body.GetNamedBoolean(L"aiConfigured",false))a.message+=L" · AI service not enabled yet";
}
void Persist(const std::vector<Entry>& entries){
    JsonArray data;for(const auto& e:entries){JsonObject row;row.SetNamedValue(L"uid",JsonValue::CreateStringValue(e.uid));row.SetNamedValue(L"event",JsonObject::Parse(winrt::to_hstring(e.json)));data.Append(row);}
    auto text=winrt::to_string(data.Stringify());DATA_BLOB in{static_cast<DWORD>(text.size()),reinterpret_cast<BYTE*>(text.data())},out{};
    if(!CryptProtectData(&in,L"Callayers events",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out))throw std::runtime_error("Could not persist events.");
    auto path=DataPath(L"pending-events.bin"),temp=DataPath(L"pending-events.tmp");
    bool written=false;{std::ofstream f(temp,std::ios::binary|std::ios::trunc);f.write(reinterpret_cast<char*>(out.pbData),out.cbData);f.flush();written=bool(f);}LocalFree(out.pbData);
    if(!written||!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Could not persist events.");
}
void Worker(std::shared_ptr<State> state){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);auto nextSend=Clock::now()+std::chrono::seconds(30);uint64_t savedRevision=UINT64_MAX;bool savedSetup=false;
    for(;;){
        std::vector<Entry> snapshot,batch;std::string token;bool stop;uint64_t revision;
        {std::unique_lock lock(state->mutex);state->cv.wait_for(lock,std::chrono::seconds(1),[&]{return state->stop;});stop=state->stop;
            snapshot.assign(state->queue.begin(),state->queue.end());token=state->token;revision=state->queueRevision;
            if(state->account.signedIn)for(const auto& e:snapshot)if((e.uid.empty()||e.uid==state->account.uid)&&batch.size()<50)batch.push_back(e);
        }
        try{if(revision!=savedRevision){Persist(snapshot);savedRevision=revision;}if(state->setupSent&&!savedSetup){std::ofstream(DataPath(L"vcam-used.txt"))<<"1";savedSetup=true;}}catch(...){} // local I/O failure must not affect camera output
        if(!batch.empty()&&(Clock::now()>=nextSend||stop)){
            try{
                JsonArray events;for(const auto& e:batch)events.Append(JsonObject::Parse(winrt::to_hstring(e.json)));JsonObject body;body.SetNamedValue(L"events",events);
                auto result=Request(L"POST",L"/v1/events",winrt::to_string(body.Stringify()),token);Require(result);
                std::lock_guard lock(state->mutex);for(const auto& sent:batch)std::erase_if(state->queue,[&](const Entry& e){return e.json==sent.json;});++state->queueRevision;
            }catch(...){}
            nextSend=Clock::now()+std::chrono::seconds(30);
        }
        if(stop)break;
    }
    SetEvent(state->finished.value);CoUninitialize();
}
}
struct CloudAdminJob::State {
    std::mutex mutex;
    unsigned epoch=0;
    std::optional<CloudAdminResult> result;
};
void CloudAdminJob::Start(const std::wstring& method,const std::wstring& path,const std::string& body){
    Cancel();auto shared=Shared();std::string token;auto pending=std::make_shared<State>();
    {std::lock_guard lock(shared->mutex);
        if(!shared->account.signedIn||!shared->account.administrator)throw std::runtime_error("Refresh your account to confirm administrator access.");
        token=shared->token;pending->epoch=shared->epoch;
    }
    if(!path.starts_with(L"/v1/admin/"))throw std::runtime_error("Invalid administrator request.");
    state=pending;
    std::thread([shared,pending,method,path,body,token]{
        CoInitializeEx(nullptr,COINIT_MULTITHREADED);CloudAdminResult result;
        try{
            {std::lock_guard lock(shared->mutex);if(pending->epoch!=shared->epoch)throw std::runtime_error("Account changed. Reopen Administration.");}
            auto response=Request(method.c_str(),path,body,token);
            if(response.status==401||response.status==403){std::lock_guard lock(shared->mutex);if(pending->epoch==shared->epoch)shared->account.administrator=false;}
            Require(response);result.json=winrt::to_string(response.body.Stringify());
        }catch(const std::exception& e){result.error=winrt::to_hstring(e.what());}
        catch(...){result.error=L"The administrator request failed. Refresh before retrying a change.";}
        {std::lock_guard lock(pending->mutex);pending->result=std::move(result);}CoUninitialize();
    }).detach();
}
std::optional<CloudAdminResult> CloudAdminJob::TakeResult(){
    if(!state)return {};auto pending=state;auto shared=Shared();
    {std::lock_guard lock(shared->mutex);if(shared->epoch!=pending->epoch){Cancel();return CloudAdminResult{{},L"Account changed. Reopen Administration."};}}
    std::lock_guard lock(pending->mutex);if(!pending->result)return {};auto result=std::move(pending->result);Cancel();return result;
}
struct CloudSetupJob::State {
    std::mutex mutex;
    unsigned epoch=0;
    std::optional<CloudAdminResult> result;
};
void CloudSetupJob::Start(const std::wstring& uid,const std::string& body){
    Cancel();auto shared=Shared();std::string token;auto pending=std::make_shared<State>();
    {std::lock_guard lock(shared->mutex);
        if(!shared->account.signedIn||shared->account.uid!=uid)throw std::runtime_error("Sign in to save setup.");
        token=shared->token;pending->epoch=shared->epoch;
    }
    state=pending;
    std::thread([shared,pending,body,token]{
        CoInitializeEx(nullptr,COINIT_MULTITHREADED);CloudAdminResult result;
        try{
            {std::lock_guard lock(shared->mutex);if(pending->epoch!=shared->epoch)throw std::runtime_error("Account changed. Sign in again to save setup.");}
            auto response=Request(L"PUT",L"/v1/account/setup",body,token);
            Require(response);result.json=winrt::to_string(response.body.Stringify());
            {std::lock_guard lock(shared->mutex);if(pending->epoch==shared->epoch){auto setup=response.body.GetNamedObject(L"setup");shared->account.setupJson=setup.Stringify();shared->account.setupComplete=setup.GetNamedBoolean(L"completed",false);}}
        }catch(const std::exception& e){result.error=winrt::to_hstring(e.what());}
        catch(...){result.error=L"Could not save setup. Check your connection and try again.";}
        {std::lock_guard lock(pending->mutex);pending->result=std::move(result);}CoUninitialize();
    }).detach();
}
std::optional<CloudAdminResult> CloudSetupJob::TakeResult(){
    if(!state)return {};auto pending=state;auto shared=Shared();
    {std::lock_guard lock(shared->mutex);if(shared->epoch!=pending->epoch){Cancel();return CloudAdminResult{{},L"Account changed. Sign in again to save setup."};}}
    std::lock_guard lock(pending->mutex);if(!pending->result)return {};auto result=std::move(pending->result);Cancel();return result;
}
bool CloudEnabled(){return !Base().empty();}
std::string CloudRequestId(){auto id=NewId();return winrt::to_string(id.substr(1,id.size()-2));}
void CloudStart(){
    if(!CloudEnabled())return;auto state=Shared();
    try{
        std::lock_guard lock(state->mutex);if(state->started)return;state->started=true;state->account.configured=true;state->session=CloudRequestId();state->token=ReadCredential();
        auto path=DataPath(L"installation-id.txt");{std::ifstream in(path);in>>state->install;}if(state->install.size()!=36){state->install=CloudRequestId();std::ofstream(path)<<state->install;}
        state->setupSent=std::filesystem::exists(DataPath(L"vcam-used.txt"));
        state->account.message=L"Put cards on your camera in calls. Continue with Google to get started.";
        auto pending=DataPath(L"pending-events.bin");
        if(std::filesystem::exists(pending)&&std::filesystem::file_size(pending)<4*1024*1024){
            std::ifstream file(pending,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(file)),{});
            DATA_BLOB in{static_cast<DWORD>(bytes.size()),reinterpret_cast<BYTE*>(bytes.data())},out{};
            if(CryptUnprotectData(&in,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out)){
                std::string plain(reinterpret_cast<char*>(out.pbData),out.cbData);LocalFree(out.pbData);
                try{for(const auto& row:JsonArray::Parse(winrt::to_hstring(plain))){auto o=row.GetObject();if(state->queue.size()<1000)state->queue.push_back({std::wstring(o.GetNamedString(L"uid")),winrt::to_string(o.GetNamedObject(L"event").Stringify())});}}catch(...){}
            }
        }
    }catch(...){return;}
    std::thread(Worker,state).detach();CloudTransition(L"process_state_changed",L"started");CloudRefresh();
}
void CloudStop(){if(!CloudEnabled())return;CloudTransition(L"process_state_changed",L"ended");auto state=Shared();{std::lock_guard lock(state->mutex);if(!state->started)return;state->stop=true;}state->cv.notify_all();WaitForSingleObject(state->finished.value,1500);}
bool CloudAccessAllowed(){return !CloudEnabled()||CloudGetAccount().signedIn;}
CloudAccount CloudGetAccount(){auto state=Shared();std::lock_guard lock(state->mutex);return state->account;}
bool CloudTakeSignInReturn(){auto state=Shared();std::lock_guard lock(state->mutex);return std::exchange(state->returnToApp,false);}
void CloudRefresh(){
    auto state=Shared();std::string token;unsigned epoch;
    {std::lock_guard lock(state->mutex);if(state->token.empty()||state->account.busy)return;token=state->token;epoch=state->epoch;state->account.busy=true;}
    std::thread([state,token,epoch]{CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        try{auto result=Request(L"GET",L"/v1/account",{},token);
            if(result.status==401||result.status==403){std::lock_guard lock(state->mutex);if(epoch==state->epoch){state->account.signedIn=false;state->account.administrator=false;}}
            Require(result);AccountResponse(state,result.body,epoch);CloudFailure(L"account",L"account_connected",S_OK);}
        catch(const std::exception& e){CloudFailure(L"account",L"account_connection_failed",E_FAIL);std::lock_guard lock(state->mutex);if(epoch==state->epoch){state->account.administrator=false;state->account.message=winrt::to_hstring(e.what());}}
        catch(...){}
        {std::lock_guard lock(state->mutex);if(epoch==state->epoch)state->account.busy=false;}CoUninitialize();
    }).detach();
}
void CloudSignIn(){
    if(!CloudEnabled())return;auto state=Shared();unsigned epoch;
    {std::lock_guard lock(state->mutex);if(state->account.busy)return;epoch=++state->epoch;state->account.signedIn=false;state->account.busy=true;state->account.administrator=false;state->account.message=L"Opening Google sign-in…";}
    std::thread([state,epoch]{CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        try{
            const auto returnState=RandomSecret();LoginReturn callback(returnState,winrt::to_string(Base()));
            const auto url=Base()+L"/login.html#port="+std::to_wstring(callback.port)+L"&state="+std::wstring(winrt::to_hstring(returnState));
            {std::lock_guard lock(state->mutex);if(epoch!=state->epoch){CoUninitialize();return;}state->account.message=L"Finish signing in in your browser. You will return to Callayers automatically.";}
            if(reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",url.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)throw std::runtime_error("Could not open your browser.");
            const auto deadline=Clock::now()+std::chrono::minutes(10);
            while(Clock::now()<deadline){
                {std::lock_guard lock(state->mutex);if(state->stop||epoch!=state->epoch){CoUninitialize();return;}}
                auto credential=callback.Poll();if(credential.empty())continue;
                // Verify identity and account access before remembering the credential.
                auto account=Request(L"GET",L"/v1/account",{},credential);Require(account);
                {std::lock_guard lock(state->mutex);if(epoch!=state->epoch){CoUninitialize();return;}
                    std::lock_guard refreshLock(refreshMutex);SaveCredential(firebaseTokens.at(credential).refreshValue);state->token=credential;state->account.uid=account.body.GetNamedString(L"uid");
                    std::erase_if(state->queue,[&](const Entry& e){return !e.uid.empty()&&e.uid!=state->account.uid;});++state->queueRevision;}
                AccountResponse(state,account.body,epoch);CloudFailure(L"authentication",L"signed_in",S_OK);
                {std::lock_guard lock(state->mutex);if(epoch==state->epoch){state->account.busy=false;state->returnToApp=true;}}CoUninitialize();return;
            }
            throw std::runtime_error("Sign-in timed out. Choose Continue with Google to try again.");
        }catch(const std::exception& e){CloudFailure(L"authentication",L"sign_in_failed",E_FAIL);std::lock_guard lock(state->mutex);if(epoch==state->epoch){state->account.administrator=false;state->account.message=winrt::to_hstring(e.what());}}
        catch(...){std::lock_guard lock(state->mutex);if(epoch==state->epoch)state->account.message=L"Sign-in failed. Please try again.";}
        {std::lock_guard lock(state->mutex);if(epoch==state->epoch)state->account.busy=false;}CoUninitialize();
    }).detach();
}
void CloudSignOut(){
    auto state=Shared();std::string token;
    {std::lock_guard lock(state->mutex);token=state->token;state->token.clear();state->returnToApp=false;++state->epoch;state->queue.clear();++state->queueRevision;state->account=CloudAccount{};state->account.configured=CloudEnabled();state->account.message=L"Signed out. Continue with Google to return to your deck. Your unsaved work is kept in memory.";}
    {std::lock_guard refreshLock(refreshMutex);firebaseTokens.erase(token);std::lock_guard lock(credentialMutex);CredDeleteW(CredentialName().c_str(),CRED_TYPE_GENERIC,0);}
    state->cv.notify_all();

}
void CloudTrack(const wchar_t* event,JsonObject properties){
    if(!CloudEnabled())return;
    try{
        auto state=Shared();std::lock_guard lock(state->mutex);if(!state->started)return;
        SYSTEMTIME now;GetSystemTime(&now);wchar_t stamp[40];swprintf_s(stamp,L"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,now.wMilliseconds);
        JsonObject row;row.SetNamedValue(L"id",JsonValue::CreateStringValue(winrt::to_hstring(CloudRequestId())));row.SetNamedValue(L"event",JsonValue::CreateStringValue(event));
        row.SetNamedValue(L"timestamp",JsonValue::CreateStringValue(stamp));row.SetNamedValue(L"session_id",JsonValue::CreateStringValue(winrt::to_hstring(state->session)));
        row.SetNamedValue(L"install_id",JsonValue::CreateStringValue(winrt::to_hstring(state->install)));row.SetNamedValue(L"app_version",JsonValue::CreateStringValue(L"0.5.0"));row.SetNamedValue(L"properties",properties);
        while(state->queue.size()>=1000)state->queue.pop_front();state->queue.push_back({state->account.uid,winrt::to_string(row.Stringify())});++state->queueRevision;
    }catch(...){}
}
void CloudTransition(const wchar_t* event,const wchar_t* state){try{JsonObject p;p.SetNamedValue(L"state",JsonValue::CreateStringValue(state));CloudTrack(event,p);}catch(...){} }
void CloudAnnotation(const AnnotationChange& change,size_t consumers){
    if(!CloudEnabled())return;
    try{JsonObject p;
        p.SetNamedValue(L"action",JsonValue::CreateStringValue(change.action));
        p.SetNamedValue(L"annotation_type",JsonValue::CreateStringValue(change.type));
        p.SetNamedValue(L"change_source",JsonValue::CreateStringValue(change.source));
        p.SetNamedValue(L"count",JsonValue::CreateNumberValue(double(change.count)));
        p.SetNamedValue(L"consuming",JsonValue::CreateBooleanValue(consumers>0));
        CloudTrack(L"annotation_changed",p);
    }catch(...){}
}
void CloudFailure(const wchar_t* component,const wchar_t* code,HRESULT nativeCode){
    if(!CloudEnabled())return;auto state=Shared();bool recovered=false;
    {std::lock_guard lock(state->mutex);auto& last=state->failures[component];if(last==std::pair<std::wstring,HRESULT>{code,nativeCode})return;recovered=last.second<0&&SUCCEEDED(nativeCode);last={code,nativeCode};}
    if(SUCCEEDED(nativeCode)&&!recovered)return;
    try{JsonObject p;p.SetNamedValue(L"component",JsonValue::CreateStringValue(component));p.SetNamedValue(L"code",JsonValue::CreateStringValue(code));p.SetNamedValue(L"native_code",JsonValue::CreateNumberValue(nativeCode));CloudTrack(recovered?L"recovery":L"failure",p);}catch(...){}
}
void CloudObserve(bool preview,size_t consumers,const std::vector<std::wstring>& cards,size_t annotations){
    if(!CloudEnabled())return;auto s=Shared(); // Called only by the UI thread.
    bool consuming=consumers>0;
    if(!s->observed||preview!=s->preview)CloudTransition(L"preview_state_changed",preview?L"visible":L"hidden");
    if(!s->observed||consuming!=s->consuming){JsonObject p;p.SetNamedValue(L"state",JsonValue::CreateStringValue(consuming?L"consuming":L"idle"));p.SetNamedValue(L"consumer_count",JsonValue::CreateNumberValue(double(consumers)));CloudTrack(L"vcam_state_changed",p);}
    if(consuming&&!s->setupSent){CloudTrack(L"setup_completed");s->setupSent=true;}
    std::set<std::wstring> stage(cards.begin(),cards.end()),output=consuming?stage:std::set<std::wstring>{};
    auto transitions=[](const wchar_t* event,const auto& before,const auto& after){
        auto emit=[&](const std::wstring& id,const wchar_t* state){JsonObject p;p.SetNamedValue(L"card_id",JsonValue::CreateStringValue(id));p.SetNamedValue(L"state",JsonValue::CreateStringValue(state));CloudTrack(event,p);};
        for(const auto& id:after)if(!before.contains(id))emit(id,L"shown");for(const auto& id:before)if(!after.contains(id))emit(id,L"removed");
    };
    transitions(L"card_stage_changed",s->stage,stage);transitions(L"card_output_changed",s->output,output);
    s->stage=std::move(stage);s->output=std::move(output);s->preview=preview;s->consuming=consuming;s->observed=true;
    auto uptime=GetTickCount64()-s->startedMs;
    if(s->heartbeat.Due(uptime)){
        JsonObject p;
        p.SetNamedValue(L"uptime_ms",JsonValue::CreateNumberValue(double(uptime)));
        p.SetNamedValue(L"preview_visible",JsonValue::CreateBooleanValue(preview));
        p.SetNamedValue(L"consumer_count",JsonValue::CreateNumberValue(double(consumers)));
        p.SetNamedValue(L"visible_card_count",JsonValue::CreateNumberValue(double(cards.size())));
        p.SetNamedValue(L"visible_annotation_count",JsonValue::CreateNumberValue(double(annotations)));
        CloudTrack(L"session_heartbeat",p);
    }
}
std::string CloudGenerate(const std::string& providerRequest,const std::string& requestId){
    auto state=Shared();std::string token;{std::lock_guard lock(state->mutex);token=state->token;}if(token.empty())throw std::runtime_error("Sign in with Google to generate cards.");
    auto request=JsonObject::Parse(winrt::to_hstring(providerRequest));JsonObject body;body.SetNamedValue(L"requestId",JsonValue::CreateStringValue(winrt::to_hstring(requestId)));body.SetNamedValue(L"input",JsonObject::Parse(request.GetNamedString(L"input")));
    std::optional<Response> response;
    try{response=Request(L"POST",L"/v1/generate",winrt::to_string(body.Stringify()),token,true);}catch(...){}
    // A lost HTTP response never launches a second paid call. Recover the same request.
    for(int i=0;i<45;++i){
        if(response&&response->status>=500){auto code=response->body.GetNamedString(L"code",L"");if(code.empty()||code==L"service_error")response.reset();}
        if(response){Require(*response);auto status=response->body.GetNamedString(L"state",L"");
            if(status==L"completed"){CloudRefresh();return winrt::to_string(response->body.GetNamedObject(L"response").Stringify());}
            if(status!=L"pending")throw std::runtime_error(winrt::to_string(response->body.GetNamedString(L"message",L"The request could not be completed.")));
        }
        Sleep(3000);try{response=Request(L"GET",L"/v1/requests/"+std::wstring(winrt::to_hstring(requestId)),{},token);}catch(...){response.reset();}
    }
    throw std::runtime_error("The request outcome is still unknown. Contact support with request ID "+requestId+" before starting another generation.");
}
}
