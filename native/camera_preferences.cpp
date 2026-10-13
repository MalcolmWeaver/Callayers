#include "camera_preferences.h"
#include <algorithm>
#include <vector>

namespace callayers {
// Isolated v2 key: do not read/migrate another implementation's preferences.
static constexpr const wchar_t* Key = L"Software\\Callayers\\WorkspaceV2";
static constexpr const wchar_t* Value = L"Camera";
static HRESULT WriteCamera(const CameraPreference& camera) {
    if(camera.identity.empty() || camera.name.empty() || camera.identity.size()+camera.name.size()>16380
       || camera.identity.find(L'\0')!=std::wstring::npos || camera.name.find(L'\0')!=std::wstring::npos) return E_INVALIDARG;
    std::vector<wchar_t> pair;
    pair.insert(pair.end(),camera.identity.begin(),camera.identity.end());pair.push_back(0);
    pair.insert(pair.end(),camera.name.begin(),camera.name.end());pair.push_back(0);pair.push_back(0);
    HKEY key=nullptr;
    LSTATUS status=RegCreateKeyExW(HKEY_CURRENT_USER,Key,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);
    if(status!=ERROR_SUCCESS)return HRESULT_FROM_WIN32(status);
    status=RegSetValueExW(key,Value,0,REG_MULTI_SZ,reinterpret_cast<const BYTE*>(pair.data()),DWORD(pair.size()*sizeof(wchar_t)));
    RegCloseKey(key);return status==ERROR_SUCCESS?S_OK:HRESULT_FROM_WIN32(status);
}
CameraPreferences::CameraPreferences() : writer_([this] {
    for(;;) {
        std::unique_lock lock(mutex_);
        wake_.wait(lock,[this]{return stopping_ || pending_.has_value();});
        if(!pending_)break;
        auto job=std::move(*pending_);pending_.reset();lock.unlock();
        HRESULT error=E_OUTOFMEMORY;
        try {error=WriteCamera(job.camera);} catch(...) {} // A settings failure never terminates video.
        lock.lock();result_.revision=job.revision;result_.error=error;
        if(SUCCEEDED(error))result_.saved=std::move(job.camera);
    }
}) {}
CameraPreferences::~CameraPreferences() {
    {std::lock_guard lock(mutex_);stopping_=true;}
    wake_.notify_one();writer_.join();
}
CameraPreference CameraPreferences::load(HRESULT& error) {
    error=S_OK;DWORD size=0;
    LSTATUS status=RegGetValueW(HKEY_CURRENT_USER,Key,Value,RRF_RT_REG_MULTI_SZ,nullptr,nullptr,&size);
    if(status==ERROR_FILE_NOT_FOUND || status==ERROR_PATH_NOT_FOUND)return {};
    if(status!=ERROR_SUCCESS){error=HRESULT_FROM_WIN32(status);return {};}
    if(size<3*sizeof(wchar_t) || size>32768 || size%sizeof(wchar_t)){error=HRESULT_FROM_WIN32(ERROR_INVALID_DATA);return {};}
    std::vector<wchar_t> pair(size/sizeof(wchar_t));
    status=RegGetValueW(HKEY_CURRENT_USER,Key,Value,RRF_RT_REG_MULTI_SZ,nullptr,pair.data(),&size);
    if(status!=ERROR_SUCCESS){error=HRESULT_FROM_WIN32(status);return {};}
    pair.resize(size/sizeof(wchar_t));
    auto first=std::find(pair.begin(),pair.end(),0);
    if(first==pair.begin() || first==pair.end()){error=HRESULT_FROM_WIN32(ERROR_INVALID_DATA);return {};}
    auto name=first+1, last=std::find(name,pair.end(),0);
    if(last==name || last==pair.end() || last+1==pair.end() || last+2!=pair.end() || *(last+1)!=0){error=HRESULT_FROM_WIN32(ERROR_INVALID_DATA);return {};}
    return {std::wstring(pair.begin(),first),std::wstring(name,last)};
}
unsigned CameraPreferences::save(CameraPreference camera) {
    std::lock_guard lock(mutex_);
    unsigned revision=++revision_;pending_=Pending{revision,std::move(camera)};wake_.notify_one();return revision;
}
PreferenceResult CameraPreferences::result() {std::lock_guard lock(mutex_);return result_;}
}
