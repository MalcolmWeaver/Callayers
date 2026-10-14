#include "auth_store.h"
#include "auth_protocol.h"
#include <windows.h>
#include <stdexcept>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace callayers {
using namespace winrt::Windows::Data::Json;
static constexpr const wchar_t* Key=L"Software\\Callayers\\WorkspaceV2";
StoredAuthSession LoadAuthSession() {
    DWORD disabled=0,size=sizeof(disabled);
    LSTATUS result=RegGetValueW(HKEY_CURRENT_USER,Key,L"AuthSignedOut",RRF_RT_REG_DWORD,nullptr,&disabled,&size);
    if(result!=ERROR_SUCCESS && result!=ERROR_FILE_NOT_FOUND && result!=ERROR_PATH_NOT_FOUND)throw std::runtime_error("Session marker unreadable");
    if(disabled)return {};
    size=0;result=RegGetValueW(HKEY_CURRENT_USER,Key,L"AuthSession",RRF_RT_REG_BINARY,nullptr,nullptr,&size);
    if(result==ERROR_FILE_NOT_FOUND || result==ERROR_PATH_NOT_FOUND)return {};
    if(result!=ERROR_SUCCESS || !size || size>131072)throw std::runtime_error("Session unreadable");
    std::vector<uint8_t> bytes(size);result=RegGetValueW(HKEY_CURRENT_USER,Key,L"AuthSession",RRF_RT_REG_BINARY,nullptr,bytes.data(),&size);
    if(result!=ERROR_SUCCESS)throw std::runtime_error("Session unreadable");bytes.resize(size);
    auto json=UnprotectAuthSession(bytes);auto root=JsonObject::Parse(winrt::to_hstring(json));SecureZeroMemory(json.data(),json.size());
    if(root.GetNamedNumber(L"version")!=1)throw std::runtime_error("Unknown session format");
    StoredAuthSession session{std::wstring(root.GetNamedString(L"serviceUrl")),std::wstring(root.GetNamedString(L"projectId")),std::wstring(root.GetNamedString(L"uid")),std::wstring(root.GetNamedString(L"refreshToken"))};
    if(session.serviceUrl.empty() || session.serviceUrl.size()>2048 || session.projectId.empty() || session.projectId.size()>128 || session.uid.empty() || session.uid.size()>128 || session.refreshToken.empty() || session.refreshToken.size()>16384)throw std::runtime_error("Invalid session fields");
    return session;
}
void SaveAuthSession(const StoredAuthSession& session) {
    JsonObject root;root.SetNamedValue(L"version",JsonValue::CreateNumberValue(1));
    root.SetNamedValue(L"serviceUrl",JsonValue::CreateStringValue(session.serviceUrl));root.SetNamedValue(L"projectId",JsonValue::CreateStringValue(session.projectId));
    root.SetNamedValue(L"uid",JsonValue::CreateStringValue(session.uid));root.SetNamedValue(L"refreshToken",JsonValue::CreateStringValue(session.refreshToken));
    auto json=winrt::to_string(root.Stringify());auto bytes=ProtectAuthSession(json);SecureZeroMemory(json.data(),json.size());
    HKEY key=nullptr;LSTATUS result=RegCreateKeyExW(HKEY_CURRENT_USER,Key,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);
    if(result!=ERROR_SUCCESS)throw std::runtime_error("Session could not be saved");
    result=RegSetValueExW(key,L"AuthSession",0,REG_BINARY,bytes.data(),DWORD(bytes.size()));
    if(result==ERROR_SUCCESS){result=RegDeleteValueW(key,L"AuthSignedOut");if(result==ERROR_FILE_NOT_FOUND)result=ERROR_SUCCESS;}
    RegCloseKey(key);if(result!=ERROR_SUCCESS)throw std::runtime_error("Session could not be saved");
}
bool ClearAuthSession() {
    HKEY key=nullptr;if(RegCreateKeyExW(HKEY_CURRENT_USER,Key,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;
    DWORD disabled=1;auto marker=RegSetValueExW(key,L"AuthSignedOut",0,REG_DWORD,reinterpret_cast<const BYTE*>(&disabled),sizeof(disabled));
    auto removed=RegDeleteValueW(key,L"AuthSession");RegCloseKey(key);
    // A persisted sign-out marker also prevents reopening an undeletable session.
    return (marker==ERROR_SUCCESS || removed==ERROR_SUCCESS || removed==ERROR_FILE_NOT_FOUND) && (removed==ERROR_SUCCESS || removed==ERROR_FILE_NOT_FOUND);
}
}
